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

int qw_compression_deflate_raw(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_capacity,
                               size_t *out_size)
{
    QwDeflateEncodeWriter writer;
    size_t *heads = NULL;
    size_t position = 0u;
    if (!out_size) return 0;
    *out_size = 0u;
    if ((!input && input_size) || !output) return 0;
    if (output_capacity < qw_compression_raw_bound(input_size)) return 0;

    memset(&writer, 0, sizeof(writer));
    writer.data = output;
    writer.capacity = output_capacity;

    if (input_size >= QW_DEFLATE_ENCODE_MIN_MATCH) {
        heads = (size_t *)malloc(QW_DEFLATE_ENCODE_HASH_SIZE * sizeof(size_t));
        if (!heads) {
            return qw_compression_deflate_raw_stored(input, input_size, output,
                                                     output_capacity, out_size);
        }
        for (position = 0u; position < QW_DEFLATE_ENCODE_HASH_SIZE; ++position)
            heads[position] = SIZE_MAX;
    }

    /* One final block using the fixed Huffman code. */
    writer_put_bits(&writer, 1u, 1u);
    writer_put_bits(&writer, 1u, 2u);

    position = 0u;
    while (position < input_size) {
        size_t length = 0u;
        size_t distance = 0u;
        if (heads && position + QW_DEFLATE_ENCODE_MIN_MATCH <= input_size) {
            uint32_t hash = hash_at(input, position);
            size_t candidate = heads[hash];
            if (candidate != SIZE_MAX && candidate < position &&
                position - candidate <= QW_DEFLATE_ENCODE_WINDOW) {
                size_t found = match_length(input, input_size, position,
                                            candidate);
                if (found >= QW_DEFLATE_ENCODE_MIN_MATCH) {
                    length = found;
                    distance = position - candidate;
                }
            }
            heads[hash] = position;
        }
        if (length) {
            size_t offset;
            emit_match(&writer, length, distance);
            for (offset = 1u; offset < length; ++offset) {
                if (position + offset + QW_DEFLATE_ENCODE_MIN_MATCH <=
                    input_size) {
                    heads[hash_at(input, position + offset)] = position + offset;
                }
            }
            position += length;
        } else {
            writer_put_literal(&writer, input[position]);
            ++position;
        }
        if (writer.failed) break;
    }
    free(heads);

    if (!writer.failed) writer_put_literal(&writer, 256u);
    if (!writer_flush(&writer) || writer.failed) {
        return qw_compression_deflate_raw_stored(input, input_size, output,
                                                 output_capacity, out_size);
    }

    /*
     * Stored blocks are cheaper whenever the fixed-code stream is not smaller
     * than the input: the stored encoder always emits exactly its bound, so
     * compare against that without writing over the compressed bytes first.
     */
    {
        size_t stored_size = qw_compression_raw_stored_bound(input_size);
        if (stored_size != SIZE_MAX && writer.size < stored_size) {
            *out_size = writer.size;
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
