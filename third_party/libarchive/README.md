# Vendored archive backend

This directory contains the static libraries used by `TerminalArchive.dll`.
They are built once and committed so users do not need CMake, zlib, xz or
Bandizip installed.

- `libarchive` 3.8.1 — BSD-2-Clause; <https://github.com/libarchive/libarchive>
- `zlib` 1.3.1 — zlib License; <https://zlib.net>
- `liblzma` 5.8.1 (xz) — public domain / LGPL-2.1-or-later components; <https://tukaani.org/xz/>

Headers are in `include/`, MinGW x86_64 static archives in `lib/`.

`libarchive.a` was configured with:

```text
BUILD_SHARED_LIBS=OFF
ENABLE_ZLIB=ON
ENABLE_LZMA=ON
ENABLE_BZip2=OFF
ENABLE_ZSTD=OFF
ENABLE_LZ4=OFF
ENABLE_OPENSSL=OFF
ENABLE_LIBXML2=OFF
ENABLE_EXPAT=OFF
ENABLE_TEST=OFF
ENABLE_TAR=OFF
ENABLE_CPIO=OFF
ENABLE_CAT=OFF
ENABLE_UNZIP=OFF
```

The DLL links `bcrypt` and `xmllite` from Windows for crypto and XAR support.
