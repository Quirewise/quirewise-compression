#include <quirewise/compression.h>

#include "internal/lzma_sdk_allocator.h"

#include "Xz.h"

static int inflate_xz(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_size,
                      size_t memory_limit, int allow_prefix)
{
    QwLzmaSdkAllocator allocator;
    CXzUnpacker unpacker;
    size_t input_position = 0u;
    size_t output_position = 0u;
    int success = 0;

    if (!input || !input_size || !memory_limit)
        return 0;
    /* A valid stream may legitimately decode to zero bytes (for example an XZ
     * container with no blocks), so a zero-length output is allowed when no
     * buffer is supplied. */
    if (!output && output_size != 0u)
        return 0;
    qw_lzma_sdk_initialize();
    qw_lzma_sdk_allocator_init(&allocator, memory_limit);
    XzUnpacker_Construct(&unpacker, &allocator.interface);
    for (;;) {
        SizeT source_size = input_size - input_position;
        SizeT destination_size = output_size - output_position;
        ECoderStatus status = CODER_STATUS_NOT_SPECIFIED;
        uint8_t zero_sink = 0u;
        uint8_t *destination = output ? output + output_position : &zero_sink;
        SRes result = XzUnpacker_Code(
            &unpacker, destination, &destination_size,
            input + input_position, &source_size, True,
            allow_prefix ? CODER_FINISH_ANY : CODER_FINISH_END, &status);
        int made_progress;

        input_position += source_size;
        output_position += destination_size;
        made_progress = source_size != 0u || destination_size != 0u;
        if (allow_prefix && output_position == output_size) {
            success = 1;
            break;
        }
        if (result != SZ_OK) break;
        if (!allow_prefix && input_position == input_size &&
                   output_position == output_size &&
                   unpacker.numFinishedStreams == 1u &&
                   XzUnpacker_IsStreamWasFinished(&unpacker)) {
            success = 1;
            break;
        }
        if (!made_progress ||
            (status == CODER_STATUS_NEEDS_MORE_INPUT &&
             input_position == input_size)) break;
    }
    XzUnpacker_Free(&unpacker);
    return success && allocator.bytes_used == 0u;
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
