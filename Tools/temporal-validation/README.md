# PHASE 4.5 staged live commands

These sources and scripts are authored, **not executed** in this change. They do not constitute compile, hardware, shader, pixel, performance or latency acceptance. The authoritative scope remains `docs/plans/TemporalReconstructionPlan.md` and `TemporalVendorIntegration20261009.md`.

## Existing command hosts

Both Editor and development Player register these commands through the existing CommandCore descriptors, help and `/commands` discovery. Shipping Player retains its existing command rejection. No second console or reflection system is introduced.

| Stage | Command | Meaning |
|---|---|---|
| TU0 | `temporal.support` | Active-device SDK/runtime queries; empty/not queried is not support |
| TR0 | `temporal.metadata` | Real-frame identity, render/display extents, separate GPU/present completion |
| TR1 | `temporal.motion` | Published production input validity and static/skinned/instanced/decal/alpha coverage |
| TR2–3 | `temporal.reset` | Queue a history reset; returns a request generation |
| TU1–5 | `temporal.upscale none\|fsr\|dlss\|xess [quality]` | Request the temporal AA/upscale provider and quality, independently of FG |
| FG0–4 | `temporal.fg none\|fsr\|dlss\|xess [interpolated-frame-count]` | Independently request FG/MFG; actual capability bounds the accepted count; non-none is rejected in Editor |
| FG1 | `temporal.latency` | Real-frame marker identity; no invented input-to-photon timing |
| Spatial | `temporal.nis [off\|scale\|sharpen] [render-scale] [sharpness]` | Request optional NIS; temporal selection converts scale to sharpen-only |
| Color | `temporal.deepdvc [off\|on] [intensity] [saturation-boost]` | Request default-off SDR-only final-resolution digital vibrance, before UI |
| Latency | `temporal.reflex [off\|on\|on-boost]` | Request Reflex independently of AA, upscaling and FG; preserve requested mode when DLSS FG requires On |
| TFG9 | `temporal.fallback` | Requested, selected and active providers plus concrete failure results |
| TFG9 development | `temporal.fault clear` or `temporal.fault capability\|dispatch fsr\|dlss\|xess <view-id>` | Deliberately reject actual successful TU readiness/dispatch in one view, then use the production fallback path |
| Setup | `temporal.runtime <absolute-trusted-directory> [dlss-project-id]` | Supply a preinstalled trusted SDK runtime directory; installs/downloads nothing |
| Async | `temporal.status [generation]` | Snapshot; optional acknowledgement check against live consumers |

Quality names: `native-aa`, `quality`, `balanced`, `performance`, `ultra-performance`.

## Anti-aliasing and upscale selection

Use the existing provider and quality together. `temporal.upscale dlss native-aa` selects DLAA, `temporal.upscale fsr native-aa` selects FSR Native AA, and `temporal.upscale xess native-aa` selects XeSS AA. Native AA reconstructs at display resolution (1:1); the other quality choices request lower-resolution rendering and temporal upscaling. There is no additional standalone TAA pass or separate global AA setting. Omitting quality preserves the last requested quality.

Project Settings > Graphics > Pass settings > Anti-aliasing / Upscaling exposes the same provider and quality selection. The native choices are Off and FXAA; temporal choices are FSR, DLSS and XeSS, with the provider's native-AA mode first in the quality list. These are temporary live controls, not saved project-profile settings. Frame generation remains independent.

Choosing a temporal provider preserves the existing `fxaaEnabled` preference, shown as “FXAA on native / fallback frames.” FXAA is skipped only after successful temporal reconstruction of that frame, including native-AA modes. A request, supported SDK, configured adapter or successful FG does not suppress it. Returning to native rendering with `temporal.upscale none`, a native capture, or a native fallback uses the saved FXAA preference. Selecting Off or FXAA explicitly in the UI changes that preference. If a requested provider falls back to another temporal provider that successfully reconstructs the frame, FXAA is still skipped.

SDK dispatch failure is fail-closed: the failed frame is discarded, with no same-frame FXAA fallback and no new submitted-AA observation. A subsequent successfully configured and submitted FSR/native frame resumes rendering. Successful FSR reconstruction still skips FXAA; native rendering resumes the saved FXAA preference. Failure must not overwrite that preference or publish the failed frame as an AA success.

