# PHASE 4.5 vendor backend integration — 2026-10-09

## Scope and order

The term backend in this work means the DLSS, FSR and XeSS implementations of temporal upscaling and frame generation. DX12 and Vulkan are graphics APIs below that boundary.

1. Inspect and pin the actual SDK interfaces, feature queries, resource requirements and presentation/lifetime contracts.
2. Define independent, graphics-API-neutral upscaler and frame-generation contracts from those requirements.
3. Implement the vendor adapters and their explicit optional-build/unsupported states. Keep graphics API native types inside implementation boundaries.
4. Connect the required renderer inputs and presenter lifecycle in follow-on coherent slices. An adapter without valid live inputs is not a completed product feature.

Do not add a generic provider framework or global synchronization solely for anticipated use. Reuse the existing RHI, RenderGraph and Player presenter ownership boundaries. Capability depends on the selected SDK, API and runtime query, not vendor ID alone. Upscaler and frame-generation selection remain independent.

## Starting point

Inspected master: `117b729a60acf9663dd6f41fc9b71f4ce6f60e00`.

BASE-0 has existing provenance and mutation checks. Actual LX GBuffer has no velocity output; current transform and skin pose do not supply previous-frame motion. Render/output extents and jitter/history contracts need integration. Player presentation currently transfers a color texture and its host-fence lifetime, not the complete set of temporal inputs through provider final consumption.

Existing native presentation or a successful support query does not prove generated frames, pixel correctness or latency improvement. Editor viewport live_present is not the FG insertion point.

## Verification policy

This work is code and static review only. No builds, compiler or shader execution, tests, native engine runs or benchmarks are authorized in this slice. Separate declared support, adapter implementation, live product wiring and executed validation in every status report.

SDK versions and verified source links, build/package requirements, implemented operations and remaining input/lifetime gaps will be recorded with the implementation. Do not claim unsupported SDKs work, fabricate ABI declarations, silently load arbitrary DLLs, or accept new SDK legal agreements without authorization.

DX12 Debug/Release is the plan's execution acceptance target; graphics-API-neutral contracts remain required. Vulkan runtime parity belongs to PHASE 4.9.

## Pinned SDK decisions

The final user choice for FSR is **FSR 3.1.4**, not the newer binary-only FSR SDK route. DLSS and XeSS use the latest stable SDK releases verified through their official release APIs for this change.

