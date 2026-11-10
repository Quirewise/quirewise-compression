#include <quirewise/compression.h>

#include "internal/xz_dec.h"

static int inflate_xz(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_size,
                      size_t memory_limit, int allow_prefix)
{
    size_t written = 0u;
    if (!input || !input_size || !memory_limit) return 0;
    if (!output && output_size != 0u) return 0;
    if (!qw_xz_decode(input, input_size, output, output_size, memory_limit,
                      allow_prefix, &written))
        return 0;
    return allow_prefix ? 1 : written == output_size;
}

int qw_compression_inflate_xz(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t memory_limit)
{
    return inflate_xz(input, input_size, output, output_size, memory_limit, 0);
}

int qw_compression_inflate_xz_prefix(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t memory_limit)
{
    return inflate_xz(input, input_size, output, output_size, memory_limit, 1);
}
