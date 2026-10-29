#include <quirewise/compression.h>

#include <lzma.h>

int qw_compression_inflate_xz(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t memory_limit)
{
    size_t input_position = 0u;
    size_t output_position = 0u;
    uint64_t decoder_memory;
    lzma_ret result;

    if (!input || !input_size || !output || !output_size || !memory_limit) {
        return 0;
    }
    decoder_memory = (uint64_t)memory_limit;
    result = lzma_stream_buffer_decode(&decoder_memory, 0u, NULL,
                                       input, &input_position, input_size,
                                       output, &output_position, output_size);
    return result == LZMA_OK && input_position == input_size &&
           output_position == output_size;
}

int qw_compression_inflate_xz_prefix(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t memory_limit)
{
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret result;
    int success;

    if (!input || !input_size || !output || !output_size || !memory_limit) {
        return 0;
    }
    result = lzma_stream_decoder(&stream, (uint64_t)memory_limit, 0u);
    if (result != LZMA_OK) return 0;

    stream.next_in = input;
    stream.avail_in = input_size;
    stream.next_out = output;
    stream.avail_out = output_size;
    for (;;) {
        result = lzma_code(&stream, LZMA_FINISH);
        if (stream.avail_out == 0u) break;
        if (result != LZMA_OK) break;
    }
    success = stream.avail_out == 0u;
    lzma_end(&stream);
    return success;
}
