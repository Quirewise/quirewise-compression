#include <quirewise/compression.h>

#include <stdlib.h>
#include <string.h>

/*
 * Fixed-Huffman DEFLATE encoder (RFC 1951 section 3.2.6).
 *
 * The encoder is deliberately small: a single-hash LZ77 pass feeds the fixed
 * Huffman tables, and the result falls back to stored blocks whenever the
 * compressed stream would not be smaller than the input. That keeps every
 * output stream valid for any inflater while removing the size penalty of the
 * stored-only encoder for the repetitive text and image streams the rest of
 * Quirewise produces.
 */

enum {
    QW_DEFLATE_ENCODE_WINDOW = 32768,
    QW_DEFLATE_ENCODE_MIN_MATCH = 3,
    QW_DEFLATE_ENCODE_MAX_MATCH = 258,
    QW_DEFLATE_ENCODE_HASH_BITS = 15,
    QW_DEFLATE_ENCODE_HASH_SIZE = 1 << QW_DEFLATE_ENCODE_HASH_BITS
};

typedef struct QwDeflateEncodeWriter {
    uint8_t *data;
    size_t capacity;
    size_t size;
    uint32_t bits;
    unsigned bit_count;
    int failed;
} QwDeflateEncodeWriter;

static const uint16_t qw_deflate_length_base[29] = {
    3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 13u, 15u, 17u, 19u, 23u, 27u,
    31u, 35u, 43u, 51u, 59u, 67u, 83u, 99u, 115u, 131u, 163u, 195u, 227u,
    258u
};

static const uint8_t qw_deflate_length_extra[29] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u,
    2u, 3u, 3u, 3u, 3u, 4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 0u
};

static const uint16_t qw_deflate_distance_base[30] = {
    1u, 2u, 3u, 4u, 5u, 7u, 9u, 13u, 17u, 25u, 33u, 49u, 65u, 97u, 129u,
    193u, 257u, 385u, 513u, 769u, 1025u, 1537u, 2049u, 3073u, 4097u, 6145u,
    8193u, 12289u, 16385u, 24577u
};

static const uint8_t qw_deflate_distance_extra[30] = {
    0u, 0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u, 5u, 5u, 6u,
    6u, 7u, 7u, 8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 13u, 13u
};

static uint32_t reverse_bits(uint32_t code, unsigned length)
{
    uint32_t reversed = 0u;
    unsigned i;
    for (i = 0u; i < length; ++i) {
        reversed = (reversed << 1) | (code & 1u);
        code >>= 1;
    }
    return reversed;
}

static void writer_put_bits(QwDeflateEncodeWriter *writer, uint32_t value,
                            unsigned count)
{
    if (writer->failed || count > 24u) {
        writer->failed = 1;
        return;
    }
    writer->bits |= (value & ((1u << count) - 1u)) << writer->bit_count;
    writer->bit_count += count;
    while (writer->bit_count >= 8u) {
        if (writer->size >= writer->capacity) {
            writer->failed = 1;
            return;
        }
        writer->data[writer->size++] = (uint8_t)(writer->bits & 0xffu);
        writer->bits >>= 8u;
        writer->bit_count -= 8u;
    }
}

static void writer_put_huffman(QwDeflateEncodeWriter *writer,
                               uint32_t code, unsigned length)
{
    writer_put_bits(writer, reverse_bits(code, length), length);
}

static void writer_put_literal(QwDeflateEncodeWriter *writer,
                               unsigned symbol)
{
    unsigned code;
    unsigned length;
    if (symbol <= 143u) {
        code = 0x30u + symbol;
        length = 8u;
    } else if (symbol <= 255u) {
        code = 0x190u + (symbol - 144u);
        length = 9u;
    } else if (symbol <= 279u) {
        code = symbol - 256u;
        length = 7u;
    } else {
        code = 0xc0u + (symbol - 280u);
        length = 8u;
    }
    writer_put_huffman(writer, code, length);
}

static int writer_flush(QwDeflateEncodeWriter *writer)
{
    if (writer->failed) return 0;
    if (writer->bit_count) {
        if (writer->size >= writer->capacity) return 0;
        writer->data[writer->size++] = (uint8_t)(writer->bits & 0xffu);
        writer->bits = 0u;
        writer->bit_count = 0u;
    }
    return 1;
}

