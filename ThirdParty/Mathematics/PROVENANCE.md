# Mathematics provenance

- Upstream: <https://github.com/29thnight/Mathematics>
- Pinned commit: `1f43e080f180db1afbf6e18cb3849b758858a496`
- Retrieved: 2026-08-26
- License: MIT; see `LICENSE`
- Vendored contents: `include/mathematics/**` and `LICENSE`

The previous pin was `d81ca3338ef6f645cc5743625067eece5f1099f0`. Its direct
child adds the public easing/tween APIs and their scalar, rect and fixed-range support.
All 35 vendored headers and `LICENSE` were recopied from the clean
`1f43e080f180db1afbf6e18cb3849b758858a496` checkout and verified byte-for-byte.
The upstream MSVC Release suite passed 338/338 tests with zero failures before import.

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
