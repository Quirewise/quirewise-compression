/* First-party LZMA1/LZMA2 decoder. */

#include "qw_lzma.h"

#include <stdlib.h>
#include <string.h>

#define LZMA_STATE_COUNT 12
#define LZMA_POS_STATES 16
#define LZMA_END_POS_MODEL_INDEX 14
#define LZMA_NUM_FULL_DISTANCES 128
#define LZMA_LEN_LOW_BITS 3
#define LZMA_LEN_MID_BITS 3
#define LZMA_LEN_HIGH_BITS 8
#define LZMA_PROB_TOTAL 2048
#define LZMA_MOVE_BITS 5

typedef struct QwLenDecoder {
    uint16_t choice;
    uint16_t choice2;
    uint16_t low[16][8];
    uint16_t mid[16][8];
    uint16_t high[256];
} QwLenDecoder;

typedef struct QwLzmaCore {
    const uint8_t *in;
    size_t in_size;
    size_t in_pos;
    uint8_t *out;
    size_t out_size;
    size_t out_pos;
    size_t dict_start;
    uint32_t range;
    uint32_t code;
    uint16_t *lit_probs;
    uint16_t is_match[LZMA_STATE_COUNT][LZMA_POS_STATES];
    uint16_t is_rep[LZMA_STATE_COUNT];
    uint16_t is_rep_g0[LZMA_STATE_COUNT];
    uint16_t is_rep_g1[LZMA_STATE_COUNT];
    uint16_t is_rep_g2[LZMA_STATE_COUNT];
    uint16_t is_rep0_long[LZMA_STATE_COUNT][LZMA_POS_STATES];
    uint16_t pos_slot[4][64];
    uint16_t spec_pos[128];
    uint16_t align[16];
    QwLenDecoder len;
    QwLenDecoder rep_len;
    uint32_t rep0;
    uint32_t rep1;
    uint32_t rep2;
    uint32_t rep3;
    int state;
    int lc;
    int lp;
    int pb;
    int finished;
    int props_set;
    int allow_eof_marker;
} QwLzmaCore;

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void probs_reset(uint16_t *probs, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) probs[i] = LZMA_PROB_TOTAL / 2u;
}

static void core_reset_probs(QwLzmaCore *c)
{
    size_t lit_count = (size_t)0x300u << (unsigned)(c->lc + c->lp);
    probs_reset(c->lit_probs, lit_count);
    probs_reset(&c->is_match[0][0], sizeof(c->is_match) / sizeof(uint16_t));
    probs_reset(c->is_rep, LZMA_STATE_COUNT);
    probs_reset(c->is_rep_g0, LZMA_STATE_COUNT);
    probs_reset(c->is_rep_g1, LZMA_STATE_COUNT);
    probs_reset(c->is_rep_g2, LZMA_STATE_COUNT);
    probs_reset(&c->is_rep0_long[0][0],
                sizeof(c->is_rep0_long) / sizeof(uint16_t));
    probs_reset(&c->pos_slot[0][0], sizeof(c->pos_slot) / sizeof(uint16_t));
    probs_reset(c->spec_pos, sizeof(c->spec_pos) / sizeof(uint16_t));
    probs_reset(c->align, 16);
    probs_reset(&c->len.choice, 1);
    probs_reset(&c->len.choice2, 1);
    probs_reset(&c->len.low[0][0], sizeof(c->len.low) / sizeof(uint16_t));
    probs_reset(&c->len.mid[0][0], sizeof(c->len.mid) / sizeof(uint16_t));
    probs_reset(c->len.high, 256);
    probs_reset(&c->rep_len.choice, 1);
    probs_reset(&c->rep_len.choice2, 1);
    probs_reset(&c->rep_len.low[0][0],
                sizeof(c->rep_len.low) / sizeof(uint16_t));
    probs_reset(&c->rep_len.mid[0][0],
                sizeof(c->rep_len.mid) / sizeof(uint16_t));
    probs_reset(c->rep_len.high, 256);
    c->state = 0;
    c->rep0 = c->rep1 = c->rep2 = c->rep3 = 0;
    c->finished = 0;
}

