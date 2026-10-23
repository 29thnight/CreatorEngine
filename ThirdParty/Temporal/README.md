# Temporal SDK installation

Installed for the TR2 route, resolution and multi-view regression on 2026-10-23.
SDK source trees, downloaded archives and build outputs are local dependencies
and are excluded by this directory's `.gitignore`.

| SDK | Local directory | Pinned version / revision |
| --- | --- | --- |
| AMD FidelityFX | `FidelityFX-SDK` | v1.1.4 / `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` |
| NVIDIA Streamline source | `Streamline` | v2.14.1 / `2122257e0fce486f91b385aa63b9a09b0a34b363` |
| NVIDIA Streamline runtime | `StreamlineRuntime` | v2.14.1 release archive |
| Intel XeSS | `XeSS` | v3.0.2 / `8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0` |

Official sources:

- https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4
- https://github.com/NVIDIA-RTX/Streamline/tree/v2.14.1
- https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1
- https://github.com/intel/xess/tree/v3.0.2

The Streamline runtime archive is `.downloads/streamline-sdk-v2.14.1.zip`.
Its SHA-256 is `92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B`.
SDK license files remain in their respective installation directories.

`Directory.Build.props` and `Directory.Build.targets` isolate external SDK builds
from CreatorEngine's inherited module, reflection and compiler settings.

## Opt-in engine build

Use VS 2026 / v145 and import `Validation.props` through
`/p:ForceImportAfterCppProps=<absolute-path>/ThirdParty/Temporal/Validation.props`.
This enables all three upscalers and selects the local SDK paths and Debug or
Release FidelityFX libraries. The normal engine build defaults are unchanged.
`Build-Validation.ps1 -OutputDirectory <new-evidence-directory>` builds both
configurations, deploys the SDK runtime DLLs and records the engine binary hashes.

`Apply-Patches.ps1` first verifies the pinned FidelityFX revision and applies
`patches/fsr-exposure-precision.patch`. The patch marks the luma reduction and
exposure recurrence result as `precise`. Debug Editor testing found different
exposure history values for identical inputs; the patched shader passed the
1458×592 seven-route solo/interleaved and independent solo-repeat comparisons
(140 full-image comparisons each, maximum error 0). Full configuration matrices
are recorded separately in the TR2 validation evidence.
The guard accepts an already patched source, rejects unknown source edits, and
checks the original, patched and patch-file hashes from `SDKVersions.json`.

FidelityFX is configured in `FidelityFX-Build` with the DX12_X64 backend and
`FFX_FSR3UPSCALER`, `FFX_FI`, `FFX_OF`, `FFX_API_DX12` and
`FFX_AUTO_COMPILE_SHADERS` enabled. Build `ffx_fsr3upscaler_x64`,
`ffx_opticalflow_x64`, `ffx_frameinterpolation_x64` and `ffx_backend_dx12_x64`
for Debug and Release. Its build must disable inherited directory properties
and targets (`ImportDirectoryBuildProps=false`, `ImportDirectoryBuildTargets=false`).

Runtime DLLs are deployed to `Bin/x64-<Configuration>/Editor/Temporal`.
The FidelityFX PIX runtime is deployed beside `CreatorEditor.exe`.
Set `CREATOR_TEMPORAL_RUNTIME_DIRECTORY` to that Editor/Temporal directory when
running SDK validation. The TR2 regression controller sets it automatically.
