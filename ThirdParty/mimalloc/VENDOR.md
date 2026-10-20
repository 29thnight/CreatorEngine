# mimalloc source dependency

Upstream: https://github.com/microsoft/mimalloc
Tag: v3.5.3
Commit: d4881d338125e1cb7c47ba4cfb398d6f7c0c8d45
License: MIT (LICENSE).

The upstream include/ and src/ directories are copied unchanged. RenderEngine
compiles src/static.c as C, outside unity/PCH, with MI_STATIC_LIB and without
MI_MALLOC_OVERRIDE. Only explicit renderer allocation APIs use this allocator;
CRT malloc/new are not replaced. No vcpkg package is used.