| Provider | Header/source pin | Deployment and API boundary |
|---|---|---|
| FSR | [FidelityFX SDK v1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55), FSR 3.1.4 | Externally source-built MIT SDK. DX12 and Vulkan specialization sources; select one FSR static backend per binary because those SDK backend libraries export colliding common symbols. This does not restrict the engine itself to one graphics API |
| DLSS | [Streamline v2.14.1](https://github.com/NVIDIA-RTX/Streamline/tree/2122257e0fce486f91b385aa63b9a09b0a34b363) | External signed NVIDIA runtime. Initial concrete adapter is DX12; SDK Vulkan support does not mean the engine adapter implements Vulkan yet |
| XeSS | [XeSS SDK v3.0.2](https://github.com/intel/xess/tree/8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0) | External Intel runtime. SR has DX12/Vulkan APIs; FG/XeLL uses DX12. Runtime interpolation limits distinguish supported 2x from additional MFG modes |

The XeSS release search index initially returned 3.0.0; the official [latest-release API](https://api.github.com/repos/intel/xess/releases/latest) identified 3.0.2, released July 24, 2026. Source signatures are checked against the pinned 3.0.2 headers. The [Streamline latest-release API](https://api.github.com/repos/NVIDIA-RTX/Streamline/releases/latest) identified 2.14.1. These are recorded observations, not an automatic future upgrade policy.

No SDK binaries or third-party headers are added to this repository. No SDK agreement is accepted, and no DLL is installed, copied, or executed by this change. A downstream deployment must supply its authorized SDK distribution and preserve its applicable notices. Source/header SDK identity and runtime-selected feature implementation are different facts.

## Public contract and private execution boundary

`Render/Temporal/TemporalReconstruction` defines independent capability-result selection, frame metadata and input validation. `ITemporalUpscaler` and `ITemporalFrameGenerator` expose graph recording/preparation operations using existing RHI handles and the graph callback's active encoder. Backend-private factories bind SDK/device contexts and resource resolution; renderer callers do not include SDK headers.

Selection means an eligible provider, not a running feature. Context creation, valid inputs, graph insertion and presenter ownership must still succeed. A failed requested provider can fall back to an actually supported FSR candidate on the same graphics backend, then native rendering or real-frame-only presentation. `None` does not mean stretching a low-resolution image and labeling it an upscale result.

Input conventions are current-to-previous motion in pixel units, top-left origin, right/down positive, including camera motion and excluding jitter. Jitter is in render-resolution pixels. Render/display extents, real-frame ID, history revision and reset are separate. Color reconstruction operates on linear HDR before tone mapping/UI. FG uses a separate HUD-less/UI input description and is forbidden for Editor viewports.

Vendor recording can alter native pipeline/descriptor state. The private bridge uses the exact graph encoder, rejects another device's encoder, and invalidates binding caches without clearing graph-owned layout history. Queue submission and graph resource retirement are not replaced.

A prepared FG frame is not a released frame. Native proxy presentation, SDK final-consumer fences, input retention, resize and teardown belong to the shell adapter/integration. Host fence completion and CPU `Present` return cannot stand in for SDK final consumption. FG input packets therefore require the existing graph/packet lifetime owner, retained by the adapter; native texture references alone cannot prevent placed-memory aliasing or pool reuse. The first adapter slice uses bounded serial retention and an explicit final-consumption drain callback. It is not an optimized pipelined FG implementation or a latency claim.

## Optional source build inputs

All vendor switches default off in `Engine/RenderEngine/RHI/Temporal/TemporalSdk.props`. Pass these properties consistently to the whole solution, including final executable/DLL consumers; a property set only on RenderEngine would not activate the FSR Vulkan final-link delay-load rule. Disabled capability queries return an explicit unavailable result; they do not report synthetic support.

| MSBuild property | External inputs |
|---|---|
| `CreatorEnableFsrDx12=true` or `CreatorEnableFsrVulkan=true` | `CreatorFsrRoot` points to SDK 1.1.4 source root; `CreatorFsrLibraries` contains full paths to compatible externally built SDK and import libraries |
| `CreatorEnableDlssStreamline=true` | `CreatorStreamlineRoot` points to complete Streamline 2.14.1 source/header tree, including the `sl_appidentity.h` and `sl_device_wrappers.h` symlink targets |
| `CreatorEnableXeSS=true` | `CreatorXeSSRoot` points to XeSS 3.0.2 SDK root; enables DX12 SR/FG/XeLL adapter sources |
| `CreatorEnableXeSSVulkan=true` | Same XeSS root; enables Vulkan SR adapter sources, not Vulkan FG |

FSR source library set is `ffx_fsr3upscaler_x64`, `ffx_opticalflow_x64`, `ffx_frameinterpolation_x64` and exactly one of `ffx_backend_dx12_x64` / `ffx_backend_vk_x64`. SDK Debug artifacts append `d` and RelWithDebInfo artifacts append `drel`; release artifacts have no suffix. Use compatible architecture/configuration/CRT and include the backend's required external import libraries, including its official PIX import library when applicable. The build property takes explicit paths rather than guessing a PIX installation or invoking the SDK build. MSVC library combination and final linking remain unexecuted under this task's no-build instruction. The SDK Vulkan backend also links the official Vulkan loader import library; include it in the explicit library list. Final EXE/DLL links delay-load `vulkan-1.dll` when `CreatorEnableFsrVulkan` is enabled, preserving the engine's existing ability to launch DX12 without a Vulkan runtime installed. The FSR Vulkan factory requires the existing Vulkan loader to be ready before any SDK call.

DLSS/XeSS private loaders receive an explicit absolute trusted runtime directory from the host. They do not search the current directory or PATH, download a runtime, or treat a missing export as supported hardware. FSR source linkage introduces no automatic runtime download. Actual deployment and GPU execution are separate acceptance steps.

## Implemented source operations

| Provider | Upscaling operations | Frame-generation operations |
|---|---|---|
| FSR 3.1.4 | Real device/effect queries, independent context/resource creation, quality-derived render extent, DX12/Vulkan RHI-handle resolution, SDK dispatch, history invalidation and drained destruction | Independent Optical Flow/Frame Interpolation contexts, Player replacement swapchain creation for both native backends, depth/motion preparation, HUD-less/UI registration, SDK-generated output dispatch, native presentation and serial final-consumption draining |
| DLSS / Streamline | Verified runtime loading, pre-device initialization/proxy hooks, runtime feature query, optimal resolution, frame tokens/constants, resource tags, SR evaluation and independent SR cleanup | Player proxy binding/pacing ownership checks, FG options/tags, Reflex sleep and latency marker entry points, presentation-status/final-input-fence query and drained FG cleanup; generated work remains in Streamline's proxy Present hook |
| XeSS | Actual component API version query, DX12/Vulkan context creation and input-range queries, velocity/exposure mapping, SDK execute, RHI bridges and drained destruction | DX12 proxy creation, XeLL sleep/markers, runtime interpolation-count limits, HUD-less/UI/depth/motion tagging, native Present/status, disabled real-frame passthrough, resize/settings lifecycle and retained-input draining |

SDK-disabled builds contain explicit unavailable paths. SDK-enabled branches contain the real calls; their presence is not a successful compile or a GPU support result. DLSS Vulkan and XeSS Vulkan FG report unsupported rather than pretending parity. XeSS Vulkan's current neutral bridge accepts the existing single-mip, nonarray 2D view contract and rejects combined depth/stencil views.

SDK execution failures invalidate SR history or latch an explicit recreate-required fault before another frame can continue. Recording failure also invalidates native binding caches and cannot release pending FG input owners. Failed GPU-drain/destruction paths preserve or quarantine the still-owned state instead of unloading code or freeing memory that a GPU/SDK callback may still use.

## Remaining integration and verification

The live LX producer still lacks complete previous-object/skinning motion inputs, jitter and independently propagated render/display extents. The neutral operation boundary does not manufacture those values. Live PostChain insertion and FSR/native fallback reconfiguration are not connected by this slice.

The Player shell still needs complete temporal input packets and retention through SDK final consumption, proxy lifecycle/pacing ownership, game-loop latency markers, HUD-less/UI composition and real/generated measurement separation. SDK adapter operations alone cannot activate a product FG path. DLSS 2.14.1 also distinguishes SDK-owned pacing from application-owned frame-latency waitable objects; its HDR FG path supports PQ rather than FP16 scRGB.

Source review is not a compile, link, shader, pixel, timing or hardware-support result. No such execution is performed here. The phase's 16-row/71-day accounting and TR/TU/FG acceptance remain open; Vulkan execution comparison remains owned by PHASE 4.9.

## Pinned FSR callback failure workaround

SDK 1.1.4's [DX12 callback caller](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12.cpp#L1383-L1411) and [Vulkan callback caller](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK.cpp#L2031-L2102) discard generated commands on callback failure but can still present the interpolation output when the descriptor's frame count remains positive. Their descriptor is a mutable local passed through a const-qualified callback API.

The adapter's failure path sets that known mutable descriptor's generated-frame count to zero, preserves the failure result and invalidates history, rather than presenting an unwritten/stale generated image. This is specific to the pinned source implementation and must be re-audited on any SDK change. No SDK source or binary is patched in this repository. It is not evidence of executed presentation recovery.
