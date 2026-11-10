#include <quirewise/compression.h>

#include "internal/qw_lzma.h"

#include <limits.h>
#include <string.h>

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void write_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void write_u32_be(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static int inflate_lzma1(const QwCompression7zCoder *coder,
                         const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t output_size,
                         size_t memory_limit)
{
    QwLzmaProps props;
    size_t in_used = 0u;
    if (!coder->properties || coder->properties_size != 5u) return 0;
    if (!qw_lzma_props_parse(coder->properties, &props)) return 0;
    if ((size_t)props.dict_size > memory_limit) return 0;
    if (!qw_lzma1_decode(input, input_size, &props, output, output_size,
                         &in_used))
        return 0;
    return in_used == input_size;
}

static int inflate_lzma2(const QwCompression7zCoder *coder,
                         const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t output_size,
                         size_t memory_limit)
{
    size_t in_used = 0u;
    if (!coder->properties || coder->properties_size != 1u) return 0;
    if ((size_t)qw_lzma2_dict_size(coder->properties[0]) > memory_limit)
        return 0;
    if (!qw_lzma2_decode(input, input_size, coder->properties[0], output,
                         output_size, &in_used, NULL))
        return 0;
    return in_used == input_size;
}

static int apply_delta(const QwCompression7zCoder *coder, uint8_t *output,
                       size_t output_size)
{
    uint8_t state[256];
    size_t distance;
    size_t i;
    if (!coder->properties || coder->properties_size != 1u) return 0;
    distance = (size_t)coder->properties[0] + 1u;
    if (distance > sizeof(state)) return 0;
    memset(state, 0, sizeof(state));
    for (i = 0; i < output_size; ++i) {
        size_t pos = i % distance;
        output[i] = (uint8_t)(output[i] + state[pos]);
        state[pos] = output[i];
    }
    return 1;
}

/* x86 BCJ: convert absolute call/jump addresses back to relative form. */
static int apply_bcj_x86(const uint8_t *properties, size_t properties_size,
                         uint8_t *output, size_t output_size)
{
    uint32_t pc = 0u;
    size_t i;
    if (properties_size == 4u && properties) pc = read_u32_le(properties);
    else if (properties_size != 0u) return 0;
    for (i = 0; i + 5u <= output_size; ++i) {
        uint8_t opcode = output[i];
        if (opcode == 0xE8u || opcode == 0xE9u) {
            uint32_t address = read_u32_le(output + i + 1u);
            address -= (uint32_t)(pc + (uint32_t)i + 5u);
            write_u32_le(output + i + 1u, address);
            i += 4u;
        }
    }
    return 1;
}

static int apply_bcj_ppc(const uint8_t *properties, size_t properties_size,
                         uint8_t *output, size_t output_size)
{
    uint32_t pc = 0u;
    size_t i;
    if (properties_size == 4u && properties) pc = read_u32_le(properties);
    else if (properties_size != 0u) return 0;
    for (i = 0; i + 4u <= output_size; i += 4u) {
        if ((output[i] >> 2) == 0x12u && (output[i + 3u] & 0x03u) == 0x01u) {
            uint32_t value = read_u32_be(output + i);
            uint32_t address = ((value & 0x03FFFFFCu) << 2) -
                               (pc + (uint32_t)i);
            value = 0x48000000u | ((address >> 2) & 0x03FFFFFCu);
            write_u32_be(output + i, value);
        }
    }
    return 1;
}

static int apply_bcj_arm(const uint8_t *properties, size_t properties_size,
                         uint8_t *output, size_t output_size)
{
    uint32_t pc = 0u;
    size_t i;
    if (properties_size == 4u && properties) pc = read_u32_le(properties);
    else if (properties_size != 0u) return 0;
    for (i = 0; i + 4u <= output_size; i += 4u) {
        if (output[i + 3u] == 0xEBu) {
            uint32_t value = read_u32_le(output + i) & 0x00FFFFFFu;
            int32_t address = (int32_t)(value << 2) -
                              (int32_t)(pc + (uint32_t)i + 8u);
            value = ((uint32_t)(address >> 2)) & 0x00FFFFFFu;
            write_u32_le(output + i, value);
            output[i + 3u] = 0xEBu;
        }
    }
    return 1;
}

static int apply_bcj_armt(const uint8_t *properties, size_t properties_size,
                          uint8_t *output, size_t output_size)
{
    uint32_t pc = 0u;
    size_t i;
    if (properties_size == 4u && properties) pc = read_u32_le(properties);
    else if (properties_size != 0u) return 0;
    for (i = 0; i + 4u <= output_size; i += 2u) {
        uint16_t v1 = read_u16_le(output + i);
        uint16_t v2 = read_u16_le(output + i + 2u);
        if ((v1 & 0xF800u) == 0xF000u && (v2 & 0xF800u) == 0xF800u) {
            uint32_t address = (((uint32_t)(v1 & 0x07FFu) << 12) |
                                ((uint32_t)(v2 & 0x07FFu) << 1)) -
                               (pc + (uint32_t)i + 4u);
            v1 = (uint16_t)((v1 & 0xF800u) | ((address >> 12) & 0x07FFu));
            v2 = (uint16_t)((v2 & 0xF800u) | ((address >> 1) & 0x07FFu));
            output[i] = (uint8_t)v1;
            output[i + 1] = (uint8_t)(v1 >> 8);
            output[i + 2] = (uint8_t)v2;
            output[i + 3] = (uint8_t)(v2 >> 8);
        }
    }
    return 1;
}

static int apply_bcj_sparc(const uint8_t *properties, size_t properties_size,
                           uint8_t *output, size_t output_size)
{
    uint32_t pc = 0u;
    size_t i;
    if (properties_size == 4u && properties) pc = read_u32_le(properties);
    else if (properties_size != 0u) return 0;
    for (i = 0; i + 4u <= output_size; i += 4u) {
        uint32_t value = read_u32_be(output + i);
        if (((value >> 22) == 0x100u) || ((value >> 22) == 0x1FFu)) {
            uint32_t address = ((value & 0x3FFFFFFFu) << 2) -
                               (pc + (uint32_t)i);
            value = 0x40000000u | ((address >> 2) & 0x3FFFFFFFu);
            write_u32_be(output + i, value);
        }
    }
    return 1;
}

static int apply_7z_filter(const QwCompression7zCoder *coder,
                           uint8_t *output, size_t output_size)
{
    switch (coder->method) {
    case QW_COMPRESSION_7Z_COPY_METHOD:
        return coder->properties_size == 0u;
    case QW_COMPRESSION_7Z_DELTA_METHOD:
        return apply_delta(coder, output, output_size);
    case QW_COMPRESSION_7Z_X86_METHOD:
        return apply_bcj_x86(coder->properties, coder->properties_size,
                             output, output_size);
    case QW_COMPRESSION_7Z_POWERPC_METHOD:
        return apply_bcj_ppc(coder->properties, coder->properties_size,
                             output, output_size);
    case QW_COMPRESSION_7Z_ARM_METHOD:
        return apply_bcj_arm(coder->properties, coder->properties_size,
                             output, output_size);
    case QW_COMPRESSION_7Z_ARMTHUMB_METHOD:
        return apply_bcj_armt(coder->properties, coder->properties_size,
                              output, output_size);
    case QW_COMPRESSION_7Z_SPARC_METHOD:
        return apply_bcj_sparc(coder->properties, coder->properties_size,
                               output, output_size);
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
    size_t i;

    if (!coders || !coder_count ||
        coder_count > QW_COMPRESSION_7Z_MAX_CODERS ||
        !input || !input_size || !output || !output_size || !memory_limit) {
        return 0;
    }
    for (i = 0u; i < coder_count; ++i) {
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
    for (i = 0u; i < codec_index; ++i) {
        if (coders[i].method != QW_COMPRESSION_7Z_COPY_METHOD) return 0;
    }
    if (coders[codec_index].method == QW_COMPRESSION_7Z_LZMA1_METHOD) {
        if (!inflate_lzma1(&coders[codec_index], input, input_size,
                           output, output_size, memory_limit)) return 0;
    } else if (!inflate_lzma2(&coders[codec_index], input, input_size,
                              output, output_size, memory_limit)) {
        return 0;
    }
    for (i = codec_index + 1u; i < coder_count; ++i) {
        if (!apply_7z_filter(&coders[i], output, output_size)) return 0;
    }
    return 1;
}
