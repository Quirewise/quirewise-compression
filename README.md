# Quirewise Compression

Quirewise Compression is an independently installable C11 library for raw
RFC 1951 inflate, RFC 1950 zlib streams, CRC-32, Adler-32, stored-block
encoding, and 7z coder chains using LZMA, LZMA2, Deflate, and supported 7z
filters. CMake discovers LibLZMA and exports it as a transitive dependency.

The v1 encoder emits valid stored DEFLATE blocks and does not attempt entropy
compression. Inflate requires the caller to provide the exact decompressed
size. The stored encoder writes into a caller-owned buffer sized with the
provided bound helpers. Buffers passed as input and output must not overlap.
Inflate may have written partial output if it returns failure. 7z decoder
memory usage is checked against the caller's supplied limit before decoding.

The 7z coder-chain API supports LZMA1, LZMA2, Deflate, Copy, Delta, and the
x86, PowerPC, IA64, ARM, ARM Thumb, and SPARC branch filters. ZIP, PNG, PDF,
SVGZ, TIFF, and Audio container parsing remain in their domain libraries.

Build, test, and install with CMake:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    cmake --install build --prefix install

Consumers use find_package(QuirewiseCompression CONFIG REQUIRED) and link
Quirewise::Compression.
