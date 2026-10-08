# ownership_cpp provenance

- Upstream: https://github.com/29thnight/ownership_cpp
- Pinned commit: `5889a32c58a49fcfded3a9d61484867849cc7dbb`
- Retrieved: 2026-10-08
- License: MIT; see `LICENSE`
- Vendored contents: `include/own/ownership.hpp` and `LICENSE`, unmodified

All engine code includes `Engine/Utility_Framework/Ownership.h`. That header selects
this exact source without build-time downloads, global include paths, or per-project
configuration. The upstream default debug-thread and unsafe-access diagnostics remain
unchanged. Do not define different ownership configuration macros in different engine
translation units.

This initial integration has only been inspected statically. No upstream or engine
build, test, benchmark, binary, or shader compilation was run for this change.

## Update procedure

1. Inspect the exact upstream commit, public API and license.
2. Replace both vendored files from that commit without local modifications.
3. Verify their byte hashes against the pinned source and update this record.
4. Audit factory, borrowing and thread-boundary compatibility before migrating users.
5. Run the upstream and engine regression gates only when execution is authorized.

GPU lifetime continues to use the engine's completion-token and quarantine contracts.
CPU ownership and upstream retirement hooks are not GPU-completion evidence.
