#include <quirewise/compression.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return 1; \
    } \
} while (0)

enum { XZ_PREFIX_SIZE = 11u };

int main(void)
{
    static const uint8_t xz_stream[] = {
        0xfdu, 0x37u, 0x7au, 0x58u, 0x5au, 0x00u, 0x00u, 0x04u,
        0xe6u, 0xd6u, 0xb4u, 0x46u, 0x02u, 0x00u, 0x21u, 0x01u,
        0x16u, 0x00u, 0x00u, 0x00u, 0x74u, 0x2fu, 0xe5u, 0xa3u,
        0x01u, 0x00u, 0x21u, 0x51u, 0x75u, 0x69u, 0x72u, 0x65u,
        0x77u, 0x69u, 0x73u, 0x65u, 0x20u, 0x58u, 0x5au, 0x20u,
        0x6bu, 0x6eu, 0x6fu, 0x77u, 0x6eu, 0x2du, 0x61u, 0x6eu,
        0x73u, 0x77u, 0x65u, 0x72u, 0x20u, 0x66u, 0x69u, 0x78u,
        0x74u, 0x75u, 0x72u, 0x65u, 0x0au, 0x00u, 0x00u, 0x00u,
        0xe9u, 0x96u, 0xabu, 0xbeu, 0x1fu, 0x3eu, 0x94u, 0xc8u,
        0x00u, 0x01u, 0x3au, 0x22u, 0xb6u, 0x2au, 0x4fu, 0xd0u,
        0x1fu, 0xb6u, 0xf3u, 0x7du, 0x01u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x04u, 0x59u, 0x5au
    };
    static const uint8_t expected[] = "Quirewise XZ known-answer fixture\n";
    uint8_t output[sizeof(expected) - 1u];
    uint8_t prefix[XZ_PREFIX_SIZE];
    uint8_t damaged[sizeof(xz_stream)];
    uint8_t concatenated[sizeof(xz_stream) * 2u];
    uint8_t doubled_output[sizeof(output) * 2u];

    CHECK(qw_compression_inflate_xz(xz_stream, sizeof(xz_stream), output,
                                    sizeof(output), 64u * 1024u * 1024u));
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    CHECK(qw_compression_inflate_xz_prefix(
        xz_stream, sizeof(xz_stream), prefix, sizeof(prefix),
        64u * 1024u * 1024u));
    CHECK(memcmp(prefix, expected, sizeof(prefix)) == 0);
    CHECK(!qw_compression_inflate_xz(xz_stream, sizeof(xz_stream), output,
                                     sizeof(output) - 1u,
                                     64u * 1024u * 1024u));
    CHECK(!qw_compression_inflate_xz(xz_stream, sizeof(xz_stream), output,
                                     sizeof(output), 1u));
    CHECK(!qw_compression_inflate_xz_prefix(
        xz_stream, 12u, output, sizeof(output), 64u * 1024u * 1024u));
    memcpy(concatenated, xz_stream, sizeof(xz_stream));
    memcpy(concatenated + sizeof(xz_stream), xz_stream, sizeof(xz_stream));
    CHECK(!qw_compression_inflate_xz(
        concatenated, sizeof(concatenated), doubled_output,
        sizeof(doubled_output), 64u * 1024u * 1024u));

    memcpy(damaged, xz_stream, sizeof(damaged));
    damaged[sizeof(damaged) - 30u] ^= 1u;
    CHECK(!qw_compression_inflate_xz(damaged, sizeof(damaged), output,
                                     sizeof(output),
                                     64u * 1024u * 1024u));

    /* A structurally valid XZ stream with zero blocks, produced by `printf '' |
     * xz` (32 bytes, CRC64 check, backward size 1). It must succeed and
     * produce exactly zero output bytes. */
    {
        static const uint8_t empty_stream[] = {
            0xfdu, 0x37u, 0x7au, 0x58u, 0x5au, 0x00u, 0x00u, 0x04u,
            0xe6u, 0xd6u, 0xb4u, 0x46u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x1cu, 0xdfu, 0x44u, 0x21u, 0x1fu, 0xb6u, 0xf3u, 0x7du,
            0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x04u, 0x59u, 0x5au
        };
        uint8_t empty_output[1] = {0xa5u};
        CHECK(qw_compression_inflate_xz(empty_stream, sizeof(empty_stream),
                                        empty_output, 0u,
                                        64u * 1024u * 1024u));
        CHECK(empty_output[0] == 0xa5u);
        /* Empty input is never a valid stream. */
        CHECK(!qw_compression_inflate_xz(empty_stream, 0u, empty_output, 0u,
                                         64u * 1024u * 1024u));
        CHECK(!qw_compression_inflate_xz(NULL, 0u, empty_output, 0u,
                                         64u * 1024u * 1024u));
    }
    puts("PASS: XZ known-answer, prefix, memory cap, integrity, empty-stream and empty-input vectors");
    return 0;
}
