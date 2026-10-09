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

## Adapter-only checkpoint (superseded by live integration below)

At the first adapter-only checkpoint, the live LX producer lacked complete previous-object/skinning motion inputs, jitter and independently propagated render/display extents. The neutral operation boundary does not manufacture those values. Live PostChain insertion and FSR/native fallback reconfiguration were not connected by that slice.

That checkpoint still needed complete Player temporal input packets and retention through SDK final consumption, proxy lifecycle/pacing ownership, game-loop latency markers, HUD-less/UI composition and real/generated measurement separation. SDK adapter operations alone cannot activate a product FG path. DLSS 2.14.1 also distinguishes SDK-owned pacing from application-owned frame-latency waitable objects; its HDR FG path supports PQ rather than FP16 scRGB.

Source review is not a compile, link, shader, pixel, timing or hardware-support result. No such execution is performed here. The phase's 16-row/71-day accounting and TR/TU/FG acceptance remain open; Vulkan execution comparison remains owned by PHASE 4.9.

## Pinned FSR callback failure workaround

SDK 1.1.4's [DX12 callback caller](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12.cpp#L1383-L1411) and [Vulkan callback caller](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK.cpp#L2031-L2102) discard generated commands on callback failure but can still present the interpolation output when the descriptor's frame count remains positive. Their descriptor is a mutable local passed through a const-qualified callback API.

The adapter's failure path sets that known mutable descriptor's generated-frame count to zero, preserves the failure result and invalidates history, rather than presenting an unwritten/stale generated image. This is specific to the pinned source implementation and must be re-audited on any SDK change. No SDK source or binary is patched in this repository. It is not evidence of executed presentation recovery.

## Live structural integration and staged acceptance commands

The follow-on source change connects the existing adapters to the actual product boundaries rather than treating SDK factories as an active feature:

- `TemporalUpscalerHost` is per logical view. It queries the initialized SDK for its render extent before graph sizing, records the selected SDK operation before PostChain, and latches evaluation failures. A failed requested provider can select FSR; a failed FSR context returns to full display-resolution rendering. An unwritten failed evaluation output is not published as an upscaled frame.
- Actual LX scene inputs retain stable object/incarnation/instance identity, previous world transforms and previous skin inputs. Temporal raster/material paths emit current-to-previous motion and separate transparency/reactive/responsive masks; camera-only background motion is initialized separately. View history/jitter advance on submitted frames, with scene/view/history/size/settings resets.
- Renderer and display extents, unjittered camera transforms, real-frame identity and exact submission provenance are carried separately. Golden/pixel capture holds a runtime native-only exclusion scope, retains user intent, and rejects missing or nonnative provenance. GPU profiler samples retain the provenance of their own submitted frame rather than sampling a newer global status.
- DX12 Player exports/imports the complete GPU temporal packet through dedicated committed shared textures and a common consumer lease. SDK proxy ownership, SDK pacing, frame-start/input/simulation/submission/presentation markers and final-consumption draining stay in Player/RHI; Editor does not acquire the FG presenter.
- Runtime controls submit a generation-tagged request. The renderer and Player acknowledge independently only after observing their own live work. Capability, selected provider, active provider, SDK operation failure, renderer submission, GPU completion, CPU Present return and SDK final consumption remain different observations.
- FSR generated callback outputs completed after draining are counted separately from vendor-reported native presentations. Requested interpolation counts, CPU Present returns and display refresh rates never synthesize real-frame FPS or generated-output identities.

See [staged validation commands](../../Tools/temporal-validation/README.md). The command/script sources are authored for subsequent controlled execution; they have **not been run** in this source-only task. A command request or successful capability query is not an executed frame, successful native presentation or pixel/timing acceptance.

### Startup device prerequisites

Set `CREATOR_TEMPORAL_UPSCALER` / `CREATOR_TEMPORAL_FRAME_GENERATOR` to `none`, `fsr`, `dlss` or `xess` before startup when a selected SDK needs pre-device integration. `CREATOR_TEMPORAL_RUNTIME_DIRECTORY` is an absolute trusted authorized runtime directory, and `CREATOR_TEMPORAL_DLSS_PROJECT_ID` supplies the real application project identity. No fabricated application ID is provided.

For a temporal startup, DX12 renderer and Player resource owners share one native device while preserving separate queues/resource tables. Streamline initialization precedes factory/device creation; its factory/device proxies are used before queue/swapchain creation. Late selection cannot retroactively hook an existing unprepared device and reports integration-required rather than false support. The shared owner drains/destroys SDK contexts before releasing proxies/native COM owners and unloading SDK code.

XeSS Vulkan requirements are queried before instance/device creation. The existing writable `VkPhysicalDeviceFeatures2` chain is passed to the official SDK patch API, with duplicate/cyclic chain rejection and native device fallback if optional requirements cannot be enabled. A runtime directory change does not pretend an existing Vulkan device enabled a different runtime's requirements.

### Anti-Lag 2 optional latency integration

The additional optional source-header pin is [Anti-Lag 2 SDK v2.0.0a](https://github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK/tree/390aa4a8c8655d0ae6e90079db2c85e103a96da3), MIT. `CreatorEnableAntiLag2=true` and `CreatorAntiLag2Root` select its external root `ffx_antilag2_dx12.h`. Keep the SDK's copyright/license notices in any downstream distribution. No driver DLL is copied or loaded from an arbitrary path: the official inline header queries the already loaded AMD driver module.

Availability is the result of `AMD::AntiLag2DX12::Initialize`, never a vendor-ID guess. `Update` belongs immediately before real input polling; `MarkEndOfFrameRendering` follows actual main-work submission; the pinned FSR 3.1.4 replacement receives the official context private-data ABI so its own presentation thread marks real/generated frame types. The same persistent context/device must survive proxy final consumption. Unsupported latency control is diagnosed independently from usable FSR frame generation.

### Explicit non-acceptance and API boundary

No build, compiler, shader, native engine, regression, pixel or benchmark execution was performed. The PHASE 4.5 TR/TU/FG acceptance rows remain open pending their DX12 Debug/Release evidence, including motion-route fixture vectors, native-scale pixel equivalence, mutation rejection, generated identity, resize/device-loss/teardown and timing/latency gates.

Vulkan SR has its native adapter/device requirement path. The current Vulkan Player uses a CPU display bridge and does not provide GPU temporal-packet transport or the reserved presentation queue needed by FSR's native replacement. Vulkan Player FG therefore remains explicitly integration-required (`NativePresentationInteropUnavailable`); this is not a claim of unsupported GPU hardware or absent Vulkan SDK support. No CPU readback/reupload FG substitute is introduced. Vulkan runtime comparison/parity remains PHASE 4.9, and this source change does not claim a complete Vulkan Player FG path.

DirectSR is not selected as an additional provider in this change: the requested pinned DLSS/FSR/XeSS SDKs already implement the neutral operation boundary, while adding a separate platform service would not replace the Vulkan implementation requirement. This is an implementation/scope decision, not a claim about the current availability or maturity of DirectSR releases.

Orthographic views retain native rendering and temporal provenance. The current SDK-facing projection path is explicitly `ProjectionUnsupported` for TU/FG instead of inventing a perspective FOV; native camera/motion/history code accepts a real orthographic camera with zero perspective FOV. Perspective SDK support is still established only by actual device/context queries and successful operations.
