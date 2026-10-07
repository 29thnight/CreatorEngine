# DxTimingCaptureLibrary provenance

This directory vendors a source-only subset of Microsoft's DxTimingCaptureLibrary with local
parser bounds hardening documented in LOCAL_PATCHES.md.

- Upstream: https://github.com/microsoft/DxTimingCaptureLibrary
- Pinned commit: `05bf5ff1d9e1b1d63c7420a166e61ded2ca7c072` (2026-09-30)
- Retrieved: 2026-10-06 through the GitHub connector, using the pinned commit for every file
- Included: public headers, static-library implementation and upstream project, common build properties, the PIX decoder stub and decoder headers, DXGK/DirectStorage ETW definitions, README, MIT license and third-party notices
- Excluded: sample applications, tests, GoogleTest, Perfetto, ImGui, and all prebuilt binaries
- Verification: all 48 upstream files matched their upstream Git blob SHA-1 values after initial materialization; see SOURCE_MANIFEST.json for that upstream baseline
- Local changes: four upstream headers were subsequently hardened; LOCAL_PATCHES.md records the changed paths and current hashes. The upstream manifest intentionally continues to describe the original pin

## Licensing

Retain LICENSE and THIRD-PARTY-NOTICES.md when redistributing this subset. The notices describe the complete upstream repository, including sample/test dependencies not included here. Included decoder headers, DXGK ETW definitions and DirectStorage ETW definitions have their own upstream provenance files and use the repository's MIT license. Microsoft SDK/NuGet packages retain their separate terms and are not relicensed or bundled by this directory.

## Build dependencies and intentional limits

The static library requires Windows x64, C++20, a suitable Microsoft C++ toolchain, Windows SDK headers, and Direct3D Agility SDK headers including D3D12Events.h. Upstream pins Microsoft.Direct3D.D3D12 1.619.5 and Windows SDK 10.0.26100.0. No package was restored or installed, and no build, test, capture, or engine execution was performed while fetching these sources.

Use an explicitly configured official SDK include directory for the CreatorEngine optional integration. The unchanged upstream Directory.Build.props documents a NuGet-based standalone flow; it is not authorization to run or install anything automatically. The static core uses the bundled DirectStorage ETW definitions; the upstream DirectStorage runtime package is relevant to its omitted samples/tests.

The included PixEventDecoderStub.cpp deliberately drops PIX timing/marker blocks. Other DirectX ETW categories remain available. Do not claim PIX user marker names or CPU/GPU PIX scope reconstruction until the real decoder is integrated and verified.

TimestampConverter output is nanoseconds, even when incoming ETW timestamps are raw QPC ticks. Do not use an identity converter: PerProcessData.h adds GPU nanosecond deltas to the converted CPU timestamp. Preserve an explicit timestamp domain and origin at the capture-format boundary.

Elevation is blocked in CreatorEngine's first integration. The local guards are source-only fixes
for issues found during static review, not a complete decoder audit or a reproduced exploit report.
Do not remove that safety gate based solely on the presence of these patches.