static uint32_t hash_at(const uint8_t *data, size_t position)
{
    uint32_t value = ((uint32_t)data[position] << 10u) ^
                     ((uint32_t)data[position + 1u] << 5u) ^
                     (uint32_t)data[position + 2u];
    return value & (QW_DEFLATE_ENCODE_HASH_SIZE - 1u);
}

static size_t match_length(const uint8_t *data, size_t size, size_t position,
                           size_t candidate)
{
    size_t maximum = size - position;
    size_t length = 0u;
    if (maximum > QW_DEFLATE_ENCODE_MAX_MATCH)
        maximum = QW_DEFLATE_ENCODE_MAX_MATCH;
    while (length < maximum &&
           data[candidate + length] == data[position + length]) {
        ++length;
    }
    return length;
}

static void emit_match(QwDeflateEncodeWriter *writer, size_t length,
                       size_t distance)
{
    size_t index;
    for (index = 0u; index < 29u; ++index) {
        size_t span = (size_t)1u << qw_deflate_length_extra[index];
        if (length >= qw_deflate_length_base[index] &&
            length <= (size_t)qw_deflate_length_base[index] + span - 1u) {
            break;
        }
    }
    if (index == 29u) index = 28u;
    writer_put_literal(writer, (unsigned)(257u + index));
    if (qw_deflate_length_extra[index]) {
        writer_put_bits(writer, (uint32_t)(length - qw_deflate_length_base[index]),
                        qw_deflate_length_extra[index]);
    }
    for (index = 0u; index < 30u; ++index) {
        size_t span = (size_t)1u << qw_deflate_distance_extra[index];
        if (distance >= qw_deflate_distance_base[index] &&
            distance <= (size_t)qw_deflate_distance_base[index] + span - 1u) {
            break;
        }
    }
    if (index == 30u) index = 29u;
    writer_put_huffman(writer, (uint32_t)index, 5u);
    if (qw_deflate_distance_extra[index]) {
        writer_put_bits(writer,
                        (uint32_t)(distance - qw_deflate_distance_base[index]),
                        qw_deflate_distance_extra[index]);
    }
}

size_t qw_compression_raw_bound(size_t input_size)
{
    size_t stored = qw_compression_raw_stored_bound(input_size);
    size_t fixed;
    if (input_size > SIZE_MAX - 64u) return SIZE_MAX;
    if (input_size / 8u > SIZE_MAX - 64u - input_size) return SIZE_MAX;
    fixed = input_size + input_size / 8u + 64u;
    return fixed > stored ? fixed : stored;
}

size_t qw_compression_zlib_bound(size_t input_size)
{
    size_t raw = qw_compression_raw_bound(input_size);
    if (raw == SIZE_MAX || raw > SIZE_MAX - 6u) return SIZE_MAX;
    return raw + 6u;
}

typedef struct QwDeflateToken {
    uint16_t literal; /* literal byte, or 256 for end of block */
    uint16_t length;  /* match length, 0 for literals */
    uint16_t distance;
} QwDeflateToken;

typedef struct QwDeflateTokens {
    QwDeflateToken *items;
    size_t count;
    size_t capacity;
    int failed;
} QwDeflateTokens;

static int deflate_token_push(QwDeflateTokens *tokens, uint16_t literal,
                              uint16_t length, uint16_t distance)
{
    if (tokens->count == tokens->capacity) {
        size_t grown = tokens->capacity ? tokens->capacity * 2u : 1024u;
        QwDeflateToken *resized = (QwDeflateToken *)realloc(
            tokens->items, grown * sizeof(*resized));
        if (!resized) {
            tokens->failed = 1;
            return 0;
        }
        tokens->items = resized;
        tokens->capacity = grown;
    }
    tokens->items[tokens->count].literal = literal;
    tokens->items[tokens->count].length = length;
    tokens->items[tokens->count].distance = distance;
    ++tokens->count;
    return 1;
}

