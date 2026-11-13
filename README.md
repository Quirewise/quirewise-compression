# Quirewise Compression

Quirewise Compression is an independently installable C11 library for DEFLATE,
zlib and gzip inflate, CRC-32, Adler-32, stored-block DEFLATE encoding, XZ
streams, and 7z coder chains (LZMA1/LZMA2/Deflate/Copy/Delta and the x86, PPC,
IA64, ARM, ARM-Thumb and SPARC branch filters). It is a first-party
implementation: the package neither discovers nor links LibLZMA, XZ, Zlib or
the 7-Zip SDK.

The v1 encoder emits valid stored DEFLATE blocks and does not attempt entropy
compression. Raw DEFLATE, zlib, strict XZ, and 7z inflate require the caller to
provide the exact decompressed size. XZ prefix decoding takes the requested
prefix size and may leave the stream trailer unchecked. The stored encoder
writes into a caller-owned buffer sized with the provided bound helpers. Input
and output buffers must not overlap. Inflate may have written partial output if
it returns failure. Decoder memory usage is checked against the caller's
supplied limit before decoding.

The 7z coder-chain API supports LZMA1, LZMA2, Deflate, Copy, Delta, and the
branch filters listed above. ZIP, PNG, PDF, SVGZ, TIFF, and audio container
parsing remain in their domain libraries.

Limits and unsupported cases:

- No encoder beyond stored DEFLATE blocks.
- Whole-stream XZ only accepts a single stream; concatenated streams and the
  IA64 BCJ filter are not implemented.
- 7z chains are linear; multi-stream, encrypted and unsupported coders return
  an error.

Build, test, and install with CMake:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    cmake --install build --prefix install

Consumers use `find_package(QuirewiseCompression CONFIG REQUIRED)` and link
`Quirewise::Compression`.