Snapshots expose `requestedUpscaleQuality` and `effectiveRequestedUpscaleQuality` beside their existing requested providers. The observed submitted frame has `aaObserved`, `temporalAaApplied`, `fxaaRequested` (its saved native preference) and `fxaaApplied` (its actual FXAA dispatch), plus `activeAaMethod` and `activeUpscaleQuality`. `activeAaMethod` is `none`, `fxaa`, `fsr_native_aa`, `dlaa`, `xess_aa`, `fsr_upscale`, `dlss_upscale` or `xess_upscale`; it is null before AA is observed. Active quality is null without successful temporal reconstruction. These observations retain the submitted frame's quality when a later request is pending; inspect `viewId`, `lastRealFrameId` and the generation fields rather than treating the latest request as already active. Submitted execution evidence is not a GPU-completion, pixel-quality or display-presentation result.

### AA acceptance matrix (authored, unexecuted)

Run only with separate execution authorization and the applicable SDK/runtime/hardware. Repeat successful temporal and fallback cases with the saved FXAA preference both on and off; unsupported configurations remain explicit failures, not passes.

| Case | Required evidence |
|---|---|
| Native default, FXAA preference on / off | Display-size rendering; `temporalAaApplied=false`, `fxaaApplied` matches the preference; active AA is `fxaa` / `none` |
| Each of FSR, DLSS and XeSS with `native-aa` | Requested provider actually reconstructs at equal render/display extents; correct native-AA name, `activeUpscaleQuality=native-aa`, `fxaaApplied=false`; preference unchanged |
| Each provider with every supported SR quality | Requested provider actually reconstructs at the SDK-selected lower extent; observed quality matches that submitted frame; `fxaaApplied=false`; preference unchanged |
| Requested provider unavailable, FSR fallback succeeds | Requested failure remains visible; active provider is FSR and actual AA/quality match FSR; FXAA skipped |
| All temporal providers unavailable, native fallback; also explicit disable and native capture | Display-size native rendering uses saved FXAA preference; active quality is null; capture preserves requested temporal selection |
| Injected SDK dispatch failure | Failed frame discarded; no same-frame FXAA dispatch or successful-AA publication; subsequent successful FSR/native submission follows its own AA policy |
| Alternating temporal and native views | Per-view frame identities and dispatch records show no suppression/result leaking across views, including parallel recording |
| Quality change while previous frame remains observed | Requested quality changes immediately; active AA/quality retain the previous submitted frame until the new generation is submitted |

## Independent NIS, DeepDVC and Reflex controls

Project Settings > Graphics > Pass settings also exposes **Spatial scaling / Digital vibrance** and **Low latency / NVIDIA Reflex**. Like the AA controls, these are temporary live process settings, not persisted project-profile settings. A supported SDK or a queued request does not turn the last observed frame into a new successful evaluation. Their commands with no arguments return a read-only snapshot; optional numeric arguments preserve their prior values when omitted. Numeric CLI values must be finite and within the documented range, with a decimal point independent of locale.

- NIS defaults to Off. `scale` requests spatial scaling plus sharpening at render scale `[0.5,1]` (default `0.77`); `sharpen` keeps input/output extents equal. A render scale of `1.0` resolves to sharpen-only. Sharpness is `[0,1]` (default `0.5`). A successfully selected temporal reconstruction provider changes a NIS scale request to effective sharpen-only, including Native AA, so there is no second scale. A temporal request that resolves to native fallback does not itself prohibit NIS scaling. Unsupported NIS must not leave an un-upscaled low-resolution image presented as display resolution. The selected/active modes and exact input/output extents remain visible separately from the requested mode.
- DeepDVC defaults to Off, with intensity `[0,1]` (default `0.5`) and saturation boost `[0,1]` (default `0.25`). It operates on the actual final-resolution SDR scene after tone mapping and NIS, before UI composition. HDR/ineligible color inputs remain an explicit non-success; the option does not silently run in HDR or affect UI pixels. `spatialPost.sdrEligible` describes the post-process surface, not the pre-tone-map temporal input's `highDynamicRange` flag.
- Reflex defaults to Off and is independent of the AA, temporal-upscale and FG selectors. DX12 Player accepts Off, On and On + Boost. Boost can increase GPU power use. Editor has no sleep/marker execution route: its On/Boost UI choices are disabled, and `temporal.reflex on` or `on-boost` returns `temporal.player_required` without storing the request. Query and Off remain available. Configure the Player through its command service or `CREATOR_TEMPORAL_REFLEX` before startup; there is no automatic Editor-to-Player propagation. If enabled intent was supplied externally to Editor, its snapshot reports `reflex.executionState=player_required`, `pending=false` and an integration-required options result, not indefinite pending or active latency. When actual DLSS FG requires Reflex, the effective mode is at least On while preserving the user's requested mode; disabling FG restores that choice. Unsupported hardware, missing SDK/runtime and missing pre-device hooks remain explicit capability or setup failures. Reflex-only startup does not need an NGX/DLSS project ID. XeSS FG owns XeLL pacing: simultaneous Reflex/XeLL pacing is not supported, and a conflicting live switch can require a restart.