/* Huffman code lengths from frequencies; returns the maximum depth (0 when
 * fewer than two symbols are used, so no tree can be built). */
static unsigned deflate_build_lengths(const uint32_t *freq, size_t count,
                                      uint8_t *lengths, unsigned max_bits)
{
    uint32_t weight[2u * 286u];
    int parent[2u * 286u];
    size_t leaf[286];
    size_t active[286];
    size_t active_count = 0u;
    size_t nodes = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        lengths[index] = 0u;
        if (!freq[index]) continue;
        weight[nodes] = freq[index];
        parent[nodes] = -1;
        leaf[index] = nodes;
        active[active_count++] = nodes;
        ++nodes;
    }
    if (active_count < 2u) return 0u;
    while (active_count > 1u) {
        size_t first = 0u;
        size_t second = 1u;
        size_t scan;
        if (weight[active[second]] < weight[active[first]]) {
            size_t swap = first;
            first = second;
            second = swap;
        }
        for (scan = 2u; scan < active_count; ++scan) {
            if (weight[active[scan]] < weight[active[first]]) {
                second = first;
                first = scan;
            } else if (weight[active[scan]] < weight[active[second]]) {
                second = scan;
            }
        }
        weight[nodes] = weight[active[first]] + weight[active[second]];
        parent[active[first]] = (int)nodes;
        parent[active[second]] = (int)nodes;
        parent[nodes] = -1;
        active[first] = nodes;
        active[second] = active[active_count - 1u];
        --active_count;
        ++nodes;
    }
    {
        unsigned maximum = 0u;
        for (index = 0u; index < count; ++index) {
            unsigned depth = 0u;
            int node;
            if (!freq[index]) continue;
            node = (int)leaf[index];
            while (parent[node] >= 0) {
                node = parent[node];
                ++depth;
            }
            lengths[index] = (uint8_t)depth;
            if (depth > maximum) maximum = depth;
        }
        return maximum > max_bits ? maximum : 1u;
    }
}

static void deflate_assign_codes(const uint8_t *lengths, size_t count,
                                 uint16_t *codes)
{
    unsigned bl_count[16] = {0u};
    unsigned next_code[16] = {0u};
    uint32_t code = 0u;
    size_t index;
    unsigned bits;
    for (index = 0u; index < count; ++index) ++bl_count[lengths[index]];
    bl_count[0] = 0u;
    for (bits = 1u; bits <= 15u; ++bits) {
        code = (code + bl_count[bits - 1u]) << 1u;
        next_code[bits] = code;
    }
    for (index = 0u; index < count; ++index) {
        if (lengths[index])
            codes[index] = (uint16_t)(next_code[lengths[index]]++);
        else
            codes[index] = 0u;
    }
}

static unsigned deflate_length_symbol(size_t length)
{
    unsigned index;
    for (index = 0u; index < 29u; ++index) {
        size_t span = (size_t)1u << qw_deflate_length_extra[index];
        if (length >= qw_deflate_length_base[index] &&
            length <= (size_t)qw_deflate_length_base[index] + span - 1u)
            return 257u + index;
    }
    return 285u;
}

static unsigned deflate_distance_symbol(size_t distance)
{
    unsigned index;
    for (index = 0u; index < 30u; ++index) {
        size_t span = (size_t)1u << qw_deflate_distance_extra[index];
        if (distance >= qw_deflate_distance_base[index] &&
            distance <= (size_t)qw_deflate_distance_base[index] + span - 1u)
            return index;
    }
    return 29u;
}

