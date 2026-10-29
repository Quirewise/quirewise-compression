#include <quirewise/compression.h>

#include <lzma.h>
#include <limits.h>
#include <string.h>

static int map_7z_filter(uint32_t method, lzma_vli *out_filter)
{
    switch (method) {
    case QW_COMPRESSION_7Z_DELTA_METHOD:
        *out_filter = LZMA_FILTER_DELTA;
        return 1;
    case QW_COMPRESSION_7Z_LZMA1_METHOD:
        *out_filter = LZMA_FILTER_LZMA1;
        return 1;
    case QW_COMPRESSION_7Z_LZMA2_METHOD:
        *out_filter = LZMA_FILTER_LZMA2;
        return 1;
    case QW_COMPRESSION_7Z_X86_METHOD:
        *out_filter = LZMA_FILTER_X86;
        return 1;
    case QW_COMPRESSION_7Z_POWERPC_METHOD:
        *out_filter = LZMA_FILTER_POWERPC;
        return 1;
    case QW_COMPRESSION_7Z_IA64_METHOD:
        *out_filter = LZMA_FILTER_IA64;
        return 1;
    case QW_COMPRESSION_7Z_ARM_METHOD:
        *out_filter = LZMA_FILTER_ARM;
        return 1;
    case QW_COMPRESSION_7Z_ARMTHUMB_METHOD:
        *out_filter = LZMA_FILTER_ARMTHUMB;
        return 1;
    case QW_COMPRESSION_7Z_SPARC_METHOD:
        *out_filter = LZMA_FILTER_SPARC;
        return 1;
    default:
        return 0;
    }
}

int qw_compression_inflate_7z_chain(
    const QwCompression7zCoder *coders, size_t coder_count,
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size, size_t memory_limit)
{
    lzma_filter filters[QW_COMPRESSION_7Z_MAX_CODERS + 1u];
    size_t filter_count = 0u;
    size_t input_position = 0u;
    size_t output_position = 0u;
    size_t active_coder_count = 0u;
    size_t deflate_count = 0u;
    size_t deflate_index = SIZE_MAX;
    uint64_t decoder_memory;
    lzma_ret result;
    const uint8_t no_properties = 0u;

    if (!coders || !coder_count ||
        coder_count > QW_COMPRESSION_7Z_MAX_CODERS ||
        !input || !input_size || !output || !output_size || !memory_limit) {
        return 0;
    }
    for (size_t i = 0u; i < QW_COMPRESSION_7Z_MAX_CODERS + 1u; ++i) {
        filters[i].id = LZMA_VLI_UNKNOWN;
        filters[i].options = NULL;
    }
    for (size_t i = 0u; i < coder_count; ++i) {
        if (coders[i].method == QW_COMPRESSION_7Z_COPY_METHOD) {
            if (coders[i].properties_size) return 0;
            continue;
        }
        ++active_coder_count;
        if (coders[i].method == QW_COMPRESSION_7Z_DEFLATE_METHOD) {
            ++deflate_count;
            deflate_index = i;
        }
    }
    if (deflate_count) {
        if (deflate_count != 1u || active_coder_count != 1u ||
            coders[deflate_index].properties_size != 0u) {
            return 0;
        }
        return qw_compression_inflate_raw(input, input_size,
                                          output, output_size);
    }

    for (size_t i = coder_count; i > 0u; --i) {
        const QwCompression7zCoder *coder = &coders[i - 1u];
        lzma_vli filter_id;
        const uint8_t *properties = coder->properties
            ? coder->properties : &no_properties;
        if (coder->method == QW_COMPRESSION_7Z_COPY_METHOD) continue;
        if (filter_count >= QW_COMPRESSION_7Z_MAX_CODERS ||
            !map_7z_filter(coder->method, &filter_id) ||
            coder->properties_size > UINT32_MAX ||
            (coder->properties_size && !coder->properties)) {
            goto error;
        }
        filters[filter_count].id = filter_id;
        filters[filter_count].options = NULL;
        result = lzma_properties_decode(
            &filters[filter_count], NULL, properties,
            (uint32_t)coder->properties_size);
        if (result != LZMA_OK) goto error;
        ++filter_count;
    }

    if (!filter_count) {
        if (input_size != output_size) return 0;
        memcpy(output, input, output_size);
        return 1;
    }
    filters[filter_count].id = LZMA_VLI_UNKNOWN;
    filters[filter_count].options = NULL;
    decoder_memory = lzma_raw_decoder_memusage(filters);
    if (decoder_memory == UINT64_MAX ||
        decoder_memory > (uint64_t)memory_limit) {
        goto error;
    }
    result = lzma_raw_buffer_decode(filters, NULL, input, &input_position,
                                    input_size, output, &output_position,
                                    output_size);
    lzma_filters_free(filters, NULL);
    return result == LZMA_OK && input_position == input_size &&
           output_position == output_size;

error:
    lzma_filters_free(filters, NULL);
    return 0;
}