The snapshot's `spatialPost` object separates requested/effective requested settings, capability queries, selected modes and submitted execution results. `observed`, `realFrameId`, `inputExtent`, `outputExtent`, `activeNisMode` and `deepDvcApplied` describe the actual submitted spatial work. Its `requestedGeneration`, per-frame `observedGeneration` and `pending` distinguish the latest request from that submitted frame. The `submittedNisMode`, `submittedNisRenderScale`, `submittedNisSharpness`, `submittedDeepDvcEnabled`, `submittedDeepDvcIntensity` and `submittedDeepDvcSaturationBoost` values come from that frame's immutable effective settings after feature selection; the ordinary tuning fields describe current requested settings. These submitted fields are meaningful only with `observed=true`. The `reflex` object separates support, sleep/marker support, `optionsResult`, `configured`, requested/effective mode and the `requiredByFrameGeneration` constraint. Its generation/pending fields use the Player consumer, not Editor renderer acknowledgement. `markerRealFrameId`, `sleepRealFrameId` and `presentedRealFrameId` are SDK API observations and can advance independently; they are not input-to-photon timings. The top-level requested mode is current user intent; the nested observed mode may still describe an older Player frame while pending.

`deepDvcApplied=true` means a submitted SDK-accepted dispatch, not proof of the plugin's internal NGX evaluation or visual correctness. The pinned plugin may return success without propagating an internal evaluation failure. Only a completed GPU capture and pixel acceptance can close that gap; staged diagnostics deliberately do not claim it.

Native golden capture temporarily disables TU, FG, NIS and DeepDVC without losing the user's choices. Reflex remains unchanged because it does not alter image content. Failed SDK dispatch must not publish a submitted spatial success; optional SDK absence and unsupported configurations remain observable rather than synthetic acceptance.

### Spatial / Reflex acceptance matrix (authored, unexecuted)

Repeat only after separate execution authorization, with the applicable supported DX12 hardware/runtime. CLI receipt or UI display is not acceptance.

| Case | Required evidence |
|---|---|
| All optional controls at defaults | NIS Off, DeepDVC Off, Reflex Off; ordinary native/FXAA behavior and UI unchanged |
| NIS scale with native rendering at each accepted ratio | Fresh exact-generation submitted NIS success; lower input and display output extents; completed GPU capture needed for image acceptance |
| NIS sharpen with native rendering and each temporal provider/quality | Equal NIS input/output extents; no second scale; actual sharpening dispatch; preserve AA/FG choices |
| NIS scale request with temporal provider or native fallback | Temporal selection resolves to sharpen-only; native fallback can select scale; unavailable NIS never presents an un-upscaled low-resolution image |
| DeepDVC SDR On/Off and strength extremes | Fresh final-resolution post-tone-map/pre-UI result; bounded values; UI pixels unchanged in completed capture |
| DeepDVC HDR / invalid post-process input | Explicit ineligible/failure result, no DeepDVC application or false success |
| SDK absent / old runtime / unsupported hardware, per feature | Separate capability and execution failures, user choice preserved, other independent controls do not acquire false activation |
| Native golden capture while features requested | Actual native extent, NIS/DeepDVC/TU/FG inactive during capture; requested settings return afterward; Reflex choice retained |
| Reflex On / Boost with FG and temporal upscaling off | Exact Player generation, correct SDK mode/options, fresh sleep, marker and present observations; no inferred latency improvement |
| Reflex Off with active DLSS FG, then FG disabled | User Off remains requested; effective On while FG requires it; restores Off when FG is disabled |
| Reflex unavailable / XeSS FG conflict / late enable without startup hooks | Explicit unsupported or integration-required result; no double pacing and no latency pass |
| Rapid setting changes, view switching and failed graph submission | Superseded generations fail stages; old observations remain identifiable; no failed frame becomes a fresh success |

