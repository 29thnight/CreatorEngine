# PrSM runtime provenance

- Upstream: https://github.com/29thnight/PrSM
- Pinned commit: `6e380ba0e4933b37c9c45fe66f1d86473e336a8a` (branch `feat/creatorengine-integration`, 2026-10-19)
- License: Apache License 2.0 (`LICENSE`, `NOTICE`), copied byte-for-byte
- Vendored scope: the engine-independent script runtime only, byte-for-byte unmodified:
  - `abi/prsm_abi.h` — the module/host C ABI (authoritative upstream copy)
  - `abi/prsm_jit.h` — the embedded ORC JIT C ABI consumed by the editor
  - `harness/host/` — GC runtime adapter, handle table, floating-point scope,
    script boundary, task scheduler and bridge, reload object validation
  - `samples/mock_engine/prsm_task_bridge.h`, `prsm_reload_bridge.h` — the
    task/reload imports emitted by generated component modules

The mock engine registry, mock binding validators, reload coordinator, lifecycle
driver and module loader are **not** vendored. CreatorEngine supplies its own
host API (`Engine/SceneRuntime/PrsmHostApi.cpp`), engine bindings and lifecycle
driver (`Engine/SceneRuntime/Prsm*`).

## Build and deployment policy

`Engine/SceneRuntime/SceneRuntime.vcxproj` compiles the vendored runtime sources
with `PRSM_HOST_BUILD` into the SceneRuntime static library, so each host
(`CreatorEditor.runtime.dll`, `Player`) owns exactly one PrSM `GcRuntime` and
`HandleTable`. No separate script runtime DLL exists: the editor's ORC JIT
receives every runtime, task-bridge and binding address explicitly (D-047), and
the Player links AOT objects statically (D-048). The runtime uses the engine's
shared `gcce.dll`; PrSM pins the same GCCE commit
(`54dd96e6a4460f0db4bbdd518ea629db0cb85176`, see `../GCCE/PROVENANCE.md`), so
`gc_runtime.cpp`'s coupling to GCCE internals is satisfied by identical sources.

The compiler (`prsmc.exe`), the embedded JIT (`prsm_jit.dll`) and LLVM's
`LLVM-C.dll` are build tools, not vendored sources. They are deployed next to the
Editor from `EnginePrsmToolchainDir` (see `Engine/SceneRuntime/PrsmToolchain.targets`).

## Exact source hashes

Git blob IDs are the pinned upstream tree's; SHA-256 covers the raw (LF) bytes.
`.gitattributes` preserves LF on Windows checkouts.

| Path | Upstream Git blob SHA-1 | SHA-256 |
|---|---|---|
| `LICENSE` | `d645695673349e3947e8e5ae42332d0ac3164cd7` | `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` |
| `NOTICE` | `a2cbdb36794a51b33ffc4a5983a95a4f822e0ab6` | `ef736d4b151e8312f1abb9bed8f73facf5d266acbc19787a092bf596d378c301` |
| `abi/prsm_abi.h` | `1293d2dafb381671222f7f14027848fba5a35f5d` | `d89d6b9206ad28e47b9f5551764638d1dbbf85774433521cf7a209cb5f075e42` |
| `abi/prsm_jit.h` | `4de83e3c5c5aad0ee10ad4d110678db4a21698d1` | `ad5bfd034bf243ac19749616cf9edf866d9ab045c3722d9d207ffdf219a8dd65` |
| `harness/host/floating_point_scope.cpp` | `9ff0e4577c41d1092047f9e25426590451d6f453` | `cd538c4cfb0d2282637e8f373eb89550bb0438878f883049ff6447325211d4bd` |
| `harness/host/floating_point_scope.h` | `ef94d089e0f5bfcd7e398047116abe53321a3034` | `b846d9c1a408470dd1b0fc0fd2cb6c23b35c4efa69fd361a848c67e1d2096b28` |
| `harness/host/gc_runtime.cpp` | `f7d31bd795cd645c4aeb56a9a6d1d7295921d3d8` | `1d18c0445c1bca529934929d3b690dd7e38ea34cde2bb6acda80a00739bdfa4d` |
| `harness/host/gc_runtime.h` | `ba74029eafe782c0bcba02ef143af38b88f2f8e3` | `6931fcfdac11ee5e34657252dc0bb7ceebabcff7499660b632c9d7a20b0dcfa1` |
| `harness/host/handle_table.cpp` | `d9f1fa5f7b327e58d8cd44d68b0de53df6c57a9b` | `0f109f5a35c4008dfcd9a254ed7a6353d49c17c3b900784f7813b52f34d14212` |
| `harness/host/handle_table.h` | `2c6968dc2dc09e69ea4e95a5eff5abea1dfe86b5` | `1bff584360cd9a4b21c33fb0e14ca30dffd93f88f0ba3a3b4665318639b9d059` |
| `harness/host/reload_validation.cpp` | `bef7abd556c107ccb3b10142a3319306e50a0fc5` | `aad91491c746dbce31030b3a9fedc6f4f0de0c042402f0024ae22315a34e1ea4` |
| `harness/host/reload_validation.h` | `0367c41aa278638c971918a126e5e0e38b896527` | `9dc4ebf1b09c10406da2b9d25959f42620f6ad201f3a85db71f64c55d57df7f5` |
| `harness/host/script_boundary.cpp` | `d660a6a25d1781fb3216959bf2c9edadf3b9a538` | `76ba7e33a091852fad1cfca12cc1dbb3e451620b4627a80fe1dbbfe7c500fc18` |
| `harness/host/script_boundary.h` | `9179587dae4b5b579c144f907e16991b524c3d38` | `7189ca28bfd4dc1071546f1b33f627b39b0dc91d3c26be38a03a8eef137acbb0` |
| `harness/host/task_bridge.cpp` | `d421a29ed941f2bd7dd2db4ac1677450dc6a3991` | `7ffafc57cfad1f037311ca763edf0105115437ec3db0e44e2bbbcf530915bc89` |
| `harness/host/task_bridge.h` | `cee7f7d113220fed43523df561ed99d4ae7bde77` | `92c02f4934ad27c1b8f51be06eec06321ed1c2c9c30d29f85c19cceef666137c` |
| `harness/host/task_scheduler.cpp` | `e563313722495e722cdaf03d4339bc2c78a5202e` | `4cfcc0c9f825a4da1b1d67d50bdf1192416709f67a61b24eec20f5f1f519dd31` |
| `harness/host/task_scheduler.h` | `2877220772371122141d5667fc7395c5a70282e7` | `36b22b8aad4d613148931492b51360c901a62fbf3c80a04f4c92ad3cee05d407` |
| `samples/mock_engine/prsm_reload_bridge.h` | `ad9508d65954fd4bb4e37c026f89c7c1cbf9744d` | `d8c5f5cce09966c915517e30c7c8c573bdc7d5b4852066ba8b363a167f939ba1` |
| `samples/mock_engine/prsm_task_bridge.h` | `66c72923efce990ee776febbe474ee806e1b6926` | `b7a69782ae2fa299b439fdcbd2973ffd638b99c6d45f861ab911dae0267b4028` |

## Update procedure

1. Review the upstream commit's ABI/runtime changes (`abi/`, `harness/host/`).
2. Replace the files above from that commit with `git show <commit>:<path>`
   (never a CRLF working copy), then update the pin and every hash.
3. Rebuild SceneRuntime and run the PrSM integration scenario.
