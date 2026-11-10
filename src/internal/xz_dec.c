/* First-party XZ container decoder: stream header, block headers, LZMA2 and
 * Delta/BCJ filters, CRC32/CRC64/SHA-256 integrity checks, index and footer. */

#include "xz_dec.h"

#include "qw_lzma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XZ_CHECK_NONE 0u
#define XZ_CHECK_CRC32 1u
#define XZ_CHECK_CRC64 4u
#define XZ_CHECK_SHA256 10u
#define XZ_FILTER_DELTA 0x03u
#define XZ_FILTER_X86 0x04u
#define XZ_FILTER_POWERPC 0x05u
#define XZ_FILTER_IA64 0x06u
#define XZ_FILTER_ARM 0x07u
#define XZ_FILTER_ARMTHUMB 0x08u
#define XZ_FILTER_SPARC 0x09u
#define XZ_FILTER_LZMA2 0x21u

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rd_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int vli_decode(const uint8_t *p, size_t available, uint64_t *value,
                      size_t *used)
{
    uint64_t result = 0;
    int shift = 0;
    size_t i;
    for (i = 0; i < 9u; ++i) {
        uint8_t byte;
        if (i >= available) return 0;
        byte = p[i];
        result |= (uint64_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0u) {
            if (i > 0u && byte == 0u) return 0;
            *value = result;
            *used = i + 1u;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* CRC64 (reflected polynomial 0xC96C5795D7870F42)                   */
/* ---------------------------------------------------------------- */

static uint64_t crc64_table[256];
static int crc64_ready;

static void crc64_init(void)
{
    uint64_t i;
    for (i = 0; i < 256u; ++i) {
        uint64_t crc = i;
        int j;
        for (j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT64_C(0xC96C5795D7870F42) : 0u);
        crc64_table[i] = crc;
    }
    crc64_ready = 1;
}

static uint64_t crc64_update(uint64_t crc, const uint8_t *data, size_t size)
{
    size_t i;
    if (!crc64_ready) crc64_init();
    for (i = 0; i < size; ++i)
        crc = (crc >> 8) ^ crc64_table[(uint8_t)(crc ^ data[i])];
    return crc;
}

/* ---------------------------------------------------------------- */
/* SHA-256                                                           */
/* ---------------------------------------------------------------- */

typedef struct QwSha256 {
    uint32_t h[8];
    uint64_t total;
    uint8_t block[64];
    size_t used;
} QwSha256;

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t ror32(uint32_t v, int n)
{
    return (v >> n) | (v << (32 - n));
}

static void sha256_transform(QwSha256 *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; ++i) w[i] = rd_u32_be(block + 4 * i);
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
    e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];
    for (i = 0; i < 64; ++i) {
        uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void sha256_init(QwSha256 *ctx)
{
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u;
    ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu;
    ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->total = 0;
    ctx->used = 0;
}

static void sha256_update(QwSha256 *ctx, const uint8_t *data, size_t size)
{
    size_t i;
    ctx->total += size;
    for (i = 0; i < size; ++i) {
        ctx->block[ctx->used++] = data[i];
        if (ctx->used == 64u) {
            sha256_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sha256_final(QwSha256 *ctx, uint8_t out[32])
{
    uint64_t bits = ctx->total * 8u;
    uint8_t pad = 0x80;
    uint8_t zero = 0;
    int i;
    sha256_update(ctx, &pad, 1);
    while (ctx->used != 56u) sha256_update(ctx, &zero, 1);
    {
        uint8_t length[8];
        for (i = 0; i < 8; ++i) length[i] = (uint8_t)(bits >> (56 - 8 * i));
        sha256_update(ctx, length, 8);
    }
    for (i = 0; i < 8; ++i) wr_u32_be(out + 4 * i, ctx->h[i]);
}

/* ---------------------------------------------------------------- */
/* BCJ filters (xz variant)                                          */
/* ---------------------------------------------------------------- */

static void xz_delta_decode(uint8_t *data, size_t size, unsigned distance)
{
    size_t i;
    if (distance == 0u || distance > 256u) return;
    for (i = 0; i < size; ++i) {
        if (i >= distance) data[i] = (uint8_t)(data[i] + data[i - distance]);
    }
}

static void xz_x86_decode(uint8_t *data, size_t size, uint32_t pc)
{
    size_t i = 0;
    while (i + 5u <= size) {
        uint8_t opcode = data[i];
        if (opcode == 0xE8u || opcode == 0xE9u) {
            uint32_t address = rd_u32_le(data + i + 1u);
            address -= (uint32_t)(pc + (uint32_t)i + 5u);
            wr_u32_le(data + i + 1u, address);
            i += 5u;
        } else {
            ++i;
        }
    }
}

static void xz_ppc_decode(uint8_t *data, size_t size, uint32_t pc)
{
    size_t i;
    for (i = 0; i + 4u <= size; i += 4u) {
        if ((data[i] >> 2) == 0x12u && (data[i + 3u] & 0x03u) == 0x01u) {
            uint32_t value = rd_u32_be(data + i);
            uint32_t address = ((value & 0x03FFFFFCu) << 2) -
                               (pc + (uint32_t)i);
            value = 0x48000000u | ((address >> 2) & 0x03FFFFFCu);
            wr_u32_be(data + i, value);
        }
    }
}

static void xz_arm_decode(uint8_t *data, size_t size, uint32_t pc)
{
    size_t i;
    for (i = 0; i + 4u <= size; i += 4u) {
        if (data[i + 3u] == 0xEBu) {
            uint32_t value = rd_u32_le(data + i) & 0x00FFFFFFu;
            int32_t address = (int32_t)(value << 2) -
                              (int32_t)(pc + (uint32_t)i + 8u);
            value = ((uint32_t)(address >> 2)) & 0x00FFFFFFu;
            wr_u32_le(data + i, value);
            data[i + 3u] = 0xEBu;
        }
    }
}

static void xz_armt_decode(uint8_t *data, size_t size, uint32_t pc)
{
    size_t i;
    for (i = 0; i + 4u <= size; i += 2u) {
        uint16_t v1 = (uint16_t)(data[i] | (data[i + 1u] << 8));
        uint16_t v2 = (uint16_t)(data[i + 2u] | (data[i + 3u] << 8));
        if ((v1 & 0xF800u) == 0xF000u && (v2 & 0xF800u) == 0xF800u) {
            uint32_t address = (((uint32_t)(v1 & 0x07FFu) << 12) |
                                ((uint32_t)(v2 & 0x07FFu) << 1)) -
                               (pc + (uint32_t)i + 4u);
            v1 = (uint16_t)((v1 & 0xF800u) | ((address >> 12) & 0x07FFu));
            v2 = (uint16_t)((v2 & 0xF800u) | ((address >> 1) & 0x07FFu));
            data[i] = (uint8_t)v1;
            data[i + 1u] = (uint8_t)(v1 >> 8);
            data[i + 2u] = (uint8_t)v2;
            data[i + 3u] = (uint8_t)(v2 >> 8);
        }
    }
}

static void xz_sparc_decode(uint8_t *data, size_t size, uint32_t pc)
{
    size_t i;
    for (i = 0; i + 4u <= size; i += 4u) {
        uint32_t value = rd_u32_be(data + i);
        if (((value >> 22) == 0x100u) || ((value >> 22) == 0x1FFu)) {
            uint32_t address = ((value & 0x3FFFFFFFu) << 2) -
                               (pc + (uint32_t)i);
            value = 0x40000000u | ((address >> 2) & 0x3FFFFFFFu);
            wr_u32_be(data + i, value);
        }
    }
}

typedef struct QwXzFilter {
    uint64_t id;
    uint8_t props[64];
    size_t props_size;
} QwXzFilter;

static int apply_filter(const QwXzFilter *filter, uint8_t *data, size_t size)
{
    uint32_t pc = 0u;
    switch (filter->id) {
    case XZ_FILTER_DELTA:
        if (filter->props_size != 1u) return 0;
        xz_delta_decode(data, size, (unsigned)filter->props[0] + 1u);
        return 1;
    case XZ_FILTER_X86:
        if (filter->props_size == 4u) pc = rd_u32_le(filter->props);
        else if (filter->props_size != 0u) return 0;
        xz_x86_decode(data, size, pc);
        return 1;
    case XZ_FILTER_POWERPC:
        if (filter->props_size == 4u) pc = rd_u32_le(filter->props);
        else if (filter->props_size != 0u) return 0;
        xz_ppc_decode(data, size, pc);
        return 1;
    case XZ_FILTER_ARM:
        if (filter->props_size == 4u) pc = rd_u32_le(filter->props);
        else if (filter->props_size != 0u) return 0;
        xz_arm_decode(data, size, pc);
        return 1;
    case XZ_FILTER_ARMTHUMB:
        if (filter->props_size == 4u) pc = rd_u32_le(filter->props);
        else if (filter->props_size != 0u) return 0;
        xz_armt_decode(data, size, pc);
        return 1;
    case XZ_FILTER_SPARC:
        if (filter->props_size == 4u) pc = rd_u32_le(filter->props);
        else if (filter->props_size != 0u) return 0;
        xz_sparc_decode(data, size, pc);
        return 1;
    default:
        return 0;
    }
}

static size_t check_size(unsigned check_type)
{
    switch (check_type) {
    case XZ_CHECK_NONE: return 0u;
    case XZ_CHECK_CRC32: return 4u;
    case XZ_CHECK_CRC64: return 8u;
    case XZ_CHECK_SHA256: return 32u;
    default: return SIZE_MAX;
    }
}

static int verify_check(unsigned check_type, const uint8_t *check,
                        const uint8_t *data, size_t size)
{
    if (check_type == XZ_CHECK_CRC32) {
        uint32_t crc = 0xFFFFFFFFu;
        size_t i;
        for (i = 0; i < size; ++i) {
            int j;
            crc ^= data[i];
            for (j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
        crc ^= 0xFFFFFFFFu;
        return rd_u32_le(check) == crc;
    }
    if (check_type == XZ_CHECK_CRC64) {
        uint64_t crc = crc64_update(UINT64_MAX, data, size) ^ UINT64_MAX;
        uint64_t stored = 0;
        int i;
        for (i = 0; i < 8; ++i) stored |= (uint64_t)check[i] << (8 * i);
        return stored == crc;
    }
    if (check_type == XZ_CHECK_SHA256) {
        QwSha256 ctx;
        uint8_t digest[32];
        sha256_init(&ctx);
        sha256_update(&ctx, data, size);
        sha256_final(&ctx, digest);
        return memcmp(digest, check, 32) == 0;
    }
    return 1;
}

static int decode_block(const uint8_t *in, size_t in_size,
                        uint8_t **block_out, size_t *block_size,
                        size_t *consumed, size_t memory_limit,
                        int prefix, size_t out_capacity)
{
    size_t pos = 0;
    uint8_t flags;
    QwXzFilter filters[4];
    size_t filter_count;
    uint64_t comp_size = 0;
    uint64_t uncomp_size = 0;
    int has_comp = 0;
    int has_uncomp = 0;
    uint8_t *block = NULL;
    size_t capacity = 0;
    size_t produced = 0;
    size_t header_size;
    size_t i;
    size_t used;
    size_t data_start;
    uint32_t header_crc;

    if (in_size < 8u) return 0;
    header_size = ((size_t)in[0] + 1u) * 4u;
    if (header_size < 8u || header_size > in_size) return 0;
    header_crc = rd_u32_le(in + header_size - 4u);
    {
        uint32_t crc = 0xFFFFFFFFu;
        size_t k;
        for (k = 0; k < header_size - 4u; ++k) {
            int j;
            crc ^= in[k];
            for (j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
        crc ^= 0xFFFFFFFFu;
        if (crc != header_crc) return 0;
    }
    flags = in[1];
    if ((flags & 0x3Cu) != 0u) return 0;
    filter_count = (size_t)(flags & 0x03u) + 1u;
    if (filter_count > 4u) return 0;
    pos = 2;
    if (flags & 0x40u) {
        if (!vli_decode(in + pos, header_size - 4u - pos, &comp_size, &used))
            return 0;
        pos += used;
        has_comp = 1;
    }
    if (flags & 0x80u) {
        if (!vli_decode(in + pos, header_size - 4u - pos, &uncomp_size, &used))
            return 0;
        pos += used;
        has_uncomp = 1;
    }
    for (i = 0; i < filter_count; ++i) {
        uint64_t id;
        uint64_t props_size;
        if (!vli_decode(in + pos, header_size - 4u - pos, &id, &used)) return 0;
        pos += used;
        if (!vli_decode(in + pos, header_size - 4u - pos, &props_size, &used))
            return 0;
        pos += used;
        if (props_size > 64u || props_size > header_size - 4u - pos) return 0;
        filters[i].id = id;
        filters[i].props_size = (size_t)props_size;
        memcpy(filters[i].props, in + pos, (size_t)props_size);
        pos += (size_t)props_size;
    }
    while (pos < header_size - 4u) {
        if (in[pos] != 0u) return 0;
        ++pos;
    }
    if (filters[filter_count - 1u].id != XZ_FILTER_LZMA2) return 0;
    if (filters[filter_count - 1u].props_size != 1u) return 0;
    if ((size_t)qw_lzma2_dict_size(filters[filter_count - 1u].props[0]) >
        memory_limit)
        return 0;

    if (getenv("XZ_DBG")) fprintf(stderr, "hdr ok hsize=%zu filters=%zu has_comp=%d has_uncomp=%d comp=%llu uncomp=%llu cap=%zu\n", header_size, filter_count, has_comp, has_uncomp, (unsigned long long)comp_size, (unsigned long long)uncomp_size, out_capacity);
    data_start = header_size;
    if (has_comp && comp_size > in_size - data_start) return 0;
    if (has_uncomp && uncomp_size > SIZE_MAX) return 0;

    if (has_uncomp) {
        capacity = (size_t)uncomp_size;
        if (!prefix && capacity > out_capacity) return 0;
    } else if (!prefix) {
        capacity = out_capacity;
        if (capacity == 0u) capacity = 1u;
    } else {
        capacity = out_capacity + (1u << 20);
        if (capacity < out_capacity) return 0;
    }
    if (capacity == 0u) capacity = 1u;
    block = (uint8_t *)malloc(capacity);
    if (!block) return 0;
    for (;;) {
        size_t in_used = 0;
        size_t out_used = 0;
        int ok = qw_lzma2_decode(
            in + data_start,
            has_comp ? (size_t)comp_size : (in_size - data_start),
            filters[filter_count - 1u].props[0], block, capacity, &in_used,
            &out_used);
        if (!ok) {
            if (!prefix || has_comp || has_uncomp || capacity > ((size_t)1 << 26)) {
                free(block);
                return 0;
            }
            capacity *= 2u;
            block = (uint8_t *)realloc(block, capacity);
            if (!block) return 0;
            continue;
        }
        if (getenv("XZ_DBG")) fprintf(stderr, "chunk ok in=%zu out=%zu\n", in_used, out_used);
        if (has_uncomp && out_used != (size_t)uncomp_size) {
            free(block);
            return 0;
        }
        if (!prefix && out_used > out_capacity) {
            free(block);
            return 0;
        }
        produced = out_used;
        *consumed = header_size + in_used;
        break;
    }
    for (i = filter_count - 1u; i > 0u; --i) {
        if (!apply_filter(&filters[i - 1u], block, produced)) {
            free(block);
            return 0;
        }
    }
    *block_out = block;
    *block_size = produced;
    return 1;
}

int qw_xz_decode(const uint8_t *in, size_t in_size, uint8_t *out,
                 size_t out_size, size_t memory_limit, int allow_prefix,
                 size_t *out_written)
{
    static const uint8_t magic[6] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
    size_t pos;
    size_t out_pos = 0;
    unsigned check_type;
    size_t check_len;
    int block_count = 0;

    if (out_written) *out_written = 0u;
    if (!in || in_size < 12u || !memory_limit) return 0;
    if (!out && out_size != 0u) return 0;
    if (memcmp(in, magic, 6u) != 0) return 0;
    check_type = (unsigned)in[7];
    if (in[6] != 0u) return 0;
    check_len = check_size(check_type);
    if (check_len == SIZE_MAX) return 0;
    {
        uint32_t crc = 0xFFFFFFFFu;
        size_t k;
        for (k = 6; k < 8; ++k) {
            int j;
            crc ^= in[k];
            for (j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
        crc ^= 0xFFFFFFFFu;
    }
    pos = 12u;
    for (;;) {
        if (pos >= in_size) return 0;
        if (in[pos] == 0u) break; /* index */
        {
            uint8_t *block = NULL;
            size_t block_size = 0;
            size_t consumed = 0;
            size_t want;
            if (!decode_block(in + pos, in_size - pos, &block, &block_size,
                              &consumed, memory_limit, allow_prefix,
                              out_size - out_pos)) {
                if (getenv("XZ_DBG")) fprintf(stderr, "decode_block fail at %zu\n", pos);
                return 0;
            }
            pos += consumed;
            /* Block padding to a multiple of four. */
            while ((pos % 4u) != 0u) {
                if (pos >= in_size || in[pos] != 0u) {
                    free(block);
                    return 0;
                }
                ++pos;
            }
            if (pos + check_len > in_size) {
                free(block);
                return 0;
            }
            if (!verify_check(check_type, in + pos, block, block_size)) {
                free(block);
                return 0;
            }
            pos += check_len;
            want = block_size;
            if (allow_prefix && want > out_size - out_pos) {
                want = out_size - out_pos;
            }
            if (want > out_size - out_pos) {
                free(block);
                return 0;
            }
            if (want) memcpy(out + out_pos, block, want);
            out_pos += want;
            free(block);
            ++block_count;
            if (allow_prefix && out_pos == out_size) {
                if (out_written) *out_written = out_pos;
                return 1;
            }
        }
    }
    /* Index: 0x00, record count, records, padding, CRC32. */
    {
        size_t index_start = pos;
        uint64_t count;
        size_t used;
        uint64_t k;
        ++pos; /* index indicator 0x00 */
        if (!vli_decode(in + pos, in_size - pos, &count, &used)) return 0;
        pos += used;
        for (k = 0; k < count; ++k) {
            uint64_t v;
            if (!vli_decode(in + pos, in_size - pos, &v, &used)) return 0;
            pos += used;
            if (!vli_decode(in + pos, in_size - pos, &v, &used)) return 0;
            pos += used;
        }
        while ((pos - index_start) % 4u != 0u) {
            if (pos >= in_size || in[pos] != 0u) return 0;
            ++pos;
        }
        if (pos + 4u > in_size) return 0;
        {
            uint32_t crc = 0xFFFFFFFFu;
            size_t k2;
            for (k2 = index_start; k2 < pos; ++k2) {
                int j;
                crc ^= in[k2];
                for (j = 0; j < 8; ++j)
                    crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
            }
            crc ^= 0xFFFFFFFFu;
            if (crc != rd_u32_le(in + pos)) return 0;
        }
        pos += 4u;
    }
    /* Footer: CRC32, backward size, stream flags, "YZ". */
    if (pos + 12u != in_size) return 0;
    {
        uint32_t crc = 0xFFFFFFFFu;
        size_t k;
        for (k = pos + 4u; k < pos + 10u; ++k) {
            int j;
            crc ^= in[k];
            for (j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
        crc ^= 0xFFFFFFFFu;
        if (crc != rd_u32_le(in + pos)) return 0;
        if (in[pos + 10u] != 'Y' || in[pos + 11u] != 'Z') return 0;
    }
    if (allow_prefix && out_pos < out_size) return 0;
    if (out_written) *out_written = out_pos;
    return 1;
}