static int core_set_props(QwLzmaCore *c, int lc, int lp, int pb)
{
    size_t lit_count;
    uint16_t *lit;
    if (lc < 0 || lc > 8 || lp < 0 || lp > 4 || pb < 0 || pb > 4) return 0;
    if (lc + lp > 4) return 0;
    lit_count = (size_t)0x300u << (unsigned)(lc + lp);
    lit = (uint16_t *)malloc(lit_count * sizeof(uint16_t));
    if (!lit) return 0;
    free(c->lit_probs);
    c->lit_probs = lit;
    c->lc = lc;
    c->lp = lp;
    c->pb = pb;
    c->props_set = 1;
    core_reset_probs(c);
    return 1;
}

int qw_lzma_props_parse(const uint8_t props[5], QwLzmaProps *out)
{
    int lc;
    int rem;
    if (!props || !out) return 0;
    if (props[0] >= 9 * 5 * 5) return 0;
    lc = props[0] % 9;
    rem = props[0] / 9;
    out->pb = rem / 5;
    out->lp = rem % 5;
    out->lc = lc;
    out->dict_size = read_u32_le(props + 1);
    if (out->dict_size < 4096u) out->dict_size = 4096u;
    if (lc + out->lp > 4) return 0;
    return 1;
}

uint32_t qw_lzma2_dict_size(uint8_t props_byte)
{
    if (props_byte >= 40u) return 0xFFFFFFFFu;
    return ((uint32_t)2u | ((uint32_t)props_byte & 1u)) <<
           ((uint32_t)props_byte / 2u + 11u);
}

static int rc_normalize(QwLzmaCore *c)
{
    if (c->range < (1u << 24)) {
        if (c->in_pos >= c->in_size) return 0;
        c->range <<= 8;
        c->code = (c->code << 8) | (uint32_t)c->in[c->in_pos++];
    }
    return 1;
}

static int rc_init(QwLzmaCore *c)
{
    if (c->in_pos + 5u > c->in_size) return 0;
    if (c->in[c->in_pos++] != 0u) return 0;
    c->code = read_u32_be(c->in + c->in_pos);
    c->in_pos += 4u;
    c->range = 0xFFFFFFFFu;
    return 1;
}

static int rc_bit(QwLzmaCore *c, uint16_t *prob)
{
    uint32_t bound = (c->range >> 11) * (uint32_t)*prob;
    int bit;
    if (c->code < bound) {
        c->range = bound;
        *prob = (uint16_t)(*prob + ((LZMA_PROB_TOTAL - *prob) >> LZMA_MOVE_BITS));
        bit = 0;
    } else {
        c->range -= bound;
        c->code -= bound;
        *prob = (uint16_t)(*prob - (*prob >> LZMA_MOVE_BITS));
        bit = 1;
    }
    if (!rc_normalize(c)) return -1;
    return bit;
}

static int rc_direct_bits(QwLzmaCore *c, uint32_t *out_value, int count)
{
    uint32_t value = 0;
    int i;
    for (i = 0; i < count; ++i) {
        int bit;
        c->range >>= 1;
        if (c->code >= c->range) {
            c->code -= c->range;
            bit = 1;
        } else {
            bit = 0;
        }
        value = (value << 1) | (uint32_t)bit;
        if (!rc_normalize(c)) return 0;
    }
    *out_value = value;
    return 1;
}

static int rc_tree(QwLzmaCore *c, uint16_t *probs, int num_bits,
                   uint32_t *out_symbol)
{
    uint32_t symbol = 1;
    int i;
    for (i = 0; i < num_bits; ++i) {
        int bit = rc_bit(c, &probs[symbol]);
        if (bit < 0) return 0;
        symbol = (symbol << 1) | (uint32_t)bit;
    }
    *out_symbol = symbol - (1u << num_bits);
    return 1;
}

static int rc_tree_reverse(QwLzmaCore *c, uint16_t *probs, int num_bits,
                           uint32_t *out_symbol)
{
    uint32_t symbol = 1;
    uint32_t value = 0;
    int i;
    for (i = 0; i < num_bits; ++i) {
        int bit = rc_bit(c, &probs[symbol]);
        if (bit < 0) return 0;
        symbol = (symbol << 1) | (uint32_t)bit;
        value |= (uint32_t)bit << i;
    }
    *out_symbol = value;
    return 1;
}