static size_t deflate_rle_lengths(const uint8_t *lengths, size_t total,
                                  uint8_t *symbols, uint8_t *extras)
{
    size_t index = 0u;
    size_t count = 0u;
    while (index < total) {
        unsigned value = lengths[index];
        size_t run = 1u;
        size_t left;
        while (index + run < total && lengths[index + run] == value) ++run;
        index += run;
        left = run;
        if (value == 0u) {
            while (left >= 11u) {
                size_t take = left > 138u ? 138u : left;
                symbols[count] = 18u;
                extras[count] = (uint8_t)(take - 11u);
                ++count;
                left -= take;
            }
            if (left >= 3u) {
                symbols[count] = 17u;
                extras[count] = (uint8_t)(left - 3u);
                ++count;
                left = 0u;
            }
            while (left > 0u) {
                symbols[count] = 0u;
                extras[count] = 0u;
                ++count;
                --left;
            }
        } else {
            symbols[count] = (uint8_t)value;
            extras[count] = 0u;
            ++count;
            --left;
            while (left >= 3u) {
                size_t take = left > 6u ? 6u : left;
                symbols[count] = 16u;
                extras[count] = (uint8_t)(take - 3u);
                ++count;
                left -= take;
            }
            while (left > 0u) {
                symbols[count] = (uint8_t)value;
                extras[count] = 0u;
                ++count;
                --left;
            }
        }
    }
    return count;
}

static int deflate_emit_fixed(QwDeflateEncodeWriter *writer,
                              const QwDeflateToken *tokens, size_t count)
{
    size_t index;
    writer_put_bits(writer, 1u, 1u);
    writer_put_bits(writer, 1u, 2u);
    for (index = 0u; index < count; ++index) {
        const QwDeflateToken *token = &tokens[index];
        if (token->length == 0u)
            writer_put_literal(writer, token->literal);
        else
            emit_match(writer, token->length, token->distance);
    }
    writer_put_literal(writer, 256u);
    return writer->failed ? 0 : 1;
}

static int deflate_emit_dynamic(QwDeflateEncodeWriter *writer,
                                const QwDeflateToken *tokens, size_t count,
                                const uint8_t *lit_len,
                                const uint16_t *lit_code,
                                const uint8_t *dist_len,
                                const uint16_t *dist_code, size_t hlit,
                                size_t hdist)
{
    static const uint8_t order[19] = {16u, 17u, 18u, 0u, 8u,  7u,  9u, 6u,
                                      10u, 5u,  11u, 4u, 12u, 3u,  13u, 2u,
                                      14u, 1u,  15u};
    uint8_t combined[286 + 30];
    uint8_t symbols[320];
    uint8_t extras[320];
    uint32_t cl_freq[19] = {0u};
    uint8_t cl_len[19];
    uint16_t cl_code[19];
    unsigned cl_max;
    size_t rle_count;
    size_t hclen;
    size_t index;
    size_t i;
    for (i = 0u; i < hlit; ++i) combined[i] = lit_len[i];
    for (i = 0u; i < hdist; ++i) combined[hlit + i] = dist_len[i];
    rle_count = deflate_rle_lengths(combined, hlit + hdist, symbols, extras);
    for (i = 0u; i < rle_count; ++i) ++cl_freq[symbols[i]];
    cl_max = deflate_build_lengths(cl_freq, 19u, cl_len, 7u);
    if (cl_max == 0u || cl_max > 7u) return 0;
    deflate_assign_codes(cl_len, 19u, cl_code);
    hclen = 19u;
    while (hclen > 4u && cl_len[order[hclen - 1u]] == 0u) --hclen;
    writer_put_bits(writer, 1u, 1u);
    writer_put_bits(writer, 2u, 2u);
    writer_put_bits(writer, (uint32_t)(hlit - 257u), 5u);
    writer_put_bits(writer, (uint32_t)(hdist - 1u), 5u);
    writer_put_bits(writer, (uint32_t)(hclen - 4u), 4u);
    for (i = 0u; i < hclen; ++i)
        writer_put_bits(writer, cl_len[order[i]], 3u);
    for (i = 0u; i < rle_count; ++i) {
        unsigned symbol = symbols[i];
        if (!cl_len[symbol]) return 0;
        writer_put_huffman(writer, cl_code[symbol], cl_len[symbol]);
        if (symbol == 16u)
            writer_put_bits(writer, extras[i], 2u);
        else if (symbol == 17u)
            writer_put_bits(writer, extras[i], 3u);
        else if (symbol == 18u)
            writer_put_bits(writer, extras[i], 7u);
    }
    for (index = 0u; index < count; ++index) {
        const QwDeflateToken *token = &tokens[index];
        if (token->length == 0u) {
            unsigned symbol = token->literal;
            if (!lit_len[symbol]) return 0;
            writer_put_huffman(writer, lit_code[symbol], lit_len[symbol]);
        } else {
            unsigned length_symbol = deflate_length_symbol(token->length);
            unsigned distance_symbol =
                deflate_distance_symbol(token->distance);
            if (!lit_len[length_symbol] || !dist_len[distance_symbol])
                return 0;
            writer_put_huffman(writer, lit_code[length_symbol],
                               lit_len[length_symbol]);
            if (qw_deflate_length_extra[length_symbol - 257u])
                writer_put_bits(
                    writer,
                    (uint32_t)(token->length -
                               qw_deflate_length_base[length_symbol - 257u]),
                    qw_deflate_length_extra[length_symbol - 257u]);
            writer_put_huffman(writer, dist_code[distance_symbol],
                               dist_len[distance_symbol]);
            if (qw_deflate_distance_extra[distance_symbol])
                writer_put_bits(
                    writer,
                    (uint32_t)(token->distance -
                               qw_deflate_distance_base[distance_symbol]),
                    qw_deflate_distance_extra[distance_symbol]);
        }
        if (writer->failed) return 0;
    }
    if (!lit_len[256]) return 0;
    writer_put_huffman(writer, lit_code[256], lit_len[256]);
    return writer->failed ? 0 : 1;
}

