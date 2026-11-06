# Vendored source record: LZMA SDK

- Component: LZMA SDK decoder subset (`LzmaDec`, `Lzma2Dec`, `XzDec`, `Xz`,
  `Bra`, `Bra86`, `BraIA64`, `Delta`, `7zCrc*`, `Sha256*`, `Alloc`, `CpuArch`,
  `Compiler.h`, `7zTypes.h`, `7zWindows.h`, `Precomp.h`).
- Origin: Igor Pavlov's LZMA SDK (7-Zip project).
- Version: file headers carry 2021-02-09 … 2024-03-01 dates. The workspace does
  **not** record an upstream release tag or commit pin, so this record does not
  claim one; the file headers are the authoritative version reference.
- License: **public domain**, as stated in every file header
  ("Igor Pavlov : Public domain").
- Why vendored: Quirewise decodes LZMA/XZ streams (XZ archives, 7z LZMA
  entries) with its own container code (`src/xz.c`, `src/seven_zip.c`) and only
  uses the SDK for the entropy decoder and filters. It is compiled into
  `Quirewise::Compression`; no external `liblzma` is found, linked or exported.
- Patches: none applied to decoder logic. `src/internal/lzma_sdk_allocator.c`
  is Quirewise code that binds the SDK allocator to Quirewise memory accounting;
  the SDK sources themselves are unmodified. No runtime symbol lookup
  (`LoadLibrary`/`GetProcAddress`/`dlopen`) is used by any compiled unit; the
  only textual occurrence is an MSVC `#pragma warning` comment in
  `Compiler.h` that is not code.
- Ownership: the SDK remains third-party code. It is private (`src/internal`),
  never installed, never included by a public header, and no Quirewise API
  exposes its types.
- Also used by: nothing else; `Quirewise::Compression` is the only consumer.
