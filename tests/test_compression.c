#include <quirewise/compression.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
    size_t i;
    if ((length & 1u) || length / 2u > capacity) return 0u;
    for (i = 0u; i < length / 2u; ++i) {
        int high = hex_value(hex[i * 2u]);
        int low = hex_value(hex[i * 2u + 1u]);
        if (high < 0 || low < 0) return 0u;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return length / 2u;
}

int main(void)
{
    static const uint8_t raw_stored_abc[] = {
        0x01u, 0x03u, 0x00u, 0xfcu, 0xffu, 'a', 'b', 'c'
    };
    static const uint8_t raw_stored_empty[] = {
        0x01u, 0x00u, 0x00u, 0xffu, 0xffu
    };
    static const uint8_t zlib_stored_abc[] = {
        0x78u, 0x01u, 0x01u, 0x03u, 0x00u, 0xfcu, 0xffu,
        'a', 'b', 'c', 0x02u, 0x4du, 0x01u, 0x27u
    };
    static const uint8_t zlib_stored_empty[] = {
        0x78u, 0x01u, 0x01u, 0x00u, 0x00u, 0xffu, 0xffu,
        0x00u, 0x00u, 0x00u, 0x01u
    };
    static const uint8_t fixed_expected[] =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    static const char fixed_hex[] =
        "0bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228014ae72456552aa4e4a7eb298490a01800";
    static const char dynamic_hex[] =
        "edcc410ac0201043d1abccd5224a95ce54a176d3d3578f51f88b8440e0c94795a53265872264b9f8da65dccdfb65ef3e7666ddddfaaa5363c85c91b22c1e13040404040404040404040404040404040404040404040404c43f890f";
    static const char dynamic_pattern[] =
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu ";
    static const uint8_t dictionary_header[] = {
        0x78u, 0x20u, 0x01u, 0x00u, 0x00u, 0x00u
    };
    uint8_t output[7000];
    uint8_t compressed[7100];
    uint8_t fixed_stream[128];
    uint8_t dynamic_stream[128];
    uint8_t large_input[70000];
    size_t fixed_size;
    size_t dynamic_size;
    size_t encoded_size;
    size_t expected_size;
    size_t i;

    CHECK(qw_compression_abi_version() == 1u);
    CHECK(qw_compression_crc32("123456789", 9u) == 0xcbf43926u);
    CHECK(qw_compression_crc32(NULL, 0u) == 0u);
    CHECK(qw_compression_adler32("abc", 3u) == 0x024d0127u);
    CHECK(qw_compression_adler32(NULL, 0u) == 1u);

    CHECK(qw_compression_inflate_raw(raw_stored_abc,
        sizeof(raw_stored_abc), output, 3u));
    CHECK(memcmp(output, "abc", 3u) == 0);
    CHECK(qw_compression_inflate_raw(raw_stored_empty,
        sizeof(raw_stored_empty), NULL, 0u));
    CHECK(!qw_compression_inflate_raw(raw_stored_abc,
        sizeof(raw_stored_abc) - 1u, output, 3u));
    CHECK(!qw_compression_inflate_raw(raw_stored_abc,
        sizeof(raw_stored_abc), output, 2u));
    CHECK(!qw_compression_inflate_raw(raw_stored_abc,
        sizeof(raw_stored_abc), output, 4u));
    CHECK(!qw_compression_inflate_raw(raw_stored_empty,
        sizeof(raw_stored_empty), NULL, 1u));
    CHECK(!qw_compression_inflate_raw(NULL, 0u, NULL, 0u));

    CHECK(qw_compression_inflate_zlib(zlib_stored_abc,
        sizeof(zlib_stored_abc), output, 3u));
    CHECK(memcmp(output, "abc", 3u) == 0);
    CHECK(qw_compression_inflate_zlib(zlib_stored_empty,
        sizeof(zlib_stored_empty), NULL, 0u));
    memcpy(compressed, zlib_stored_abc, sizeof(zlib_stored_abc));
    compressed[sizeof(zlib_stored_abc) - 1u] ^= 1u;
    CHECK(!qw_compression_inflate_zlib(compressed,
        sizeof(zlib_stored_abc), output, 3u));
    CHECK(!qw_compression_inflate_zlib(dictionary_header,
        sizeof(dictionary_header), output, 0u));
    CHECK(!qw_compression_inflate_zlib(zlib_stored_abc,
        sizeof(zlib_stored_abc) - 1u, output, 3u));

    fixed_size = decode_hex(fixed_hex, fixed_stream, sizeof(fixed_stream));
    CHECK(fixed_size > 0u);
    CHECK(qw_compression_inflate_raw(fixed_stream, fixed_size, output,
        sizeof(fixed_expected) - 1u));
    CHECK(memcmp(output, fixed_expected, sizeof(fixed_expected) - 1u) == 0);
    CHECK(!qw_compression_inflate_raw(fixed_stream, fixed_size, output,
        sizeof(fixed_expected) - 2u));

    dynamic_size = decode_hex(dynamic_hex, dynamic_stream,
                              sizeof(dynamic_stream));
    CHECK(dynamic_size > 0u);
    expected_size = (sizeof(dynamic_pattern) - 1u) * 100u;
    for (i = 0u; i < expected_size; ++i)
        output[i] = (uint8_t)dynamic_pattern[i % (sizeof(dynamic_pattern) - 1u)];
    CHECK(qw_compression_inflate_raw(dynamic_stream, dynamic_size,
                                     compressed, expected_size));
    CHECK(memcmp(compressed, output, expected_size) == 0);

    CHECK(qw_compression_raw_stored_bound(0u) == 5u);
    CHECK(qw_compression_zlib_stored_bound(0u) == 11u);
    CHECK(qw_compression_raw_stored_bound(SIZE_MAX) == SIZE_MAX);
    CHECK(qw_compression_zlib_stored_bound(SIZE_MAX) == SIZE_MAX);
    CHECK(qw_compression_deflate_raw_stored(NULL, 0u, compressed,
        sizeof(compressed), &encoded_size));
    CHECK(encoded_size == sizeof(raw_stored_empty));
    CHECK(memcmp(compressed, raw_stored_empty, encoded_size) == 0);
    CHECK(qw_compression_deflate_zlib_stored(NULL, 0u, compressed,
        sizeof(compressed), &encoded_size));
    CHECK(encoded_size == sizeof(zlib_stored_empty));
    CHECK(memcmp(compressed, zlib_stored_empty, encoded_size) == 0);

    for (i = 0u; i < sizeof(large_input); ++i)
        large_input[i] = (uint8_t)((i * 37u + i / 251u) & 0xffu);
    encoded_size = 123u;
    CHECK(!qw_compression_deflate_raw_stored(large_input,
        sizeof(large_input), compressed, sizeof(compressed), &encoded_size));
    CHECK(encoded_size == 0u);
    encoded_size = 123u;
    CHECK(!qw_compression_deflate_zlib_stored(large_input,
        sizeof(large_input), compressed, sizeof(compressed), &encoded_size));
    CHECK(encoded_size == 0u);

    {
        size_t large_bound = qw_compression_raw_stored_bound(sizeof(large_input));
        uint8_t *large_encoded = (uint8_t *)malloc(large_bound);
        uint8_t *large_decoded = (uint8_t *)malloc(sizeof(large_input));
        size_t zlib_bound = qw_compression_zlib_stored_bound(sizeof(large_input));
        uint8_t *large_zlib = (uint8_t *)malloc(zlib_bound);
        CHECK(large_encoded && large_decoded && large_zlib);
        CHECK(qw_compression_deflate_raw_stored(large_input,
            sizeof(large_input), large_encoded, large_bound, &encoded_size));
        CHECK(encoded_size == large_bound);
        CHECK(qw_compression_inflate_raw(large_encoded, encoded_size,
            large_decoded, sizeof(large_input)));
        CHECK(memcmp(large_input, large_decoded, sizeof(large_input)) == 0);
        CHECK(qw_compression_deflate_zlib_stored(large_input,
            sizeof(large_input), large_zlib, zlib_bound, &encoded_size));
        CHECK(encoded_size == zlib_bound);
        CHECK(qw_compression_inflate_zlib(large_zlib, encoded_size,
            large_decoded, sizeof(large_input)));
        CHECK(memcmp(large_input, large_decoded, sizeof(large_input)) == 0);
        free(large_zlib);
        free(large_decoded);
        free(large_encoded);
    }

    encoded_size = 123u;
    CHECK(!qw_compression_deflate_raw_stored("abc", 3u, compressed,
        7u, &encoded_size));
    CHECK(encoded_size == 0u);
    CHECK(!qw_compression_deflate_raw_stored(NULL, 1u, compressed,
        sizeof(compressed), &encoded_size));

    puts("PASS: compression known-answer vectors and stored-stream round trips");
    return 0;
}
