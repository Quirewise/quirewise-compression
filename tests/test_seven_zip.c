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
    uint8_t lzma1_stream[64];
    uint8_t lzma2_stream[64];
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

    coder.method = UINT32_C(0xdeadbeef);
    CHECK(!qw_compression_inflate_7z_chain(&coder, 1u, lzma2_stream,
        lzma2_size, output, sizeof(output), 256u * 1024u * 1024u));
    puts("PASS: 7z LZMA1 and LZMA2 known-answer vectors");
    return 0;
}
