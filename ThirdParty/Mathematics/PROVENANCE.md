# Mathematics provenance

- Upstream: <https://github.com/29thnight/Mathematics>
- Pinned commit: `04c8bbe30272b3332716cec66cd35dc4d8cb8dbf`
- Retrieved: 2026-08-25
- License: MIT; see `LICENSE`
- Vendored contents: `include/mathematics/**` and `LICENSE`

Mathematics is header-only and is not available through the repository's pinned vcpkg
manifest. The source is vendored so every CreatorEngine build uses the same reviewed
headers without fetching from the network during configure or build.

## Update procedure

1. Inspect the upstream commit, public headers, release status and license.
2. Run the upstream MSVC Release tests, including DirectXMath/DirectXCollision parity.
3. Replace `include/mathematics/**` and `LICENSE` from that exact clean commit.
4. Update the pinned SHA and retrieval date in this file and `ThirdParty/README.md`.
5. Run both configurations of `Tools/regression/verify-mathematics-contract.ps1`.
6. Build the affected CreatorEngine vertical slices and run their regression gates before
   changing consumers.

Do not replace this pin with a build-time `git clone`, `FetchContent`, floating branch or
untagged `master` reference.