static int deflate_collect_tokens(const uint8_t *input, size_t input_size,
                                  size_t *heads, QwDeflateTokens *tokens)
{
    size_t position = 0u;
    while (position < input_size) {
        size_t length = 0u;
        size_t distance = 0u;
        if (heads && position + QW_DEFLATE_ENCODE_MIN_MATCH <= input_size) {
            uint32_t hash = hash_at(input, position);
            size_t candidate = heads[hash];
            if (candidate != SIZE_MAX && candidate < position &&
                position - candidate <= QW_DEFLATE_ENCODE_WINDOW) {
                size_t found =
                    match_length(input, input_size, position, candidate);
                if (found >= QW_DEFLATE_ENCODE_MIN_MATCH) {
                    length = found;
                    distance = position - candidate;
                }
            }
            heads[hash] = position;
        }
        if (length) {
            size_t offset;
            if (!deflate_token_push(tokens, 0u, (uint16_t)length,
                                    (uint16_t)distance))
                return 0;
            for (offset = 1u; offset < length; ++offset) {
                if (position + offset + QW_DEFLATE_ENCODE_MIN_MATCH <=
                    input_size) {
                    heads[hash_at(input, position + offset)] = position + offset;
                }
            }
            position += length;
        } else {
            if (!deflate_token_push(tokens, input[position], 0u, 0u)) return 0;
            ++position;
        }
    }
    return !tokens->failed;
}

