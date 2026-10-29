#include <quirewise/compression.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return 1; \
    } \
} while (0)

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static size_t decode_hex(const char *hex, uint8_t *output, size_t capacity)
{
    size_t length = strlen(hex);
    if ((length & 1u) || length / 2u > capacity) return 0u;
    for (size_t i = 0u; i < length / 2u; ++i) {
        int high = hex_value(hex[i * 2u]);
        int low = hex_value(hex[i * 2u + 1u]);
        if (high < 0 || low < 0) return 0u;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return length / 2u;
}

int main(void)
{
    static const uint8_t expected[] =
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n"
        "Quirewise 7z raw LZMA fixture\n";
    static const char lzma1_hex[] =
        "00289d4927558dc1605540399fcd93704cde5761cb836d0e855ab19589d6f409"
        "32219e3ed7f3fffff5b0c000";
    static const char lzma2_hex[] =
        "e0012b00265d00289d4927558dc1605540399fcd93704cde5761cb836d0e855ab"
        "19589d6f40932219e28ea000000";
    static const uint8_t lzma1_properties[] = {0x5du, 0u, 0u, 0x10u, 0u};
    static const uint8_t lzma2_properties[] = {0x10u};
    static const uint8_t delta_lzma2_properties[] = {8u};
    static const uint8_t delta_properties[] = {0u};
    static const uint8_t delta_expected[] =
        "delta filter known-answer fixture\n"
        "delta filter known-answer fixture\n";
    static const uint8_t delta_stream[] = {
        0xe0u, 0x00u, 0x43u, 0x00u, 0x2bu, 0x5du, 0x00u, 0x32u,
        0x00u, 0x3cu, 0xe0u, 0x65u, 0x6cu, 0xc2u, 0x24u, 0xeau,
        0x1cu, 0x3du, 0x67u, 0xd1u, 0x0fu, 0xa9u, 0xb4u, 0xc1u,
        0x39u, 0x5bu, 0x4eu, 0xcfu, 0x13u, 0xbfu, 0xd8u, 0x14u,
        0x14u, 0xa7u, 0x7du, 0x1du, 0x0cu, 0x98u, 0x01u, 0x44u,
        0x64u, 0xfbu, 0x27u, 0x9fu, 0xeau, 0x5eu, 0x1du, 0xd0u,
        0x00u, 0x00u, 0x00u
    };
    static const uint8_t x86_expected[] = {
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u,
        0xe8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x90u
    };
    static const uint8_t x86_stream[] = {
        0xe0u, 0x00u, 0x2fu, 0x00u, 0x19u, 0x5du, 0x00u, 0x74u,
        0x01u, 0x3cu, 0x19u, 0x38u, 0xb7u, 0x0eu, 0x01u, 0xabu,
        0x54u, 0xdbu, 0xb0u, 0x07u, 0x40u, 0xf7u, 0xffu, 0x48u,
        0x0cu, 0xcfu, 0xe1u, 0xffu, 0xbbu, 0xa2u, 0x2cu, 0xb9u,
        0x00u
    };
    uint8_t lzma1_stream[64];
    uint8_t lzma2_stream[64];
    uint8_t one_byte_input[] = {0u};
    uint8_t one_byte_output[sizeof(one_byte_input)];
    uint8_t output[sizeof(expected) - 1u];
    size_t lzma1_size = decode_hex(lzma1_hex, lzma1_stream,
                                   sizeof(lzma1_stream));
    size_t lzma2_size = decode_hex(lzma2_hex, lzma2_stream,
                                   sizeof(lzma2_stream));
    QwCompression7zCoder coder;

    CHECK(lzma1_size != 0u && lzma2_size != 0u);
    coder = (QwCompression7zCoder){QW_COMPRESSION_7Z_LZMA1_METHOD,
                                   lzma1_properties,
                                   sizeof(lzma1_properties)};
    CHECK(qw_compression_inflate_7z_chain(&coder, 1u, lzma1_stream,
        lzma1_size, output, sizeof(output), 256u * 1024u * 1024u));
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    CHECK(!qw_compression_inflate_7z_chain(&coder, 1u, lzma1_stream,
        lzma1_size, output, sizeof(output), 1024u));
    CHECK(!qw_compression_inflate_7z_chain(&coder, 1u, lzma1_stream,
        lzma1_size, output, sizeof(output) - 1u, 256u * 1024u * 1024u));

    coder = (QwCompression7zCoder){QW_COMPRESSION_7Z_LZMA2_METHOD,
                                   lzma2_properties,
                                   sizeof(lzma2_properties)};
    CHECK(qw_compression_inflate_7z_chain(&coder, 1u, lzma2_stream,
        lzma2_size, output, sizeof(output), 256u * 1024u * 1024u));
    CHECK(memcmp(output, expected, sizeof(output)) == 0);

    {
        const QwCompression7zCoder coders[] = {
            {QW_COMPRESSION_7Z_LZMA2_METHOD, delta_lzma2_properties,
             sizeof(delta_lzma2_properties)},
            {QW_COMPRESSION_7Z_DELTA_METHOD, delta_properties,
             sizeof(delta_properties)}
        };
        uint8_t filtered_output[sizeof(delta_expected) - 1u];
        CHECK(qw_compression_inflate_7z_chain(
            coders, sizeof(coders) / sizeof(coders[0]), delta_stream,
            sizeof(delta_stream), filtered_output, sizeof(filtered_output),
            1024u * 1024u));
        CHECK(memcmp(filtered_output, delta_expected,
                     sizeof(filtered_output)) == 0);
    }
    {
        const QwCompression7zCoder coders[] = {
            {QW_COMPRESSION_7Z_LZMA2_METHOD, delta_lzma2_properties,
             sizeof(delta_lzma2_properties)},
            {QW_COMPRESSION_7Z_X86_METHOD, NULL, 0u}
        };
        uint8_t filtered_output[sizeof(x86_expected)];
        CHECK(qw_compression_inflate_7z_chain(
            coders, sizeof(coders) / sizeof(coders[0]), x86_stream,
            sizeof(x86_stream), filtered_output, sizeof(filtered_output),
            1024u * 1024u));
        CHECK(memcmp(filtered_output, x86_expected,
                     sizeof(filtered_output)) == 0);
    }

    coder.method = UINT32_C(0xdeadbeef);
    CHECK(!qw_compression_inflate_7z_chain(&coder, 1u, lzma2_stream,
        lzma2_size, output, sizeof(output), 256u * 1024u * 1024u));
    coder.properties = NULL;
    coder.properties_size = 0u;
    CHECK(!qw_compression_inflate_7z_chain(
        &coder, 1u, one_byte_input, sizeof(one_byte_input), one_byte_output,
        sizeof(one_byte_output), 1024u));
    puts("PASS: 7z LZMA, Delta, and BCJ known-answer vectors");
    return 0;
}