## Snapshot and acknowledgement

All read-only commands return the same complete versioned snapshot so each saved stage preserves provenance. A successful command means its query or enqueue operation succeeded. Only submitted active-provider/spatial observations, actual evaluation results and SDK presentation evidence describe execution. Requested and selected providers are not activation; Reflex configuration remains separate from fresh sleep/marker/presentation API observations.

Request commands return `receiptGeneration`. Poll `temporal.status <receiptGeneration>` in a later frame; `requestState: pending` with `acknowledged: false` is expected until the real renderer (and Player presenter in Player) observes the request. A pending query is successful and does not poison the batch exit status; the staged driver fails only on its bounded deadline. Superseded requests are reported separately and cannot pass a stage. No command blocks the game thread waiting for render/GPU completion. Renderer acknowledgement and presenter acknowledgement are separate from GPU completion, CPU Present return and SDK final consumption. `simulationDeltaMilliseconds` is the SDK input simulation delta, not measured renderer duration or a performance budget. Full-width identities are decimal strings. A zero completion identity means unobserved. Generated presentation count is not a frame ID; a generated identity is the source real-frame ID plus SDK-output ordinal and remains null until observed. Completed generated-output count and SDK-reported presentation count are separate.

## Staged driver, for separately authorized later execution

Attach to an already running command service using its endpoint file. The driver never builds, launches or installs anything. For example:

```powershell
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Project/Library/CommandService/endpoint.json -Stage metadata
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Project/Library/CommandService/endpoint.json -Stage upscale -Provider fsr -Quality quality
```

Stages: `discover`, `support`, `metadata`, `motion`, `reset`, `upscale`, `fg`, `nis`, `deepdvc`, `reflex`, `disable`, `latency`, `fallback`, `fault-fallback`, `golden-baseline`. Use a Player endpoint for `fg` and `reflex`. Provider-specific stages require the requested provider actually active; successful fallback does not pass the requested-provider stage. `upscale` additionally requires a fresh submitted real-frame AA observation at the exact receipt generation, matching requested and observed quality/method, successful temporal AA and no FXAA dispatch. `native-aa` also requires equal render/display extents, so a previously active provider at another quality cannot pass. Provider `none` verifies native rendering resumes its saved FXAA preference. FG additionally requires increased SDK-observed evidence: completed generated output for FSR, or SDK-reported presentations for DLSS/XeSS. `-InterpolatedFrameCount` defaults to 1; enabled FG requires requested, configured and active counts to match the exact acknowledged generation. This proves configured execution evidence, not per-output MFG cadence. The report names the evidence and never treats FSR callback completion as an observed native presentation. Missing SDKs, runtimes, inputs, unsupported hardware/API and setup failures remain visible and do not become synthetic green checks.

`nis` and `deepdvc` require a fresh submitted spatial real-frame observation at the exact receipt generation, matching that frame's submitted request/tuning and dispatch/extent rules. `reflex` requires the exact Player generation and requested/effective mode; an enabled effective mode additionally needs successful support/options plus sleep, marker and present API observations newer than the post-acknowledgement baseline. This avoids accepting a previous mode's final in-flight frame that completed after the request. Off with active DLSS FG is checked as a constrained effective On and explicitly reported. Off without that requirement does not demand enabled-mode markers. These stages do not compare pixels, prove internal DeepDVC evaluation or measure latency.

`support`, `discover` and `fallback` are diagnostic stages only. `motion` requires all declared production coverage but cannot establish vector direction/magnitude without the plan's independent pixel fixtures. `latency` checks marker presence for configured independent Reflex or the selected FG latency provider, not latency improvement. `golden-baseline` explicitly disables TU, FG, NIS and DeepDVC, waits for a fresh submitted spatial/native frame, and requires matching native extents before a later pixel capture; it does not compare pixels. It deliberately leaves all four image features off so the caller's next golden capture is not silently re-enabled. `disable` also disables these four image features. Neither stage changes the independent Reflex choice.

