# quirewise-compression

First-party DEFLATE, zlib, gzip, XZ and 7z decompression, plus a fixed-Huffman
DEFLATE encoder with a stored-block fallback. No zlib, liblzma or 7-Zip runtime
dependency.

`Quirewise::Compression` is an independent, MIT-licensed C11 library. It is
installed as a CMake package and can be consumed without the Quirewise
application or any other Quirewise package.

## What it does

- Inflates raw RFC 1951 DEFLATE, RFC 1950 zlib and RFC 1952 gzip streams.
- Decodes single-stream XZ, including LZMA2, Delta and BCJ filters.
- Decodes linear 7z coder chains (LZMA1, LZMA2, Deflate, Copy, Delta and the
  x86/PPC/IA64/ARM/ARM-Thumb/SPARC BCJ filters).
- Computes CRC-32 and Adler-32.
- Encodes DEFLATE and zlib streams: a single-hash LZ77 pass over the fixed
  Huffman code, falling back to stored blocks when that is smaller.
- Provides exact-size and bounded inflate entry points, plus worst-case size
  bounds for both the compressing and stored encoders.

It is not a general-purpose compressor: there is no dynamic-Huffman encoder,
no XZ/7z writer, and no streaming API.

## Requirements

- C11 compiler and CMake 3.21 or newer.
- No third-party libraries.
- CTest is used for the test suite.

## Build, test and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

Shared builds are available with `-DQUIREWISE_BUILD_SHARED=ON`.

## Use it from CMake

```cmake
find_package(QuirewiseCompression CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Compression)
```

If the package is installed outside a default prefix, pass
`-DCMAKE_PREFIX_PATH=/path/to/prefix` when configuring your project.

## Quick start

Encode a small buffer as a stored raw DEFLATE stream and decode it again:

```c
#include <quirewise/compression.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t input[] = "hello";
    uint8_t compressed[64];
    uint8_t restored[sizeof input - 1u];
    size_t compressed_size = 0u;

    if (!qw_compression_deflate_raw_stored(
            input, sizeof(input) - 1u, compressed, sizeof(compressed),
            &compressed_size)) {
        fputs("deflate failed\n", stderr);
        return 1;
    }
    if (!qw_compression_inflate_raw(compressed, compressed_size, restored,
                                    sizeof(restored))) {
        fputs("inflate failed\n", stderr);
        return 1;
    }
    if (memcmp(input, restored, sizeof(restored)) != 0) {
        fputs("round-trip mismatch\n", stderr);
        return 1;
    }
    puts("stored DEFLATE round-trip OK");
    return 0;
}
```

For real compressed input, pass the exact decompressed size to
`qw_compression_inflate_*`, or use the bounded variants when the size is not
known in advance.

## API overview

| Header | Purpose |
|---|---|
| `quirewise/compression.h` | all public entry points |

| Task | Functions |
|---|---|
| Version / hashes | `qw_compression_abi_version`, `qw_compression_crc32`, `qw_compression_adler32` |
| Size bounds | `qw_compression_raw_stored_bound`, `qw_compression_zlib_stored_bound` |
| Exact-size inflate | `qw_compression_inflate_raw`, `qw_compression_inflate_zlib`, `qw_compression_inflate_gzip`, `qw_compression_inflate_xz` |
| Bounded inflate | `qw_compression_inflate_raw_bounded`, `qw_compression_inflate_zlib_bounded` |
| XZ prefix | `qw_compression_inflate_xz_prefix` |
| 7z coder chain | `qw_compression_inflate_7z_chain` |
| Stored encode | `qw_compression_deflate_raw_stored`, `qw_compression_deflate_zlib_stored` |
| Compressing encode | `qw_compression_deflate_raw`, `qw_compression_deflate_zlib` (fixed Huffman, stored fallback) |

## Behavior and limits

- Exact-size functions return `1` on success and `0` on invalid arguments,
  malformed input, insufficient output capacity or checksum mismatch.
- Bounded functions return `QW_COMPRESSION_INFLATE_OK`,
  `QW_COMPRESSION_INFLATE_OUTPUT_FULL` or
  `QW_COMPRESSION_INFLATE_ERROR`; `out_size` reports the bytes written.
- The caller owns all input and output buffers. Input and output must not
  overlap. The library does not allocate output buffers.
- XZ decoding accepts one complete stream; concatenated streams are rejected.
  Prefix decoding may leave the trailer unchecked by design.
- 7z chains are linear. Multi-stream, encrypted and unsupported coders return
  failure. `memory_limit` caps decoder-owned allocations.
- The encoder is fixed-Huffman only (no dynamic Huffman); the stored
  functions remain available as the fallback path.

## Testing

`ctest` covers known-answer vectors for raw/zlib/gzip/XZ/7z, checksum cases,
stored-stream round trips, malformed-input rejection, memory-limit handling and
a fuzz smoke run.

## Documentation

See [docs/usage.md](docs/usage.md) for task-oriented recipes, bounded-inflate
patterns and the 7z/XZ details.

## License

MIT. See [LICENSE](LICENSE).
