#include <quirewise/compression.h>

#include "internal/lzma_sdk_allocator.h"

#include "Bra.h"
#include "Delta.h"
#include "Lzma2Dec.h"
#include "LzmaDec.h"

#include <limits.h>
#include <string.h>

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int inflate_lzma1(const QwCompression7zCoder *coder,
                         const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t output_size,
                         size_t memory_limit)
{
    QwLzmaSdkAllocator allocator;
    CLzmaDec decoder;
    SizeT source_size = input_size;
    SizeT destination_size = output_size;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    SRes result;
    int success;

    if (!coder->properties || coder->properties_size != LZMA_PROPS_SIZE ||
        coder->properties_size > UINT_MAX) return 0;
    qw_lzma_sdk_allocator_init(&allocator, memory_limit);
    LzmaDec_Construct(&decoder);
    result = LzmaDec_Allocate(&decoder, coder->properties,
                              (unsigned)coder->properties_size,
                              &allocator.interface);
    if (result != SZ_OK) {
        LzmaDec_Free(&decoder, &allocator.interface);
        return 0;
    }
    LzmaDec_Init(&decoder);
    result = LzmaDec_DecodeToBuf(&decoder, output, &destination_size,
                                 input, &source_size, LZMA_FINISH_END,
                                 &status);
    success = result == SZ_OK && source_size == input_size &&
              destination_size == output_size &&
              (status == LZMA_STATUS_FINISHED_WITH_MARK ||
               status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK);
    LzmaDec_Free(&decoder, &allocator.interface);
    return success && allocator.bytes_used == 0u;
}

static int inflate_lzma2(const QwCompression7zCoder *coder,
                         const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t output_size,
                         size_t memory_limit)
{
    QwLzmaSdkAllocator allocator;
    CLzma2Dec decoder;
    SizeT source_size = input_size;
    SizeT destination_size = output_size;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    SRes result;
    int success;

    if (!coder->properties || coder->properties_size != 1u) return 0;
    qw_lzma_sdk_allocator_init(&allocator, memory_limit);
    Lzma2Dec_Construct(&decoder);
    result = Lzma2Dec_Allocate(&decoder, coder->properties[0],
                               &allocator.interface);
    if (result != SZ_OK) {
        Lzma2Dec_Free(&decoder, &allocator.interface);
        return 0;
    }
    Lzma2Dec_Init(&decoder);
    result = Lzma2Dec_DecodeToBuf(&decoder, output, &destination_size,
                                  input, &source_size, LZMA_FINISH_END,
                                  &status);
    success = result == SZ_OK && source_size == input_size &&
              destination_size == output_size &&
              status == LZMA_STATUS_FINISHED_WITH_MARK;
    Lzma2Dec_Free(&decoder, &allocator.interface);
    return success && allocator.bytes_used == 0u;
}

static int apply_7z_filter(const QwCompression7zCoder *coder,
                           uint8_t *output, size_t output_size)
{
    uint32_t pc = 0u;
    if (coder->method == QW_COMPRESSION_7Z_COPY_METHOD)
        return coder->properties_size == 0u;
    if (coder->method == QW_COMPRESSION_7Z_DELTA_METHOD) {
        Byte state[DELTA_STATE_SIZE] = {0};
        if (!coder->properties || coder->properties_size != 1u) return 0;
        Delta_Init(state);
        Delta_Decode(state, (unsigned)coder->properties[0] + 1u,
                     output, output_size);
        return 1;
    }
    if (coder->properties_size == 4u) {
        if (!coder->properties) return 0;
        pc = read_u32_le(coder->properties);
    } else if (coder->properties_size != 0u) {
        return 0;
    }
    switch (coder->method) {
    case QW_COMPRESSION_7Z_X86_METHOD: {
        UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
        (void)z7_BranchConvSt_X86_Dec(output, output_size, pc, &state);
        return 1;
    }
    case QW_COMPRESSION_7Z_POWERPC_METHOD:
        (void)z7_BranchConv_PPC_Dec(output, output_size, pc);
        return 1;
    case QW_COMPRESSION_7Z_IA64_METHOD:
        (void)z7_BranchConv_IA64_Dec(output, output_size, pc);
        return 1;
    case QW_COMPRESSION_7Z_ARM_METHOD:
        (void)z7_BranchConv_ARM_Dec(output, output_size, pc);
        return 1;
    case QW_COMPRESSION_7Z_ARMTHUMB_METHOD:
        (void)z7_BranchConv_ARMT_Dec(output, output_size, pc);
        return 1;
    case QW_COMPRESSION_7Z_SPARC_METHOD:
        (void)z7_BranchConv_SPARC_Dec(output, output_size, pc);
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
    size_t active_coder_count = 0u;
    size_t deflate_count = 0u;
    size_t deflate_index = SIZE_MAX;
    size_t lzma1_count = 0u;
    size_t lzma2_count = 0u;
    size_t codec_index = SIZE_MAX;

    if (!coders || !coder_count ||
        coder_count > QW_COMPRESSION_7Z_MAX_CODERS ||
        !input || !input_size || !output || !output_size || !memory_limit) {
        return 0;
    }
    for (size_t i = 0u; i < coder_count; ++i) {
        if (coders[i].method == QW_COMPRESSION_7Z_COPY_METHOD) {
            if (coders[i].properties_size) return 0;
            continue;
        }
        ++active_coder_count;
        if (coders[i].method == QW_COMPRESSION_7Z_LZMA1_METHOD) {
            ++lzma1_count;
            codec_index = i;
        } else if (coders[i].method == QW_COMPRESSION_7Z_LZMA2_METHOD) {
            ++lzma2_count;
            codec_index = i;
        }
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

    if (lzma1_count + lzma2_count == 0u) {
        if (active_coder_count != 0u) return 0;
        if (input_size != output_size) return 0;
        memcpy(output, input, output_size);
        return 1;
    }
    if (lzma1_count + lzma2_count != 1u || deflate_count ||
        codec_index == SIZE_MAX) return 0;
    for (size_t i = 0u; i < codec_index; ++i) {
        if (coders[i].method != QW_COMPRESSION_7Z_COPY_METHOD) return 0;
    }
    if (coders[codec_index].method == QW_COMPRESSION_7Z_LZMA1_METHOD) {
        if (!inflate_lzma1(&coders[codec_index], input, input_size,
                           output, output_size, memory_limit)) return 0;
    } else if (!inflate_lzma2(&coders[codec_index], input, input_size,
                              output, output_size, memory_limit)) {
        return 0;
    }
    for (size_t i = codec_index + 1u; i < coder_count; ++i) {
        if (!apply_7z_filter(&coders[i], output, output_size)) return 0;
    }
    return 1;
}