The driver changes runtime settings only for its chosen mutation stage or explicit runtime directory option. It leaves requested settings in place for inspection; use `disable` to return to native rendering. Poll deadlines fail closed. Saved `commands.json`, `responses.jsonl` and `stage.json` omit endpoint tokens.

## Acceptance still required

Live PLRD v3 diagnostics carry the submitted spatial mode and DeepDVC dispatch flag; legacy v1/v2 diagnostics decode spatial state as unknown, never assumed Off. Native capture gates require `temporalProvenanceSchemaVersion=2`, `spatialMode=off` and `deepDvcApplied=false` in addition to completed native-real-frame evidence. Older or missing provenance cannot qualify a golden baseline. New `.ceprof` snapshot v4 adds chunk 7 and continuous v5 adds a per-frame tail containing exact-key CPU-render-submit/GPU-pass measurements and immutable source provenance. The 100-byte measurement preserves full real, publication, scene, view and submission identities. Snapshot v1/v2 and continuous v3 remain readable but ineligible; re-saving legacy input cannot manufacture provenance. `summarize-material-profile.py --require-native-real` rejects lost, missing, generated, unknown, mixed-effect or ambiguous GPU provenance, rather than annotating old samples from `temporal.status`.

The material performance runner now waits for recording startup, writer finalization and asynchronous save completion, forces image features off and checks the saved file. `completedRealViewRendersPerSecond` is the global completed-view-render rate, not physical display FPS or a single view's rate. Per-view CPU submit and GPU pass costs are grouped separately in the file summary; overlapping pass times are not a frame critical-path or latency measurement. Native quality metadata eligibility does not establish pixel equivalence. Armed developer faults deliberately invalidate native/temporal measurement proof while retaining source IDs and factual native capture suppression.

See [External measurement and fault acceptance](Evidence.md) for the actual acquisition/import tools, required instrument/raw artifacts, exact presenter-frame association, and authored TU fault matrix. `Record-TemporalEvidence.ps1` saves a complete profile plus sampled immutable presenter responses for a separately running external instrument. `analyze-temporal-evidence.py` accepts physical input-to-photon samples or individual presentation timestamps, never CPU markers or aggregate generated-frame counts. Missing polled presenter frames are rejected, not inferred. None of these tools has been executed here.

Run Debug/Release DX12 compilation and actual TR/TU/FG acceptance only after separate execution authorization. The raw motion capture producer, independent analytic vector checker and source fixtures are authored; the deterministic route-owning static/skinned/instanced/decal/alpha scene driver is still missing implementation. [Motion fixture details](MotionFixtures.md) explains why numeric agreement returns incomplete route acceptance, not TR1 PASS. SDK FG fault injection, per-output SDK presentation timestamp producers and an in-engine physical latency instrument also remain absent; the importer deliberately uses external measured evidence instead. Hardware matrix, pixels, jitter/resize/camera-cut resets, provider fallback, final-consumer lifetime and real-frame/performance/latency acceptance are unrun. Mutations deleting extent metadata or relabeling generated frames must fail gates. Source publication and diagnostic command success do not close those gates. Vulkan runtime parity remains PHASE 4.9.

## Pre-device startup settings and optional latency support

Set `CREATOR_TEMPORAL_UPSCALER` and/or `CREATOR_TEMPORAL_FRAME_GENERATOR` before creating devices when selecting DLSS or XeSS Vulkan. Values are `none`, `fsr`, `dlss`, `xess`. `CREATOR_TEMPORAL_RUNTIME_DIRECTORY` must be an absolute authorized SDK deployment directory; `CREATOR_TEMPORAL_DLSS_PROJECT_ID` must be the real project identity. Streamline manual factory/device hooks and XeSS Vulkan extension/feature requirements cannot be retrofitted by a late command. Such requests report integration-required until restart with the prerequisites.

For the independent Streamline routes, set `CREATOR_TEMPORAL_REFLEX=off|on|on-boost`, `CREATOR_NIS_MODE=off|scale|sharpen` and/or `CREATOR_DEEPDVC=0|1` before device creation as needed. All default to Off. These pre-device switches permit the required Streamline plugins/hooks to load; later live commands change settings but cannot retrofit missing device integration. Reflex-only/NIS/DeepDVC startup does not grant DLSS support or replace a required real DLSS project identity when DLSS is requested. Use only the authorized external pinned SDK/runtime; these controls fetch/install nothing.