static int len_decode(QwLzmaCore *c, QwLenDecoder *decoder, int pos_state,
                      uint32_t *out_len)
{
    int bit = rc_bit(c, &decoder->choice);
    uint32_t value;
    if (bit < 0) return 0;
    if (bit == 0) {
        if (!rc_tree(c, decoder->low[pos_state], LZMA_LEN_LOW_BITS, &value))
            return 0;
    } else {
        int bit2 = rc_bit(c, &decoder->choice2);
        if (bit2 < 0) return 0;
        if (bit2 == 0) {
            if (!rc_tree(c, decoder->mid[pos_state], LZMA_LEN_MID_BITS, &value))
                return 0;
            value += 8;
        } else {
            if (!rc_tree(c, decoder->high, LZMA_LEN_HIGH_BITS, &value))
                return 0;
            value += 16;
        }
    }
    *out_len = value + 2u;
    return 1;
}

static int dist_decode(QwLzmaCore *c, uint32_t len, uint32_t *out_dist)
{
    int len_to_pos = (int)(len - 2u < 4u ? len - 2u : 3u);
    uint32_t pos_slot;
    uint32_t dist;
    if (!rc_tree(c, c->pos_slot[len_to_pos], 6, &pos_slot)) return 0;
    if (pos_slot < 4u) {
        *out_dist = pos_slot;
        return 1;
    }
    {
        int num_direct = (int)(pos_slot >> 1) - 1;
        uint32_t base = (2u | (pos_slot & 1u)) << (unsigned)num_direct;
        if (pos_slot < LZMA_END_POS_MODEL_INDEX) {
            uint16_t *probs = c->spec_pos + (base - pos_slot);
            uint32_t m = 1;
            uint32_t symbol = 0;
            int i;
            for (i = 0; i < num_direct; ++i) {
                int bit = rc_bit(c, &probs[m]);
                if (bit < 0) return 0;
                m = (m << 1) | (uint32_t)bit;
                symbol |= (uint32_t)bit << i;
            }
            dist = base + symbol;
        } else {
            uint32_t direct;
            uint32_t align;
            if (!rc_direct_bits(c, &direct, num_direct - 4)) return 0;
            if (!rc_tree_reverse(c, c->align, 4, &align)) return 0;
            dist = base + (direct << 4) + align;
        }
    }
    *out_dist = dist;
    return 1;
}

static int copy_match(QwLzmaCore *c, uint32_t dist, uint32_t len)
{
    size_t src;
    if (dist == 0xFFFFFFFFu) {
        c->finished = 1;
        return 1;
    }
    if ((size_t)dist + 1u > c->out_pos - c->dict_start) return 0;
    if ((size_t)len > c->out_size - c->out_pos) return 0;
    src = c->out_pos - (size_t)dist - 1u;
    while (len--) {
        c->out[c->out_pos++] = c->out[src++];
    }
    return 1;
}

static int decode_literal(QwLzmaCore *c)
{
    uint32_t prev = c->out_pos > c->dict_start ? c->out[c->out_pos - 1u] : 0u;
    uint32_t lit_state = (uint32_t)(c->out_pos - c->dict_start) &
                         ((1u << (unsigned)c->lp) - 1u);
    uint16_t *probs;
    uint32_t symbol = 1;
    if (c->out_pos >= c->out_size) return 0;
    lit_state = (lit_state << (unsigned)c->lc) +
                (prev >> (8 - (unsigned)c->lc));
    probs = c->lit_probs + 0x300u * lit_state;
    if (c->state >= 7) {
        uint32_t match_byte = c->out[c->out_pos - (size_t)c->rep0 - 1u];
        for (;;) {
            int match_bit = (int)((match_byte >> 7) & 1u);
            int bit;
            match_byte <<= 1;
            bit = rc_bit(c, &probs[((unsigned)(1 + match_bit) << 8) + symbol]);
            if (bit < 0) return 0;
            symbol = (symbol << 1) | (uint32_t)bit;
            if (match_bit != bit) {
                while (symbol < 0x100u) {
                    int b = rc_bit(c, &probs[symbol]);
                    if (b < 0) return 0;
                    symbol = (symbol << 1) | (uint32_t)b;
                }
                break;
            }
            if (symbol >= 0x100u) break;
        }
    } else {
        while (symbol < 0x100u) {
            int bit = rc_bit(c, &probs[symbol]);
            if (bit < 0) return 0;
            symbol = (symbol << 1) | (uint32_t)bit;
        }
    }
    c->out[c->out_pos++] = (uint8_t)(symbol - 0x100u);
    if (c->state < 4) c->state = 0;
    else if (c->state < 10) c->state -= 3;
    else c->state -= 6;
    return 1;
}

