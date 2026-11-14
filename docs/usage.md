# quirewise-compression usage guide

This guide covers the task-level workflows of `Quirewise::Compression`. The
public API is declared in `quirewise/compression.h`.

## 1. Build and integrate

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

Consumer CMake:

```cmake
find_package(QuirewiseCompression CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Compression)
```

The package is header + static/shared library only; there is nothing to
initialize at runtime.

## 2. Core model

The library works on caller-owned buffers:

- `input` / `input_size` describe the compressed stream.
- `output` / `output_size` describe the destination buffer.
- Exact-size calls require `output_size` to be the exact decompressed size.
- Bounded calls take `output_capacity` and report `out_size`.
- No function allocates the output buffer or retains pointers after returning.
- Input and output buffers must not overlap.

Return conventions:

| API family | Success | Failure |
|---|---|---|
| Exact inflate, 7z, XZ, stored deflate | `1` | `0` |
| Bounded inflate | `QW_COMPRESSION_INFLATE_OK` | `..._OUTPUT_FULL` or `..._ERROR` |

## 3. Checksums and bounds

```c
#include <quirewise/compression.h>
#include <stdio.h>

int main(void)
{
    const uint8_t data[] = "quirewise";
    printf("crc32=%08x adler32=%08x\n",
           qw_compression_crc32(data, sizeof(data) - 1u),
           qw_compression_adler32(data, sizeof(data) - 1u));

    size_t bound = qw_compression_zlib_stored_bound(1024u);
    if (bound == SIZE_MAX) {
        fputs("bound overflow\n", stderr);
        return 1;
    }
    printf("zlib stored bound for 1024 bytes: %zu\n", bound);
    return 0;
}
```

A bound of `SIZE_MAX` means the calculation would overflow `size_t`; treat it
as an error rather than allocating.

## 4. Exact-size inflate

Use this when the decompressed size is known from a container header.

```c
uint8_t *out = malloc(expected_size);
if (!out) return 1;
if (!qw_compression_inflate_zlib(compressed, compressed_size, out,
                                 expected_size)) {
    free(out);
    return 1; /* malformed stream, wrong size, or checksum mismatch */
}
/* use out, expected_size */
free(out);
```

The same pattern applies to:

- `qw_compression_inflate_raw` — raw RFC 1951.
- `qw_compression_inflate_gzip` — exactly one gzip member; trailing bytes and
  extra members are rejected.
- `qw_compression_inflate_xz` — one complete XZ stream.

All of these verify the format trailer and checksum before returning success.

## 5. Bounded inflate and retry

When the size is unknown, start with a reasonable buffer. `OUTPUT_FULL` means
the stream is valid so far but needs more room; `out_size` is the number of
bytes written. Grow the buffer and retry from the beginning of the stream.

```c
size_t capacity = 64u * 1024u;
uint8_t *out = malloc(capacity);
size_t written = 0u;
if (!out) return 1;

for (;;) {
    QwCompressionInflateStatus st = qw_compression_inflate_zlib_bounded(
        compressed, compressed_size, out, capacity, &written);
    if (st == QW_COMPRESSION_INFLATE_OK) break;
    if (st == QW_COMPRESSION_INFLATE_ERROR) {
        free(out);
        return 1;
    }
    if (capacity > (16u * 1024u * 1024u)) { /* application policy */
        free(out);
        return 1;
    }
    {
        size_t next = capacity * 2u;
        uint8_t *grown = realloc(out, next);
        if (!grown) {
            free(out);
            return 1;
        }
        out = grown;
        capacity = next;
    }
}
/* use out, written */
free(out);
```

Set your own maximum before doubling: a valid stream can legitimately expand
to a very large size, so the retry loop must be bounded by policy.

## 6. XZ

Whole-stream decode:

```c
if (!qw_compression_inflate_xz(xz, xz_size, out, exact_size, 64u * 1024u * 1024u))
    return 1;
```

The last argument caps decoder-owned allocations (dictionary and working
buffers); it is not an output-size cap. Choose it from the application's memory
budget.

Prefix decode is intended for reading the first N bytes of a large stream
(for example, a preview or an embedded resource):

```c
uint8_t prefix[4096];
if (!qw_compression_inflate_xz_prefix(xz, xz_size, prefix, sizeof(prefix),
                                      64u * 1024u * 1024u))
    return 1;
```

Prefix decoding stops when the output is full and may not validate the stream
trailer or remaining input. Do not use it when you need integrity guarantees.

## 7. 7z coder chains

A 7z container describes a linear chain of coders. Fill one
`QwCompression7zCoder` per filter, in container order, with the method id and
the coder properties:

```c
QwCompression7zCoder coders[2];
coders[0].method = QW_COMPRESSION_7Z_LZMA2_METHOD;
coders[0].properties = lzma2_props;
coders[0].properties_size = lzma2_props_size;
coders[1].method = QW_COMPRESSION_7Z_X86_METHOD;
coders[1].properties = x86_props;
coders[1].properties_size = x86_props_size;

if (!qw_compression_inflate_7z_chain(coders, 2u, input, input_size,
                                     output, output_size,
                                     64u * 1024u * 1024u))
    return 1;
```

Only the coders listed in the public header are supported. Multi-stream,
encrypted and unsupported methods fail; there is no fallback decoder.

## 8. Stored DEFLATE

The encoder emits stored (uncompressed) blocks only. Use the bound helpers to
size the destination:

```c
size_t bound = qw_compression_raw_stored_bound(input_size);
if (bound == SIZE_MAX) return 1;
uint8_t *out = malloc(bound);
size_t out_size = 0u;
if (!out) return 1;
if (!qw_compression_deflate_raw_stored(input, input_size, out, bound,
                                       &out_size)) {
    free(out);
    return 1;
}
```

`qw_compression_deflate_zlib_stored` adds the zlib header and Adler-32 trailer.

## 9. Error handling checklist

- Check every return value; `0` / `..._ERROR` means the output must not be used.
- A checksum mismatch is a failure even if the bytes decoded.
- `OUTPUT_FULL` is not corruption; retry with more capacity.
- Do not reuse a partially written output buffer without resetting your length.
- Treat `SIZE_MAX` from a bound helper as overflow.

## 10. Threading and lifetime

- No global mutable state; independent calls may run concurrently on separate
  buffers.
- The library never keeps pointers after a call returns.
- There is no context object to create or destroy.

## 11. Related packages

- `Quirewise::Archive` uses these decoders for ZIP/USTAR/7z container payloads.
- `Quirewise::Ebook`, `Quirewise::Document` and `Quirewise::Spreadsheet` use
  them for compressed package members.

## 12. License

MIT. See `LICENSE`.