`CreatorEnableAntiLag2=true` with `CreatorAntiLag2Root` selects the external official Anti-Lag2 v2.0.0a header for FSR latency control. The wrapper uses runtime `Initialize` support, pre-input `Update`, real render-submission marking and FSR's official context binding. `latencyResult`/`latencyProvider` remain independent from FG support; a marker is not an input-to-photon measurement. No SDK or driver is installed by the commands.

Authored fixtures include `Tools/regression/TemporalRuntimeContractTests.cpp`, `TemporalMeasurementProvenanceTests.cpp`, `TemporalFaultControlTests.cpp`, `TemporalPerformanceEvidenceTests.py` and the profiler codec/diagnostics fixtures. They exercise production contracts and byte-level rejection, not a mock successful provider. They remain uncompiled/unexecuted here. Motion numeric fixtures still need their deterministic route-owning scene driver and actual GPU captures; CPU checks do not stand in for those routes.

For a separately authorized CPU-contract run from an existing Visual Studio developer shell (no SDK required):

```powershell
New-Item -ItemType Directory -Force Artifacts/TemporalContracts | Out-Null
cl.exe /nologo /std:c++20 /EHsc Tools/regression/TemporalRuntimeContractTests.cpp Engine/RenderEngine/Render/Temporal/TemporalReconstruction.cpp Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp /Fe:Artifacts/TemporalContracts/runtime-contracts.exe
if ($LASTEXITCODE -ne 0) { throw 'CPU contract fixture compilation failed' }
& ./Artifacts/TemporalContracts/runtime-contracts.exe
if ($LASTEXITCODE -ne 0) { throw 'CPU contract fixture failed' }
```

This isolated command is not a RenderEngine build or the phase's DX12 Debug/Release/GPU acceptance. It is documented, not executed, in this change.

## Suggested live sequence

Use the verified endpoint JSON for the already-running instance, not a guessed shared project endpoint. Development Player (`EngineShipping=false`, either Debug or Release) enables its loopback command service only when launched with `--command-service`. Shipping excludes these commands. The packaged backend must be DX12 for the FG stages; changing temporal settings does not change the packaged graphics backend.

```powershell
$endpoint = 'C:/path/to/the-running-instance-endpoint.json'
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage discover
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage support
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage metadata
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage motion
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage reset
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage upscale -Provider fsr -Quality quality
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage fg -Provider fsr
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage latency
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage nis -NisMode sharpen -NisSharpness 0.5
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage deepdvc -DeepDvcMode on -DeepDvcIntensity 0.5 -DeepDvcSaturationBoost 0.25
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage reflex -ReflexMode on
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage fallback
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage golden-baseline
```

The motion stage needs a live fixture containing every reported motion route; an empty/static-only scene must not pass all-route coverage. Missing optional Anti-Lag2/driver support must fail the latency stage rather than turn valid FSR rendering into a latency pass. For pixel capture use an Editor endpoint with the existing `render.pbr.capture <new-absolute-directory> [game|editor|material]` command; Player deliberately does not register Editor capture/authoring commands. The capture command itself holds the native-only gate even if user TU/FG preferences are enabled.

The SDK build switches apply consistently to the solution and final consumers: `Configuration=Debug` or `Release`, `Platform=x64`, `EngineShipping=false`; then enable only the authorized supplied SDKs using `CreatorEnableFsrDx12=true`, `CreatorFsrRoot`, `CreatorFsrLibraries`; `CreatorEnableDlssStreamline=true`, `CreatorStreamlineRoot`; `CreatorEnableXeSS=true`, `CreatorXeSSRoot`; and optionally `CreatorEnableAntiLag2=true`, `CreatorAntiLag2Root`. FSR requires full paths to the externally source-built upscaler/optical-flow/interpolation/DX12-backend libraries and their import dependencies. These settings neither fetch nor build an SDK. Never enable both FSR native backend static libraries in the same binary. See the [pinned integration requirements](../../docs/plans/TemporalVendorIntegration20261009.md) before any separately authorized build or runtime deployment.