static int decode_one(QwLzmaCore *c)
{
    int pos_state;
    int bit;
    pos_state = (int)((c->out_pos - c->dict_start) &
                      ((1u << (unsigned)c->pb) - 1u));
    bit = rc_bit(c, &c->is_match[c->state][pos_state]);
    if (bit < 0) return 0;
    if (bit == 0) return decode_literal(c) ? 1 : 0;

    {
        int is_rep = rc_bit(c, &c->is_rep[c->state]);
        uint32_t dist;
        uint32_t len;
        if (is_rep < 0) return 0;
        if (is_rep) {
            int g0 = rc_bit(c, &c->is_rep_g0[c->state]);
            if (g0 < 0) return 0;
            if (g0 == 0) {
                int long_rep = rc_bit(c, &c->is_rep0_long[c->state][pos_state]);
                if (long_rep < 0) return 0;
                if (long_rep == 0) {
                    c->state = c->state < 7 ? 9 : 11;
                    if (!copy_match(c, c->rep0, 1u)) return 0;
                    return 1;
                }
                dist = c->rep0;
            } else {
                int g1 = rc_bit(c, &c->is_rep_g1[c->state]);
                if (g1 < 0) return 0;
                if (g1 == 0) {
                    dist = c->rep1;
                } else {
                    int g2 = rc_bit(c, &c->is_rep_g2[c->state]);
                    if (g2 < 0) return 0;
                    if (g2 == 0) {
                        dist = c->rep2;
                    } else {
                        dist = c->rep3;
                        c->rep3 = c->rep2;
                    }
                    c->rep2 = c->rep1;
                }
                c->rep1 = c->rep0;
                c->rep0 = dist;
            }
            if (!len_decode(c, &c->rep_len, pos_state, &len)) return 0;
            c->state = c->state < 7 ? 8 : 11;
            if (!copy_match(c, dist, len)) return 0;
            return 1;
        }
        c->rep3 = c->rep2;
        c->rep2 = c->rep1;
        c->rep1 = c->rep0;
        if (!len_decode(c, &c->len, pos_state, &len)) return 0;
        c->state = c->state < 7 ? 7 : 10;
        if (!dist_decode(c, len, &dist)) return 0;
        c->rep0 = dist;
        if (dist == 0xFFFFFFFFu) {
            c->finished = 1;
            return 2;
        }
        if (!copy_match(c, dist, len)) return 0;
        return 1;
    }
}

static int core_decode(QwLzmaCore *c)
{
    for (;;) {
        int r;
        if (c->finished) return 1;
        if (c->out_pos == c->out_size) {
            if (!c->allow_eof_marker) return 1;
            /* LZMA1: look for the optional end marker; a stream without one
             * is accepted when the input is exhausted. */
            if (c->in_pos >= c->in_size) return 1;
        }
        r = decode_one(c);
        if (r == 0) return 0;
        if (r == 2) return 1;
        if (c->out_pos > c->out_size) return 0;
    }
}

