# Quirewise Compression

Quirewise Compression is an independently installable C11 library for raw
RFC 1951 inflate, RFC 1950 zlib streams, CRC-32, Adler-32, and stored-block
encoding. It has no third-party build or runtime dependencies.

The v1 encoder emits valid stored DEFLATE blocks and does not attempt entropy
compression. Inflate requires the caller to provide the exact decompressed
size. The stored encoder writes into a caller-owned buffer sized with the
provided bound helpers. Buffers passed as input and output must not overlap.
Inflate may have written partial output if it returns failure.

LZMA, LZMA2, XZ, and 7z filter APIs will be added as Archive and Audio migrate
to this package in the next implementation steps. ZIP, PNG, PDF, SVGZ, TIFF,
and Audio container parsing remain in their domain libraries.

Build, test, and install with CMake:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    cmake --install build --prefix install

Consumers use find_package(QuirewiseCompression CONFIG REQUIRED) and link
Quirewise::Compression.
