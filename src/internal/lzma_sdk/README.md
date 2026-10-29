# Bundled 7-Zip LZMA SDK decoder sources

These files are a source subset of the official 7-Zip 26.05 release:

- Upstream: <https://github.com/ip7z/7zip/tree/26.05/C>
- Release: <https://github.com/ip7z/7zip/releases/tag/26.05>
- Source archive SHA-256: `d8c8c9dec136a0c5a7d7ce05bccef73173a6866641ba75764fbe32ecd614fa2a`
- Release commit: `b88c6bc`

The selected upstream files retain their Public Domain notices. The official
[7-Zip LZMA SDK page](https://www.7-zip.org/sdk.html) describes the ANSI-C
LZMA, LZMA2, and XZ decoder sources and their Public Domain status. The copied
files are compiled into `Quirewise::Compression`; they are not linked as an
external library. The `Z7_ST` build definition selects the SDK's single-thread
decoder implementation.

The adjacent `lzma_sdk_allocator.c` is Quirewise adapter code. It supplies the
SDK allocator interface and tracks live SDK allocations against each decoder's
configured memory limit. XZ CRC tables are initialized once before decoding.