int qw_lzma1_decode(const uint8_t *in, size_t in_size, const QwLzmaProps *props,
                    uint8_t *out, size_t out_size, size_t *in_used)
{
    QwLzmaCore core;
    int ok;
    memset(&core, 0, sizeof(core));
    if (!in || !out || !props) return 0;
    if (!core_set_props(&core, props->lc, props->lp, props->pb)) return 0;
    core.in = in;
    core.in_size = in_size;
    core.out = out;
    core.out_size = out_size;
    core.allow_eof_marker = 1;
    if (!rc_init(&core)) {
        free(core.lit_probs);
        return 0;
    }
    ok = core_decode(&core);
    if (ok && in_used) *in_used = core.in_pos;
    free(core.lit_probs);
    return ok;
}

int qw_lzma2_decode(const uint8_t *in, size_t in_size, uint8_t props_byte,
                    uint8_t *out, size_t out_size, size_t *in_used,
                    size_t *out_used)
{
    QwLzmaCore core;
    size_t in_pos = 0;
    size_t out_pos = 0;
    int current_props = props_byte;
    int props_known = 0;

    memset(&core, 0, sizeof(core));
    if (!in || (!out && out_size != 0u)) return 0;
    core.out = out;
    core.out_size = out_size;
    if (!core_set_props(&core, 3, 0, 2)) return 0;
    core.props_set = 0;

    for (;;) {
        uint8_t control;
        if (in_pos >= in_size) goto fail;
        control = in[in_pos++];
        if (control == 0x00u) {
            if (in_used) *in_used = in_pos;
            if (out_used) *out_used = out_pos;
            free(core.lit_probs);
            return 1;
        }
        if (control < 0x80u) {
            uint32_t size;
            int reset_dict = control == 0x01u;
            if (control != 0x01u && control != 0x02u) goto fail;
            if (in_pos + 2u > in_size) goto fail;
            size = (((uint32_t)in[in_pos] << 8) | (uint32_t)in[in_pos + 1u]) + 1u;
            in_pos += 2u;
            if (reset_dict) {
                core.dict_start = out_pos;
            }
            if (in_pos + size > in_size) goto fail;
            if (size > out_size - out_pos) goto fail;
            memcpy(out + out_pos, in + in_pos, size);
            out_pos += size;
            in_pos += size;
            core.out_pos = out_pos;
            continue;
        }
        {
            uint32_t usize;
            uint32_t csize;
            int reset_state;
            int reset_dict;
            int has_props;
            if (in_pos + 4u > in_size) goto fail;
            usize = (((uint32_t)(control & 0x1Fu) << 16) |
                     ((uint32_t)in[in_pos] << 8) |
                     (uint32_t)in[in_pos + 1u]) + 1u;
            in_pos += 2u;
            csize = (((uint32_t)in[in_pos] << 8) | (uint32_t)in[in_pos + 1u]) + 1u;
            in_pos += 2u;
            reset_state = control >= 0xA0u;
            reset_dict = control >= 0xE0u;
            has_props = (control & 0x40u) != 0u;
            if (has_props) {
                uint8_t b;
                int lc, lp, pb;
                if (in_pos >= in_size) goto fail;
                b = in[in_pos++];
                if (b >= 9u * 5u * 5u) goto fail;
                lc = b % 9;
                pb = (b / 9) / 5;
                lp = (b / 9) % 5;
                if (!core_set_props(&core, lc, lp, pb)) goto fail;
                current_props = b;
                props_known = 1;
            }
            if (!props_known) goto fail;
            (void)current_props;
            if (reset_dict) core.dict_start = out_pos;
            if (in_pos + csize > in_size) goto fail;
            if (usize > out_size - out_pos) goto fail;
            if (reset_state) core_reset_probs(&core);
            core.in = in + in_pos;
            core.in_size = csize;
            core.in_pos = 0;
            core.out_pos = out_pos;
            core.out_size = out_pos + usize;
            core.finished = 0;
            if (!rc_init(&core)) goto fail;
            {
                size_t consumed;
                int ok = core_decode(&core);
                consumed = core.in_pos;
                if (!ok) goto fail;
                /* The compressed chunk must be fully consumed. */
                if (consumed != csize) goto fail;
                out_pos = core.out_pos;
                in_pos += csize;
                if (out_pos != core.out_size && out_pos != out_size) goto fail;
            }
        }
    }

fail:
    free(core.lit_probs);
    return 0;
}