int qw_compression_deflate_raw(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_capacity,
                               size_t *out_size)
{
    QwDeflateEncodeWriter writer;
    QwDeflateEncodeWriter initial;
    QwDeflateTokens tokens;
    size_t *heads = NULL;
    uint32_t lit_freq[286];
    uint32_t dist_freq[30];
    uint8_t lit_len[286];
    uint16_t lit_code[286];
    uint8_t dist_len[30];
    uint16_t dist_code[30];
    size_t hlit = 286u;
    size_t hdist = 30u;
    size_t index;
    unsigned lit_max;
    unsigned dist_max;
    size_t fixed_size = SIZE_MAX;
    size_t dynamic_size = SIZE_MAX;
    size_t chosen_size;
    int dynamic_ok = 0;

    if (!out_size) return 0;
    *out_size = 0u;
    if ((!input && input_size) || !output) return 0;
    if (output_capacity < qw_compression_raw_bound(input_size)) return 0;

    memset(&writer, 0, sizeof(writer));
    writer.data = output;
    writer.capacity = output_capacity;
    initial = writer;
    memset(&tokens, 0, sizeof(tokens));
    memset(lit_freq, 0, sizeof(lit_freq));
    memset(dist_freq, 0, sizeof(dist_freq));

    if (input_size >= QW_DEFLATE_ENCODE_MIN_MATCH) {
        heads = (size_t *)malloc(QW_DEFLATE_ENCODE_HASH_SIZE * sizeof(size_t));
        if (heads) {
            for (index = 0u; index < QW_DEFLATE_ENCODE_HASH_SIZE; ++index)
                heads[index] = SIZE_MAX;
        }
    }
    if (!deflate_collect_tokens(input, input_size, heads, &tokens)) {
        free(heads);
        free(tokens.items);
        return qw_compression_deflate_raw_stored(input, input_size, output,
                                                 output_capacity, out_size);
    }
    free(heads);

    for (index = 0u; index < tokens.count; ++index) {
        const QwDeflateToken *token = &tokens.items[index];
        if (token->length == 0u) {
            ++lit_freq[token->literal];
        } else {
            ++lit_freq[deflate_length_symbol(token->length)];
            ++dist_freq[deflate_distance_symbol(token->distance)];
        }
    }
    ++lit_freq[256];
    while (hlit > 257u && !lit_freq[hlit - 1u]) --hlit;
    while (hdist > 1u && !dist_freq[hdist - 1u]) --hdist;

    lit_max = deflate_build_lengths(lit_freq, hlit, lit_len, 15u);
    dist_max = deflate_build_lengths(dist_freq, hdist, dist_len, 15u);
    if (lit_max > 0u && lit_max <= 15u && dist_max > 0u && dist_max <= 15u) {
        deflate_assign_codes(lit_len, hlit, lit_code);
        deflate_assign_codes(dist_len, hdist, dist_code);
        dynamic_ok = 1;
    }

    if (deflate_emit_fixed(&writer, tokens.items, tokens.count)) {
        if (writer_flush(&writer)) fixed_size = writer.size;
    }
    writer = initial;
    if (dynamic_ok &&
        deflate_emit_dynamic(&writer, tokens.items, tokens.count, lit_len,
                             lit_code, dist_len, dist_code, hlit, hdist) &&
        writer_flush(&writer)) {
        dynamic_size = writer.size;
    }
    writer = initial;

    if (dynamic_size < fixed_size) {
        chosen_size = dynamic_size;
        if (!deflate_emit_dynamic(&writer, tokens.items, tokens.count, lit_len,
                                  lit_code, dist_len, dist_code, hlit,
                                  hdist) ||
            !writer_flush(&writer)) {
            free(tokens.items);
            return qw_compression_deflate_raw_stored(
                input, input_size, output, output_capacity, out_size);
        }
    } else {
        chosen_size = fixed_size;
        if (chosen_size == SIZE_MAX ||
            !deflate_emit_fixed(&writer, tokens.items, tokens.count) ||
            !writer_flush(&writer)) {
            free(tokens.items);
            return qw_compression_deflate_raw_stored(
                input, input_size, output, output_capacity, out_size);
        }
    }
    free(tokens.items);

    {
        size_t stored_size = qw_compression_raw_stored_bound(input_size);
        if (stored_size != SIZE_MAX && chosen_size < stored_size) {
            *out_size = chosen_size;
            return 1;
        }
    }
    return qw_compression_deflate_raw_stored(input, input_size, output,
                                             output_capacity, out_size);
}

int qw_compression_deflate_zlib(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_capacity,
                                size_t *out_size)
{
    size_t raw_size = 0u;
    uint32_t adler;
    if (!out_size) return 0;
    *out_size = 0u;
    if ((!input && input_size) || !output) return 0;
    if (output_capacity < qw_compression_zlib_bound(input_size)) return 0;
    /* 0x78 0x9C: deflate, 32 KiB window, default level, no preset dictionary. */
    output[0] = 0x78u;
    output[1] = 0x9cu;
    if (!qw_compression_deflate_raw(input, input_size, output + 2u,
                                    output_capacity - 6u, &raw_size)) {
        return 0;
    }
    adler = qw_compression_adler32(input, input_size);
    output[2u + raw_size] = (uint8_t)(adler >> 24);
    output[3u + raw_size] = (uint8_t)(adler >> 16);
    output[4u + raw_size] = (uint8_t)(adler >> 8);
    output[5u + raw_size] = (uint8_t)adler;
    *out_size = raw_size + 6u;
    return 1;
}
