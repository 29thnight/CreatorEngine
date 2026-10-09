# GCCE provenance

- Upstream: https://github.com/29thnight/Garbage-Collector-for-CreatorEngine-GCCE_CPP-
- Pinned commit: `aef79b3f57861fc16d0045e860e9082eeed8ec29`
- Retrieved: 2026-10-09
- Upstream project version: `0.1.0` (from the pinned `CMakeLists.txt`)
- Vendored scope: `include/gc/`, `src/`, and `CMakeLists.txt`, byte-for-byte unmodified
- License: **unspecified**. No upstream LICENSE was supplied at this pin. The source owner
  explicitly authorized this integration; that authorization does not invent a license,
  grant general redistribution rights, or relicense the upstream code. Resolve applicable
  redistribution terms before distributing this dependency beyond the authorized scope.

## Build and deployment policy

`Engine/GCCE/GCCE.vcxproj` compiles only `src/domain.cpp` and `src/block_allocator.cpp`
into `gcce.dll` with C++23 and the shared CRT (`/MDd` Debug, `/MD` Release). It does not
build upstream tests/benchmarks, fetch dependencies, or invoke CMake. The copied CMake
file records upstream build intent; the omitted tests and benchmarks are not an
independently buildable complete upstream checkout.

All engine native translation units receive `GCCE_SHARED` and the same explicit
`GC_DEBUG_CHECKS` (`1` Debug, `0` Release) from `Directory.Build.targets`.
`GCCE_BUILDING` belongs only to the GCCE implementation project. Consumers must link
its import library, never compile these two sources into their own host DLLs. GCCE
owns process-wide registry/thread state, so independent static copies are invalid.

The DLL is built once per architecture/configuration in
`Bin/<platform>-<configuration>/Runtime/Common/gcce.dll`. GCCE deliberately ignores
`EngineShipping`: a distribution combines Development Editor and Shipping Player
manifests and requires identical shared DLL bytes. The engine's existing ASan switch
still applies. GCCE has no engine reflection, vcpkg, SceneRuntime, or Editor dependency.

`Tools/runtime/EngineHost.targets` supplies the DLL path to `deploy-runtime.ps1`,
which records GCCE and its import closure in each Editor/Player runtime manifest.
Shipping staging copies the exact same DLL. Existing `EnginePublisher` and
`GamePackager` consume these manifests; the provenance is included by the publisher's
existing `Licenses/ThirdParty` collection. There is no invented license file.

Engine-facing code includes `Engine/SceneRuntime/SceneGC.h`. Assets retain their
separate `Ownership.h` ownership contracts. See [integration plan](../../docs/design/GCCEIntegration.md).

## Exact source hashes

Git blob IDs below were checked against the pinned upstream tree; SHA-256 covers
raw file bytes. `.gitattributes` preserves LF on Windows checkouts. No upstream
formatting or implementation edits belong in this integration.

| Path | Upstream Git blob SHA-1 | SHA-256 |
|---|---|---|
| `CMakeLists.txt` | `9dc0603186d2049cd6161dfd81f83de546bdfbcf` | `602f5efb27e74f24e3fb03feb52cccece252f161003dc82bcd4a84d52091ddfb` |
| `include/gc/config.hpp` | `1d16b22a335f95b5f6633e52e2857768a9e75b03` | `db3e69f428054e9fc9933e22332f4fb06b5541387910945e36aa475d9c40fb8f` |
| `include/gc/detail/header.hpp` | `5e0ef12fdace042267b0b95149beed89e9262035` | `feca8d19de8ab0ae7789341d22cfb9a5b3b0320e39aee6c018a34c4dc5129fd8` |
| `include/gc/detail/testing.hpp` | `82056ecf3231040064daf74a57d94a4c3c13e893` | `4a4be596c1db16bed6fa3db61003ad3f29e272181f232d87ffafa59c8b900168` |
| `include/gc/domain.hpp` | `9560d0c1f1a65f80e71668ccbbae34d65593e7d8` | `f44379dd05fe00b75ea77d386cf553720e10abc263a614c22d116f9e1151bff0` |
| `include/gc/gc.hpp` | `553f8dcb92bb3c38abfc419524b7bd5928f15602` | `7cad3ccfe898415ad6005dc08da2b96117eb51f90c9b274de16d3a45656ee8f4` |
| `include/gc/lifecycle.hpp` | `8ba965b12e7e4f137abd0b928a5ff88de1c1421a` | `ae30e3b4257b2f0a2f527dfedfbc5cd83df97d9c7ee9d747ba172902d47892b5` |
| `include/gc/make.hpp` | `1d7dd8a644f6f4932f3e479d59018dbe7e775a2c` | `09ed32271d83b3d33d2020c7f91890a0c1ce1ce2a62711c522ef24b980729d28` |
| `include/gc/managed.hpp` | `543b01e5e1b64d97f132898b4aea8a15a6c56e53` | `2ecf0710f82b52357219b7ef989f537f9a12d875514ffbe19da7f8ae59a3e7b0` |
| `include/gc/pinned.hpp` | `1e2fefa1299a553a57eafacef524c13c4499aaa7` | `7065ba0abd58c8b9eec14c63a0d7bca31fba981ef2d42c2a1c9c750a719de8a6` |
| `include/gc/refs.hpp` | `1e0ade64394584a1d03c2794922eea4595023b41` | `be8ecb0d458fd4e472f906ed70f184a0b122bc82ca344ee2b1f1c5bd48fa9655` |
| `include/gc/tracer.hpp` | `cfe75bc0531ab0a997d9093c28721c56b4ddd03f` | `43766042b5913bab2d44ff369e614a92c677a94fed848f703f7eafe9410c4f6a` |
| `src/block_allocator.cpp` | `3f5626fc5cd0b19c8f4f9505f9ebfb55ab7cc156` | `a63a20d0b4821e64524ebfd18088d85861a5f07290d8f1d5fc88588c721beba9` |
| `src/block_allocator.hpp` | `5886ed0791f837288b870a2e8555be0d31cde28d` | `f8536dd91306e0032cc550f71cb90383b23309be219f111757faeb30960232fc` |
| `src/block_allocator_testing.hpp` | `d2e0b9955d909a5d93276c5dbfbc948c02264a40` | `1da41b16baad0a54ee6b29d041baac425bd14861a8b45a0a3199a4e2fe846851` |
| `src/domain.cpp` | `8fbb97dc4b07f083ac3104f2d62cdbe356313706` | `50b1f9cad80feb9e519f13499529021e77916a06dcb7076f79546e8de67e1074` |

## Update procedure and validation limits

1. Review a specific upstream commit, its API/ABI, ownership protocol, and license status.
2. Replace the selected source files without local modifications; update every hash and pin.
3. Recheck C++23/toolchain requirements, DLL exports, debug configuration, graph tracing,
   domain lifetime, thread affinity, module unload, and distribution closure together.
4. Perform permitted static inspection first. Run upstream/engine builds and runtime
   regression gates only when execution is authorized.

This dependency slice has only static source/hash, project/XML, and packaging review.
No build, test, shader compilation, NativeAOT publish, engine execution, or benchmark
was run. Runtime and Windows linker validation remain pending.
