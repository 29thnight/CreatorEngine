# ownership_cpp provenance

- Upstream: https://github.com/29thnight/ownership_cpp
- Pinned commit: `3574d238d8000fb21825e5391047da7337ac7529`
- Retrieved: 2026-10-22
- License: MIT; see `LICENSE`
- Vendored contents: `include/own/ownership.hpp` and `LICENSE`, unmodified upstream source (Windows Git CRLF checkout).
- Windows checkout header SHA256: `c17c871d25ea68c3d6f4284c19a22240d825632b3017ea643081be239293a6e7`

- Upstream LF blob SHA256: `2c11f5adf977bb1a407fe7a8315bb2e2a5ef00bb19016dabc7d4caf9760d893f`
- The checkout and upstream Git blob match after CRLF/LF normalization.

Engine code includes `Engine/Utility_Framework/Ownership.h`. Builds consume this
source directly, without vcpkg, FetchContent, submodules or build-time downloads.
The upstream configuration defaults are unchanged across engine modules.

The renderer uses `shared_owner`, `weak_owner` and `unique_owner`. CPU borrows do
not extend lifetime. Asynchronous jobs, RHI pending records and GPU retirement
entries retain owning handles until their existing completion/cancellation gates.
`LifetimeToken.h` erases the payload type in fixed inline storage while keeping a
real ownership_cpp strong/weak handle. It does not allocate or replace GPU fences.

Validation evidence: `Artifacts/gpu-marker-blob-20261022`.
