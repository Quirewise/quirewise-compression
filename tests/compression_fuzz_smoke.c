#include <quirewise/compression.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_SEED UINT32_C(0x51f08844)
#define FUZZ_RUNS 250u

static uint32_t rng_state = FUZZ_SEED;

static const uint8_t zlib_seed[] = {
    0x78u, 0x01u, 0x01u, 0x03u, 0x00u, 0xfcu, 0xffu,
    'a', 'b', 'c', 0x02u, 0x4du, 0x01u, 0x27u
};
static const uint8_t gzip_seed[] = {
    0x1fu, 0x8bu, 0x08u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xffu,
    0x01u, 0x05u, 0x00u, 0xfau, 0xffu, 'h', 'e', 'l', 'l', 'o',
    0x86u, 0xa6u, 0x10u, 0x36u, 0x05u, 0x00u, 0x00u, 0x00u
};
static const uint8_t xz_seed[] = {
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

static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    rng_state = value;
    return value;
}

static size_t mutate(const uint8_t *seed, size_t seed_size, uint8_t *buffer,
                     size_t capacity)
{
    size_t length = seed_size > capacity ? capacity : seed_size;
    unsigned int flips = 1u + next_random() % 4u;
    size_t index;
    memcpy(buffer, seed, length);
    if (next_random() % 8u == 0u && length > 1u)
        length = 1u + next_random() % length;
    for (index = 0u; index < flips && length; ++index) {
        size_t position = next_random() % length;
        buffer[position] = (uint8_t)(buffer[position] ^
                                     (uint8_t)(1u << (next_random() % 8u)));
    }
    return length;
}

int main(void)
{
    uint8_t buffer[512];
    uint8_t output[128];
    size_t run;
    size_t ok = 0u;
    size_t rejected = 0u;
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        size_t size = mutate(zlib_seed, sizeof(zlib_seed), buffer,
                             sizeof(buffer));
        size_t produced = 0u;
        if (qw_compression_inflate_zlib_bounded(buffer, size, output,
                                                sizeof(output),
                                                &produced) ==
            QW_COMPRESSION_INFLATE_OK)
            ++ok;
        else
            ++rejected;
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        size_t size = mutate(gzip_seed, sizeof(gzip_seed), buffer,
                             sizeof(buffer));
        if (qw_compression_inflate_gzip(buffer, size, output, 5u))
            ++ok;
        else
            ++rejected;
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        size_t size = mutate(xz_seed, sizeof(xz_seed), buffer, sizeof(buffer));
        if (qw_compression_inflate_xz_prefix(buffer, size, output,
                                             sizeof(output),
                                             64u * 1024u * 1024u))
            ++ok;
        else
            ++rejected;
    }
    for (run = 0u; run < FUZZ_RUNS; ++run) {
        size_t size = mutate(zlib_seed, sizeof(zlib_seed), buffer,
                             sizeof(buffer));
        if (qw_compression_inflate_raw(buffer, size, output, sizeof(output)))
            ++ok;
        else
            ++rejected;
    }
    printf("Compression fuzz smoke: %zu ok, %zu rejected, seed=0x%08x runs=%u\n",
           ok, rejected, FUZZ_SEED, FUZZ_RUNS);
    return 0;
}
