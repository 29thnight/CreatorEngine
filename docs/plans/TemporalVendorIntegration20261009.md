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

## Optional AA, spatial color and independent latency controls

The next source slice keeps the existing SDK pins and adds independently requested controls:

- DLAA / FSR Native AA / XeSS AA are the native-resolution quality of their temporal providers. A successfully submitted temporal reconstruction graph omits FXAA without changing its saved native/fallback preference. SDK recording failure discards the frame; it is not a same-frame FXAA fallback. A standalone engine TAA resolve is not added.
- NIS supports spatial scaling or sharpening after SDR post-processing and before UI. Already reconstructed temporal output is never scaled twice; a scale request then selects sharpen-only. NVScaler already sharpens, so no second NIS sharpening pass follows it. The Streamline adapter's actual capability query determines availability; the cross-vendor scope of the standalone NIS SDK is not a blanket support claim for this integration.
- DeepDVC is default-off, final-resolution SDR, after tone mapping and before UI. HDR requests report unavailable. Successful SDK command recording is observable; it is not pixel-level proof that an internally accepted NGX effect changed the image.
- Reflex Off / On / On+Boost is independent of TU and FG in the DX12 Player. DLSS FG imposes its required minimum mode; disabling FG restores the requested policy. There is one pacing owner, not a second sleep layered onto XeLL. An explicit XeSS FG startup request retains XeLL and disables optional Reflex; switching an already loaded incompatible runtime requires restart rather than silently selecting another FG provider.
- Editor NIS/DeepDVC use the existing shared proxy-factory/shell Present route. Editor Reflex execution is unavailable because its input/presentation loop does not own the Player latency protocol. A default-off explicit Player-host registration prevents loading Reflex there; Editor FG remains prohibited.

Persistent per-view spatial resources are reused. Reconfiguration, resize, shutdown and failed-operation recovery retire resources; normal frames and scalar sharpness/vibrance changes do not add a GPU-idle wait or rebuild temporal history. Native-only capture suppresses both spatial effects, including same-resolution sharpening, while retaining user intent.

`temporal.nis`, `temporal.deepdvc` and `temporal.reflex` join the existing sorted CommandCore registry. The [staged command guide](../../Tools/temporal-validation/README.md) documents exact request/observed generations, accepted-dispatch evidence, unavailable combinations and external runtime prerequisites. Live rendering diagnostics use PLRD v3 with backward reads that preserve unknown effect evidence; legacy `.ceprof` recordings remain ineligible for native/temporal acceptance gates.

This is source implementation and static review only. Builds, shader compilation, SDK execution, pixel checks, timing/latency measurements and the authored regression fixtures were not run. No existing PHASE 4.5 runtime acceptance row is marked complete by this addition.

## Product defaults, history and profiler gap closure — source-only checkpoint

The following describes inspected source wiring, not executed acceptance. The PHASE 4.5 ledger remains **16 rows / 71 days, no additional earned days**. Editor workspace W9 receives only the temporal/render-feature settings subset below; its broader settings ownership, profile resolver, quality presets and recovery/product acceptance are not complete.

### Portable intent and startup precedence

- `TemporalProductSettings` schema 1 is a portable value-only DTO for TU/quality/native AA, FG/interpolated-frame count, latency policy, fallback AA, NIS and DeepDVC. Editor defaults persist under `editorRenderFeatures`; separately authored Player defaults persist under `build.renderFeatures`. Graphics/Project Settings expose the two scopes separately and report save failure rather than claiming persistence.
- The cooker/package path projects the Player block to runtime `renderFeatures`; Player initializes it before graphics-device creation. Editor likewise initializes its own defaults before device creation. Missing fields migrate to native/off defaults, retaining the legacy authored AA preference; invalid/future schema, unknown or duplicate fields are rejected. Runtime directories, SDK application identity, device capabilities, active results and test faults are not portable project settings.
- Precedence is saved project defaults, then explicitly supplied startup environment overrides, then transient live commands. Invalid environment values fail to native defaults. A native capture exclusion overrides effective TU/FG/spatial behavior without replacing saved/requested intent. Late commands cannot retrofit SDK hooks into an unprepared device.
- Requested MFG count is persisted and exposed in UI/CLI independently from queried support, configured count and active count. Zero supported count means unknown/unavailable; requested count is not proof of generated outputs, successful Present or display cadence.

### Explicit camera discontinuities and submitted history

`Camera::NotifyCameraCut`, the managed CameraComponent bridge and Editor camera cut paths carry an explicit cut revision in `FrameCameraSnapshot`. The temporal consumer compares that revision and the unjittered projection/lens state with the last successfully submitted camera. Ordinary translation/rotation does not declare a cut. Cut, projection, source/view/scene, extent, provider/settings and explicit reset changes invalidate history; committed history generations avoid collisions from adding independent revision counters. Failed/attempted frames do not commit the next camera history.

### Source-owned measurements and honest profiler axes

The GPU token now keeps the originating CPU-profiler frame owner separately from render publication, real-frame, view, scene and full submission IDs. Deferred GPU completion uses that captured owner rather than the render publication ID or a newer global frame. CPU render-submit spans and GPU-pass samples carry immutable value-only provenance sidecars through bounded producer pages, collection, recording and viewer lookup. A CPU engine frame can own zero or multiple render submissions; an aggregate CPU frame is not one inferred temporal render frame.

`.ceprof` now writes **snapshot v6 / render-measurement chunk v2** and **continuous stream v7**. The fixed 100-byte tagged record preserves CPU render-submit/GPU-pass layouts and adds a distinct CPU-presenter-return payload. Snapshot v4 / stream v5 remain readable for their original axes and reject the new presenter variant; snapshot v1/v2 and stream v3 still lack render provenance. Ordinary event layout remains separate. Legacy snapshot v1/v2 and stream v3 remain readable with unknown temporal/spatial evidence, never filled from current live state. Live rendering diagnostics remain **PLRD v3**. Viewer/table/timeline and summary sources distinguish CPU frame, CPU render-submit and GPU-pass axes; source identities and native/temporal/spatial state are not interchangeable. Ambiguous narrow legacy event keys, missing provenance and incomplete source evidence fail closed for acceptance.

The Player presenter observation is one immutable, identified **CPU/native-or-SDK Present outcome** with its source frame, request/observed generations, provider/count and fault/capture state. It is not a physical display timestamp, a generated-frame identity, GPU completion or SDK final consumption. Every owner-observed CPU return is now offered to the existing bounded profiler stream with QPC clock, sequence, exact source identity and native result; live 600-frame windows and full-session files retain the same distinction. Normal page sealing is reused without a new per-Present page allocation or blocking queue. `Record-TemporalEvidence.ps1` requires stream v7 continuous presenter evidence; sampled JSONL remains control audit only. The external-measurement importer still requires calibrated endpoints or individual presentation timestamps matched to exact identities. Missing sequences, losses and ambiguous matches fail closed. Owner QPC is sampled after the native/SDK wrapper and can include wrapper work; neither these points nor existing DxTiming GPU/present-token correlations supply OS-displayed, generated-output or photon endpoints. The evidence recorder/analyzer sources are authored but unexecuted; no input-to-photon or physical-display result is claimed.

### Fault controls and remaining implementation work

The TU capability/dispatch fault control is development-only, provider/view/revision scoped, transient and excluded from cooked settings. A fault is consumed only after actual SDK readiness; an absent provider cannot masquerade as fault-induced fallback. Fault-active measurements invalidate ordinary native/temporal acceptance evidence, and source identity/consumption diagnostics keep this recovery exercise separate from normal performance or quality measurements. Shipping does not expose the injection path. **Development DX12 Player FG fault injection/staging is now authored** for capability, configure, prepare, FSR callback evaluation, Present and post-drain final-consumption acknowledgement. DLSS/XeSS expose no separate application evaluation callback and reject that scenario explicitly. Real setup/dispatch/drain must succeed before its corresponding injected rejection; leases remain retained until genuine retirement. See the staged evidence guide for exact coverage and limits. Source fixtures and fail-closed checks have not been executed.

TR1 now has a source-authored development-only scene driver and strict independent pixel-ownership oracle, described in [MotionFixtures.md](../../Tools/temporal-validation/MotionFixtures.md). `render.temporal.motion.fixture` and the `motion-fixture` stage build real cooked inputs through the existing job/asset paths and sequence seven isolated routes: rigid, skinned, instanced, meshlet, alpha, decal and world sprite. Sealed input/session/step hashes and actual successful-submission acknowledgements bind each moved capture to its immediate submitted predecessor. Positive and route-disabled GPU captures, independently projected geometry, weighted skinning, alpha holes and actual indexed/mesh-dispatch records are all required; a route label or numeric agreement alone cannot pass.

The fixture holds one native exclusion across the experiment, uses nonblocking capture admission/status/cancellation on the game thread, restores the original scene visibility, blocks saving transient diagnostic entities until their removal, and releases retained asset requests after render join before service teardown. Unicode paths retain native filesystem representation. These are inspected source contracts, **not executed results**. Legacy analytic fixtures still return INCOMPLETE (exit 2); missing/unsupported seven-route proof also remains INCOMPLETE. Full v2 PASS is only a future checker result after real capture, never inferred from authored code.

All DX12 Debug/Release runtime/hardware acceptance remains open: compilation/link/shaders, persistence/cook round trips, camera cuts/projection transitions, all-route motion pixel ownership, native-scale equivalence, mutation/failure recovery, generated identity, resize/device loss/teardown, physical timing/latency and overhead measurements. Vulkan execution parity remains PHASE 4.9. These source changes do not complete TR0/TR1/TR3, TFG9, W9 or any other acceptance row.


### Physics/master integration — source-only checkpoint

The PHASE 4.5 branch integrates master `b2f918c196d02c648eef5ef064e11ae4f8d7846b`. Script ABI **41** preserves the master's complete 198-slot prefix (including `Body_Remove` and `Body_ShapeRole`) and appends `Camera_NotifyCameraCut` as slot 199. Native host and managed assemblies must be rebuilt together; older ABI 36/40 binaries are not accepted. Physics contact/shape layouts and their authored regression coverage remain intact. The body-identity probe now supplies all required typed-asset/camera bindings. Historical ABI numbers above describe earlier checkpoints.

Profiler integration retains master counter-loss/worker-idle diagnostics and reduced chunk-lock scope alongside temporal render-measurement sidecars and continuous presenter records. GPU profiler admission stays after scene-resource preparation, with one admission call and independent CPU-engine/render-source identities. Physics contact publication/root ordering, transient motion-fixture save guards, scene cancellation and joined shutdown are retained together. Conflict resolution and contract review were static only; no builds, tests, compiler, shader, repository-script or engine execution was performed.
