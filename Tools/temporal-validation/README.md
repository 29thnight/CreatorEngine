# PHASE 4.5 staged live commands

Source and script presence alone does not constitute execution acceptance. Recorded checkpoints below and `docs/plans/TemporalReconstructionPlan.md` define the executed scope; other stages retain their own hardware, pixel, performance and latency gates. `TemporalVendorIntegration20261009.md` records the integration boundaries.

## 2026-10-10 closure regression entry points

The [current checkpoint](../../docs/analysis/TemporalClosureCheckpoint20261010.md) closes TR1 and TU5 on SDK-enabled DX12 Debug/Release. Other rows retain their full acceptance requirements. Local run identities and binary hashes are in `Artifacts/PR169/closure-checkpoint-20261010.json`.

- `Tools/regression/verify-temporal-native-evidence.ps1 -Config Debug -Stage motion` runs the actual seven-route pixel oracle and history regression. `-Stage profile` checks complete native-real measurement identities and actual structural schedule reuse; `-DisableScheduleCache` requires zero reuse.
- `Tools/regression/verify-temporal-input-demand.ps1 -Editor <absolute-editor-path>` checks SSGI ON/OFF, native/DLSS input demand, reactivation, commandlet UI pixels and basic validation.
- `Tools/regression/verify-temporal-sdk-quality.ps1 -Config Debug -Provider dlss -Quality quality` launches an isolated project and requires actual requested SDK output, strict quality provenance, validation and normal shutdown. The accepted checkpoint is warmed Quality, not the complete startup/reset/provider matrix.
- `Tools/regression/verify-temporal-editor-overlay.ps1 -Editor <absolute-editor-path> -FullViewport -RequireRequestedProviders` checks fixed-camera overlay alignment and actual compiled graph upscale/PostChain/UI ordering.

Replace Debug with Release for the other configuration. SDK compilation and `CreatorDeployTemporalRuntime=true` are separate requirements; the runtime directory is `Bin/x64-<Config>/Editor/Temporal`. Preserve failed attempts and use fresh output directories. A passing native profiler identity gate alone does not establish a master performance comparison or physical presentation latency.

TR0 follow-up: `Tools/regression/verify-temporal-golden-exclusion.ps1 -Config Debug` checks controlled golden captures under non-native startup intent, before/after intent preservation, GPU pixels and 29 failure mutations. `Tools/regression/test-render-measurement-wire.py <actual-v7-recording.ceprof> --output <result.json>` exercises 12 semantic wire mutations with recomputed CRCs. The [measurement checkpoint](../../docs/analysis/TemporalMeasurementCheckpoint20261010.md) records Debug/Release execution and the retained TR0 completion boundary.

The [consumer checkpoint](../../docs/analysis/TemporalMeasurementConsumers20261010.md) inventories the remaining surfaces. `analyze-render-budget.py <capture> --view <id> --render <w> <h> --display <w> <h> --maximum-p95-cpu-ms <explicit-limit> --output <result.json>` applies a CPU render-submit budget to one native-real population; no product default or displayed FPS is inferred. `Tools/regression/verify-temporal-player-recording.ps1 -Config Release -Package <cooked-package>` validates native v7 continuous Player owner observations in an isolated package copy. It does not supply generated output or physical presentation evidence.

`Tools/regression/verify-native-real-consumer.ps1 -Configuration Debug` collects isolated live snapshots and exercises the shared material/RG8 native preflight, including 62 metadata/state/axis rejections. `Tools/regression/RG8NativeMeasurementTests.py <results.jsonl> --output <result.json>` exercises the production RG8 file-admission helper with 52 rejections, including mismatched GPU frame/view/display identities. RG8 normal rows now retain temporal metadata; old rows without it fail instead of inheriting current settings. These checks do not execute the full material empty/robot stages or the RG8 adoption workload comparison.

Diagnostic UI units: Scene updates/s is the rate of promoted view textures. FrameOverview shows engine frame boundary time and diagnostic 16.67/33.33ms reference lines, with no project budget configured there. Rendering shows CPU live render time across views and the selected view's GPU pass-duration sum; overlapping pass durations can count twice. These surfaces do not report physical display FPS or input-to-photon latency. See `Artifacts/PR169/tr0-ui-label-checkpoint.json` for the UI build/runtime scope and retained visual acceptance boundary.

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
| TFG9 development | `temporal.fault clear` or `temporal.fault <stage> fsr\|dlss\|xess <view-id>` | TU: capability/dispatch; Player FG: fg-capability/fg-configure/fg-prepare/fg-evaluate/fg-present/fg-final-consumption; FG evaluation is FSR-only |
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

Stages: `discover`, `support`, `metadata`, `motion`, `reset`, `upscale`, `fg`, `nis`, `deepdvc`, `reflex`, `disable`, `latency`, `fallback`, `fault-fallback`, `fg-fault-fallback`, `golden-baseline`. Use a Player endpoint for `fg` and `reflex`. Provider-specific stages require the requested provider actually active; successful fallback does not pass the requested-provider stage. `upscale` additionally requires a fresh submitted real-frame AA observation at the exact receipt generation, matching requested and observed quality/method, successful temporal AA and no FXAA dispatch. `native-aa` also requires equal render/display extents, so a previously active provider at another quality cannot pass. Provider `none` verifies native rendering resumes its saved FXAA preference. FG additionally requires increased SDK-observed evidence: completed generated output for FSR, or SDK-reported presentations for DLSS/XeSS. `-InterpolatedFrameCount` defaults to 1; enabled FG requires requested, configured and active counts to match the exact acknowledged generation. This proves configured execution evidence, not per-output MFG cadence. The report names the evidence and never treats FSR callback completion as an observed native presentation. Missing SDKs, runtimes, inputs, unsupported hardware/API and setup failures remain visible and do not become synthetic green checks.

`nis` and `deepdvc` require a fresh submitted spatial real-frame observation at the exact receipt generation, matching that frame's submitted request/tuning and dispatch/extent rules. `reflex` requires the exact Player generation and requested/effective mode; an enabled effective mode additionally needs successful support/options plus sleep, marker and present API observations newer than the post-acknowledgement baseline. This avoids accepting a previous mode's final in-flight frame that completed after the request. Off with active DLSS FG is checked as a constrained effective On and explicitly reported. Off without that requirement does not demand enabled-mode markers. These stages do not compare pixels, prove internal DeepDVC evaluation or measure latency.

`support`, `discover` and `fallback` are diagnostic stages only. `motion` requires all declared production coverage but cannot establish vector direction/magnitude without the plan's independent pixel fixtures. `latency` checks marker presence for configured independent Reflex or the selected FG latency provider, not latency improvement. `golden-baseline` explicitly disables TU, FG, NIS and DeepDVC, waits for a fresh submitted spatial/native frame, and requires matching native extents before a later pixel capture; it does not compare pixels. It deliberately leaves all four image features off so the caller's next golden capture is not silently re-enabled. `disable` also disables these four image features. Neither stage changes the independent Reflex choice.

The driver changes runtime settings only for its chosen mutation stage or explicit runtime directory option. It leaves requested settings in place for inspection; use `disable` to return to native rendering. Poll deadlines fail closed. Saved `commands.json`, `responses.jsonl` and `stage.json` omit endpoint tokens.

## Acceptance still required

For the deterministic seven-route GPU motion experiment, use an Editor endpoint
with a live Game view and a stable native extent:

```powershell
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Project/Library/CommandService/endpoint.json -Stage motion-fixture -OutputDirectory C:/Evidence/new-motion-run -TimeoutSeconds 300
```

This stage submits the fixture command asynchronously and polls its operation;
the command service's five-second synchronous wait is too short for asset
preparation and fourteen readbacks. The output directory must be new. Recording
completion alone does not pass: the independent oracle checks exact submitted
pose/predecessor ownership, route disappearance in the disabled control, and
analytic motion vectors from the actual GPU bytes. JSON evidence flags are
booleans. Fixture poses resolve world transforms and UI layout before the same
frame's proxy publication. Renderer admission retries transient lock contention
without blocking the game thread or accepting a stale predecessor.

Live PLRD v3 diagnostics carry the submitted spatial mode and DeepDVC dispatch flag; legacy v1/v2 diagnostics decode spatial state as unknown, never assumed Off. Native capture gates require `temporalProvenanceSchemaVersion=2`, `spatialMode=off` and `deepDvcApplied=false` in addition to completed native-real-frame evidence. Older or missing provenance cannot qualify a golden baseline. `.ceprof` snapshot v4 introduced chunk 7 and continuous v5 introduced the per-frame tail containing exact-key CPU-render-submit/GPU-pass measurements and immutable source provenance. Snapshot v6/chunk 7 v2 and continuous v7 now add a distinct CPU presenter-owner return payload within the same 100-byte record size. Full real, publication, scene, view and submission identities remain intact; presenter points additionally retain owner QPC, sequence, configuration generations and native result. Snapshot v1/v2/v4 and continuous v3/v5 remain readable; absent provenance or presenter data is never invented. The 600-frame live window remains separate from the full stream. `summarize-material-profile.py --require-native-real` rejects lost, missing, generated, unknown, mixed-effect or ambiguous GPU provenance, rather than annotating old samples from `temporal.status`. Presenter points do not enter render-cost/native-quality gates and do not prove GPU/SDK completion, OS display or physical photon timing; see [Evidence.md](Evidence.md).

The material performance runner now waits for recording startup, writer finalization and asynchronous save completion, forces image features off and checks the saved file. `completedRealViewRendersPerSecond` is the global completed-view-render rate, not physical display FPS or a single view's rate. Per-view CPU submit and GPU pass costs are grouped separately in the file summary; overlapping pass times are not a frame critical-path or latency measurement. Native quality metadata eligibility does not establish pixel equivalence. Armed developer faults deliberately invalidate native/temporal measurement proof while retaining source IDs and factual native capture suppression.

See [External measurement and fault acceptance](Evidence.md) for acquisition/import tools, required instrument/raw artifacts, exact presenter-frame association, and the TU fault matrix. `Record-TemporalEvidence.ps1` saves a complete profile plus sampled immutable presenter responses for a separately running external instrument. `analyze-temporal-evidence.py` accepts physical input-to-photon samples or individual presentation timestamps, never CPU markers or aggregate generated-frame counts. Repeated v6/v7 Present attempts require an exact external `presenterSequence`; missing associations are rejected. `Tools/regression/TemporalExternalEvidenceTests.py --native <actual-player.ceprof> --output <result.json>` exercises importer contracts and native repeated-attempt association. Debug/Release native records and 27 importer contract tests passed at the [consumer checkpoint](../../docs/analysis/TemporalMeasurementConsumers20261010.md). External instrument latency/pacing and actual generated output acceptance remain unverified.

Run Debug/Release DX12 compilation and actual TR/TU/FG acceptance only after separate execution authorization. The raw motion capture producer, independent analytic vector checker and source fixtures are authored; the deterministic seven-route scene driver is implemented and requires fresh GPU capture/oracle evidence per tested binary and device. [Motion fixture details](MotionFixtures.md) distinguishes legacy numeric-only evidence from the submitted-step and route-ownership experiment. Development DX12 FG host-failure injection is authored as documented below; per-output SDK presentation timestamp producers and an in-engine physical latency instrument remain absent; the importer deliberately uses external measured evidence instead. Hardware matrix, pixels, jitter/resize/camera-cut resets, provider fallback, final-consumer lifetime and real-frame/performance/latency acceptance are unrun. Mutations deleting extent metadata or relabeling generated frames must fail gates. Source publication and diagnostic command success do not close those gates. Vulkan runtime parity remains PHASE 4.9.

## Pre-device startup settings and optional latency support

Set `CREATOR_TEMPORAL_UPSCALER` and/or `CREATOR_TEMPORAL_FRAME_GENERATOR` before creating devices when selecting DLSS or XeSS Vulkan. Values are `none`, `fsr`, `dlss`, `xess`. `CREATOR_TEMPORAL_RUNTIME_DIRECTORY` must be an absolute authorized SDK deployment directory; `CREATOR_TEMPORAL_DLSS_PROJECT_ID` must be the real project identity. Streamline manual factory/device hooks and XeSS Vulkan extension/feature requirements cannot be retrofitted by a late command. Such requests report integration-required until restart with the prerequisites.

For the independent Streamline routes, set `CREATOR_TEMPORAL_REFLEX=off|on|on-boost`, `CREATOR_NIS_MODE=off|scale|sharpen` and/or `CREATOR_DEEPDVC=0|1` before device creation as needed. All default to Off. These pre-device switches permit the required Streamline plugins/hooks to load; later live commands change settings but cannot retrofit missing device integration. Reflex-only/NIS/DeepDVC startup does not grant DLSS support or replace a required real DLSS project identity when DLSS is requested. Use only the authorized external pinned SDK/runtime; these controls fetch/install nothing.

`CreatorEnableAntiLag2=true` with `CreatorAntiLag2Root` selects the external official Anti-Lag2 v2.0.0a header for FSR latency control. The wrapper uses runtime `Initialize` support, pre-input `Update`, real render-submission marking and FSR's official context binding. `latencyResult`/`latencyProvider` remain independent from FG support; a marker is not an input-to-photon measurement. No SDK or driver is installed by the commands.

Authored fixtures include `Tools/regression/TemporalRuntimeContractTests.cpp`, `TemporalMeasurementProvenanceTests.cpp`, `TemporalFaultControlTests.cpp`, `TemporalPerformanceEvidenceTests.py` and the profiler codec/diagnostics fixtures. They exercise production contracts and byte-level rejection, not a mock successful provider. They remain uncompiled/unexecuted here. Motion acceptance requires the deterministic route-owning scene driver and actual GPU captures; CPU checks do not stand in for those routes.

For a separately authorized CPU-contract run from an existing Visual Studio developer shell (no SDK required):

```powershell
New-Item -ItemType Directory -Force Artifacts/TemporalContracts | Out-Null
cl.exe /nologo /std:c++20 /EHsc /utf-8 /IEngine/Utility_Framework Tools/regression/TemporalRuntimeContractTests.cpp Engine/RenderEngine/Render/Temporal/TemporalReconstruction.cpp Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp /Fe:Artifacts/TemporalContracts/runtime-contracts.exe
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

### DX12 temporal history contract

`dx12.temporalhistory` runs the production `EnhancedTemporalViewState::Begin` /
`Commit` code with a real DX12 device and native, FSR and XeSS SDK configuration.
Supply the installed trusted runtime directory through
`CREATOR_TEMPORAL_RUNTIME_DIRECTORY`; a missing requested SDK fails the test.
Run the command asynchronously and save the completed operation's structured
`data.passed`, `data.scope` and `data.log` in a fresh evidence directory.

The assertions cover independent rational Halton samples, render-pixel raster
jitter, native and SDK native-AA 1:1 extents, quality resolution separation,
aborted preparation, continuous camera movement, cut/FOV/explicit reset/resize,
logical view and scene/source history changes. The test drives Commit itself:
it does not submit a graph, evaluate SDK pixels or prove visual quality. Pair it
with live `upscale` and `reset` stages; reset now requires a fresh submitted
observation with the exact request generation and advancing history revision.

### Live resize and camera-cut history recovery

Create a disposable Editor scene with one active camera at position zero, select
the Game viewport, and run `Verify-TemporalHistoryRecovery.ps1 -EndpointFile ...
-CameraTarget Camera -OutputDirectory <new-directory>`. The driver moves that
camera, invokes `camera.cut`, and resizes the actual hidden or visible host
window twice for each of native, FSR and XeSS. It restores the original host
client dimensions and camera position. Use a disposable fixture because the
camera's cut revision is intentionally advanced and the provider preference is
changed. `window.info` retains the screen-bus `width/height` fields and separately
reports `hostClientKnown/hostClientWidth/hostClientHeight` for window restoration.

`temporal.status.lastHistoryReset` retains the last committed reset frame's
identity, source, extents, provider and canonical first jitter even when polling
a later frame. Preparation and aborted recordings cannot replace it. The
driver requires a fresh matching view/scene/generation, actual temporal AA
dispatch, advancing history and observed GPU completion of the reset frame.
It rejects a normal camera move that resets history and verifies continuous
frames after resize. The normal-move check waits for `submittedCameraPosition`
to reach the new position; a fresh frame of the old pose cannot pass. Enabled
DX12 validation with zero problems/dropped messages and exact host window
restoration are mandatory. These are live history/AA recovery observations; they do
not inspect pixels, assess reconstruction quality or measure physical display
timing. Native golden captures retain their existing forced exclusion contract.

`Tools/regression/TemporalHistoryRecoveryGateTests.ps1 -EvidenceFile
<recovery.json>` loads only the driver's pure reset predicate. It accepts the
nine recorded native/FSR/XeSS reset observations and must reject nine mutations
of reset identity, revision, first jitter, extent, view, scene and provider.

### SSGI GPU history after abort and reset

`dx12.ssgi` is registered only in the process-scoped commandlet table. Run
`Verify-SSGIAbortRecovery.ps1` with a disposable development project:

```powershell
./Tools/temporal-validation/Verify-SSGIAbortRecovery.ps1 `
    -EditorExecutable 'C:/checkout/Bin/x64-Debug/Editor/CreatorEditor.exe' `
    -ProjectPath 'C:/fixtures/SSGIProject' `
    -RuntimeDirectory 'C:/temporal-runtime' `
    -OutputDirectory 'C:/new/ssgi-debug-evidence'
```

Repeat with the Release executable and a fresh output directory. The runner
requires successful structured SSGI results, all three abort comparisons and
eight reset/recreation comparisons,
enabled basic DX12 validation with zero messages/problems/drops, and process
exit code zero. It records the runtime binary hash and stops its own process on
timeout. Use ordinary build/deployment steps to provide the runtime dependencies.

The GPU test renders a procedural sphere/ground fixture and compares eight
accepted frames with sequences that insert an abandoned preparation, declaration
or fully recorded graph before each accepted frame. The abandoned camera differs
from the accepted camera. Every recovered 128x128 RGBA history value must be
finite and match the uninterrupted sequence within 1e-6; map/extent failures
reject the run.

The same test warms eight frames, then compares a production SSGI pass against a
newly initialized pass on the first and eighth frame following a shared cut
reset, shared resize reset at two extents, or extent-only texture recreation.
The full render inputs are 256x256, 128x192, 384x320 and 192x128; depth/normal
textures are initialized on the GPU at each actual input extent. All finite
RGBA values must match within 1e-6, with nonempty surface samples, first-frame
accumulation exactly one and eighth-frame average at least four. The reset
signal is supplied directly to `PrepareFrame`; the cut fixture keeps the same
pose. This establishes reset-consumer pixel recovery, independently of the live
camera/window routing checks described above. It does not establish a moving
camera's image quality. Device loss, shared view-history consolidation, SDK
reconstruction quality and end-to-end live cut/resize pixel capture remain
separate acceptance work.

### Live SSGI camera-cut and window-resize pixels

Use a disposable scene with one active zero-position camera, a directional
light, and the static cube/floor model in `fixtures/SSGIRecovery.gltf`. Import
and place it through the normal `model.load` and `model.place` commands, then
select the Game viewport. This fixture uses real GBuffer geometry and nonzero
SSGI radiance. Run:

```powershell
./Tools/temporal-validation/Verify-LiveSSGIRecovery.ps1 `
    -EndpointFile 'C:/fixture/Library/CommandService/endpoint.json' `
    -CameraTarget Camera -OutputDirectory 'C:/new/live-ssgi-evidence'
```

The driver defaults to native rendering; `-Provider fsr|xess -Quality <quality>`
selects and requires actual requested SDK reconstruction. It moves the actual camera to X=0.2, and
checks the submitted pose. It arms `render.temporal.history.capture <new-dir>
<1|8> [fixed-sdk-clock]` before invoking `camera.cut` or `window.resize`, then polls that command's
`status` subcommand. This capture mode preserves the temporal state and observes
the first or eighth accepted frame following the next reset. It rejects changes
of view, scene or history generation, excludes FG/spatial processing, and records the
reset/cut identities and accepted predecessor. Ordinary PBR/golden capture keeps
its native exclusion and controlled-history behavior. History recovery captures
are explicitly ineligible for golden acceptance. They must be cancelled with
`render.temporal.history.capture cancel` if the triggering event is abandoned.

The 13 captures contain an old-pose control and six recovery/reference pairs:
camera cut and actual host resizes to 960x640/1280x720, each at frame 1 and 8.
A new cut at the same pose and target extent provides each reference. The
independent Python oracle checks raw float bytes, actual depth changes on camera
movement, nonempty radiance/surface pixels, first-frame accumulation exactly 1
and later average at least 4. All SSGI RGBA values must agree within 1e-6; the
final GPU display texture allows at most one 8-bit code step. Geometry extents
and submitted identities must match, and DX12 validation/restoration must pass.
`records.json`, per-capture manifests/images, `pixels.json`, and `recovery.json`
preserve both results and failure evidence. The driver restores the host window
and camera position; it intentionally leaves the tested provider preference.

Run `Tools/regression/LiveSSGIRecoveryOracleTests.py --directory <live-directory>
--output <new-directory>` to accept the recorded captures and reject twelve native or nineteen SDK
identity/clock/quality/pixel corruptions (add `--provider` and `--quality` for SDK data). Corrupted copies are stored separately from the
original evidence. Scope is native SSGI and the final GPU display image, not
physical scanout, SDK reconstruction quality or native-versus-legacy equivalence.

### Development FG failure scenarios

`-Stage fg-fault-fallback -FaultMode fg-prepare -Provider fsr -ExpectedFallback none`
exercises actual Player setup/packet preparation and the production recovery
path. The other FG fault stages and their precise safety limits are documented in
[Evidence.md](Evidence.md#fg-fault-recovery-source-tooling-unexecuted). A genuine
successful requested-provider baseline is mandatory. The injected result remains
separate from real native Present, generated output and final consumption. The
final-consumption fault rejects only a host acknowledgement after genuine drain;
it never fabricates GPU completion. All these sources/fixtures are unexecuted.

### Repeated provider teardown

With a running Editor game viewport or development Player rendering real frames,
verify native/FSR/XeSS transitions sequentially:

```powershell
./Tools/temporal-validation/Verify-TemporalProviderTransitions.ps1 `
    -EndpointFile $endpoint -OutputDirectory 'C:/new/transition-evidence' -TimeoutSeconds 60
```

This requires working FSR and XeSS runtimes. It checks actual submitted AA,
provider teardown, generation acknowledgement, and native FXAA restoration.
`transitions.json` records each completed step and its elapsed time; failures keep
the completed steps and the failing stage's evidence. It does not establish
image quality, visible presentation cadence, or input latency acceptance.

Choose `CREATOR_DX12_VALIDATION=basic` before launching for the ordinary transition
check. A separate `gpu` run may need a longer explicit timeout because GPU-based
validation instruments SDK shaders and can trigger expensive driver compilation.
Preserve a timeout as a failed bounded run; do not relabel it as a pass merely
because a later observation shows the provider active.

### SDK reset recovery comparison

With `-Provider fsr|xess`, the live recovery driver explicitly requests
`fixed-sdk-clock`: only the targeted SDK packet delta is normalized to 1000/60 ms
while the capture is pending/recording. Simulation and camera clocks remain live.
The history manifest records this condition and captured SDK quality; the oracle
rejects missing/mismatched clocks, provider fallback, wrong quality or duplicate FXAA.
FSR uses delta-dependent temporal processing, so ordinary variable-time sequences
are not assumed to produce identical reconstruction pixels. Their failures are
retained separately rather than increasing pixel tolerances.

Input attachments and SSGI are read at the actual render extent; reconstructed HDR
and final display are read at display extent. Besides SSGI/display comparisons,
reconstructed HDR must agree within 1e-6. A successful recovery comparison proves
reset recovery under this explicit SDK-clock condition; continuous motion,
ghosting/disocclusion quality and physical scanout require separate acceptance.

The final output checks also require nonconstant RGB content and a genuine RGB
change between old/moved camera captures, independently of the depth-change
check. Constant reconstructed HDR/display mutations must be rejected.

PR #169 executed checkpoint: `Artifacts/PR169/sdk-pixels/results.json` accepts
20 DX12 Debug/Release FSR/XeSS combinations across all five requested qualities.
260 GPU captures / 120 recovery-reference comparisons have zero SSGI, reconstructed
HDR and final display error under the explicit matched SDK clock. All outputs are
nonconstant and change with the real camera pose. The per-case baseline rejects
17 mutations; the four Quality cases additionally reject two constant outputs
each. Original golden captures, 468 temporal assertions and SSGI regressions pass
in both configurations. Unmatched variable-time FSR failures remain preserved.
This checkpoint does not accept continuous-motion ghosting/disocclusion quality,
variable-time image quality, native-versus-legacy equivalence or physical scanout.

## Continuous SDK interior/disocclusion diagnostic

`render.temporal.motion.fixture <new-absolute-directory> sdk-sequence` records an
explicit perspective-camera diagnostic using the selected FSR/XeSS provider.
Unlike the native seven-route mode it does not acquire native exclusion and its
captures are never golden eligible. The v2 fixture observes at least 32 distinct
accepted baseline submissions with one view/scene/history, captures a stationary
baseline with zero motion and verified HDR/ownership, then submits eight foreground translations
of 0.5 world units, followed by eight foreground-disabled frames. Each measured
pose submits once; the next capture must retain the exact preceding capture's
source/real identity, view, scene, and history generation, including the first
moving frame linked to the captured baseline. There are 17 captures per run.
Only SDK packet delta
is fixed to 1000/60 ms; CPU/readback pacing and physical scanout are not tested.

`Verify-TemporalSdkSequence.ps1 -EndpointFile <absolute-endpoint-file> -OutputDirectory <new-absolute-directory> -Provider fsr|xess -Quality native-aa|quality|balanced|performance|ultra-performance`
selects the requested quality with FG/NIS/DeepDVC disabled, records the owned fixture, then runs
`Compare-TemporalSdkSequence.py`. The fixture temporarily isolates and restores
the authoring scene; the verifier leaves the selected temporal settings active.
The oracle checks authored asset/input hashes, actual SDK provenance, all GPU
bytes for finiteness and exact dimensions, projected foreground/background
ownership and depth, motion in render pixels, and uninterrupted predecessors.
For newly exposed/background-recovery interiors, HDR residual is measured against
the same frame's pre-upscale input; final GPU display residual uses a stable
pixel in the final foreground-disabled recovery capture, whose authored state,
submission identity, source HDR/depth, full bytes and output are also verified.
This avoids comparing pixels with different spatial post-processing effects.
The recovery reference is not independent proof of absolute display tone mapping;
its HDR is separately checked against pre-upscale input. Both residuals are divided by foreground /
background contrast. Predetermined limits are mean <= 0.10 and p95 <= 0.25,
minimum contrast 0.03, and a two-render-pixel boundary exclusion. This narrow
opaque-quad interior test does not accept silhouette edges, other material
routes, unmeasured extents, variable-time SDK packets, or physical display.
Native AA requires identical render/display extents; the other qualities require
smaller render extents. The verifier binds the requested quality to the authored
sequence and every capture. At low input heights the unchanged two-render-pixel
guard can leave no disocclusion interior; that case fails instead of weakening
the mask or thresholds. Resize the host window before recording, and report the
actual Game display extent from the authored input rather than the host size.
Quality threshold failures are collected for all sixteen frames in
`qualityFailures`; the report remains `passed: false` and the CLI exits with 1.
Malformed bytes or missing submission/ownership proof still fail immediately.

`Tools/regression/TemporalSdkSequenceOracleTests.py --capture <executed-fixture> --output <new-mutation-directory>`
checks acceptance of existing GPU evidence and rejection of thirteen quality / identity /
clock / reset / pixel corruptions. It preserves the original evidence and does
not generate GPU evidence. Local execution reports are under
`Artifacts/PR169/sdk-sequence`, `Artifacts/PR169/sdk-quality-matrix`, and
`Artifacts/PR169/sdk-baseline`; completed scope is recorded in the phase plan.

The SDK sequence verifier requires the v2 captured/warmed baseline. The Python
checker keeps legacy v1 review available, but `--require-baseline` rejects v1.
Warmup acceptance applies to continuous motion from the captured stable pose.
It does not accept the original cold/appearance behavior or the entire quality
and extent matrix on a newly built binary. Preserve original failed records.

The verifier's `-Startup` switch records the separate `sdk-startup` diagnostic
(`temporal.sdk.startup.v1`). It starts capture admission after at least one
observed non-reset baseline submission instead of deliberately waiting for 32.
Renderer contention may add observations and frames; reports record both the
observation count and the real-frame gap to the captured baseline. The checker
rejects 32 or more observations or a gap of 32 or more real frames. This bounds
the observed interval, not the lifetime of the SDK context before observation.
All 17 captures, submitted predecessors, input ownership and existing quality
thresholds are checked as in v2. `--require-startup` and `--require-baseline`
reject the other mode. This is an early observed non-reset sequence, not proof
of the reset frame or a new object's first appearance. The final display
baseline check still measures contrast/uniformity rather than absolute tone
mapping. Failed historical captures remain failed.

`Verify-TemporalResetQuality.ps1 -EndpointFile <endpoint> -OutputDirectory <new-directory> -FixturePath <loaded-copy-of-SSGIRecovery.gltf> -Provider fsr|xess -Quality <quality>`
uses the existing first-submission history readback to record two distinct live
camera cuts. Run it in an idle scratch Editor scene with the authored cube/floor
fixture at identity transform, a primary camera at the origin, and a light.
It leaves the selected SDK settings active. The caller owns scene setup and
restoration. The verifier checks basic DX12 validation and releases the capture
before offline analysis; cleanup failure is recorded without losing its receipt.
All command and operation-poll requests close their connections.

`Compare-TemporalResetQuality.py` verifies the actual first reset submission,
cut receipt, view/scene/history, SDK/quality/fixed clock, authored fixture hash,
sealed material and draw, full finite readback bytes and SDK input/output extents.
It selects foreground/background interiors from input color/depth/normal and a
two-render-pixel cardinal/corner neighborhood, whose input HDR variation is at
most 0.02. At least 64 input and output pixels per region and contrast 0.03 are
required. Reconstructed HDR is compared with that frame's pre-upscale HDR using
reset sample-zero jitter (0, -1/6); normalized mean/p95 limits remain 0.10/0.25.
The separately captured SDK stage must equal the reconstructed HDR attachment.
The two resets' SSGI/HDR/final GPU display must repeat within 1e-6. This proves
repeatability of final display, not independent absolute tone-map correctness.
An insufficient interior mask fails rather than reducing the guard or limits.

`Tools/regression/TemporalResetQualityOracleTests.py` mutates existing executed
reset evidence. Its nonconstant HDR-bias case changes both reset outputs and SDK
stage images equally, so repeatability and nonempty-output checks cannot hide
failure of the independent input/HDR threshold. Execution artifacts are under
`Artifacts/PR169/sdk-reset`. Scope excludes first object appearance, silhouettes,
non-smooth surfaces, variable-time packets, physical scanout and the original
opaque-quad cold-motion failure. It does not replace the entire quality matrix.
# Artifact storage

GPU capture entry points now require at least 30 GiB of actual free space. Native,
golden, SDK quality, material, reset and SSGI drivers retain one latest successful
raw run per family; rollover occurs only after their required acceptance checks.
Use `-KeepRawCaptures` to pin an additional original needed by a pending comparison.
SDK sequence and RG8 collection check capacity but leave final retention to the
outer acceptance driver. Failed runs are never silently treated as successes.

SDK sequence/reset mutation tests immediately discard raw `.f32` copies after
their expected rejection, preserving manifests and reasons. Unexpected acceptance
keeps that case for investigation. Cleanup ledgers distinguish retained historical
verdicts from replayable pixels. Review repeated failures before retrying a large
capture and measure recovery using disk free space, since hardlinks inflate summed
file lengths. See [PR169 retention evidence](../../docs/analysis/ArtifactRetention20261011.md).

### DLSS quality / resize reset recovery

`Tools/regression/verify-temporal-transitions.ps1 -Config Debug -Quality quality -VerifyHistoryContract`
launches an isolated SDK-enabled Editor (repeat with Release). The live driver
`Verify-TemporalTransitions.ps1` waits for render-thread capture readiness before
changing quality or window size, then captures accepted reset ordinals 1 and 8
and camera-cut references. Five quality cases cover Quality-to-other modes and
Balanced-to-Quality, with host resize to 960x640 and 1280x720 from 1400x900.
`Compare-TemporalTransitions.py` checks submitted identity, fixed SDK clock,
unchanged camera/FOV, display aspect, geometry, SSGI, HDR and final GPU pixels.
It does not claim every directed quality pair or physical presentation timing.

The launcher requires zero DX12 validation messages and normal exit before
pruning to the `quality-1` representative; verdicts, manifests and hashes remain.
`TemporalTransitionOracleTests.py` checks rejected receipt/provenance mutations
using that retained capture without cloning pixel payloads.

### Perspective lens reset

`Tools/regression/verify-temporal-transitions.ps1 -Config Debug -Quality quality -Scenario lens -VerifyHistoryContract`
(and Release) runs an isolated Editor Game viewport. The default scenario remains `transitions`.

The live `camera.lens.probe <target> [vertical-fov near far]` raw fixture changes perspective lens values without emitting a camera cut or undo transaction. It is a diagnostic command, not the editor authoring workflow. FOV must be strictly between 1 and 179 degrees and clip planes must satisfy `0 < near < far`; invalid inputs leave the camera unchanged.

The lens scenario checks FOV 45/90, near .25 and far 100 from the 60/.1/500 baseline, at accepted reset ordinals 1 and 8. Same-state camera-cut captures are references; analytic projection coefficients and unchanged camera pose/cut revision distinguish actual lens changes from synthetic resets. Eight identical-value assignments must preserve history across later submissions. Baseline restoration is also compared. Each run records 18 captures, 9 comparisons, 8 no-op checks and 6 invalid-input rejections. Fixed SDK time and a static fixture limit this evidence; it does not cover orthographic transitions, continuous zoom, arbitrary projection matrices or general visual quality.

Successful runs retain only `fov-45-1` after pixel checks and normal exit. All verdicts/manifests/hashes remain. `TemporalLensOracleTests.py` rejects changed lens receipts, mixed reset/control identity and invalid projection matrices using this representative without copying raw pixels.

## 2026-10-23 TR3 history recovery

`Tools/regression/verify-tr3-history.ps1 -Configuration Debug -OutputDirectory <fresh-directory>` runs the existing SSGI commandlet with 21 production view-state checks, four abort/reset-admission comparisons, four interleaved-view comparisons and eight reset/recreation comparisons. `verify-tr3-live-history.ps1` runs the existing live SSGI pixel oracle in an isolated plane/cube project. Both accept Debug/Release. Capture arming and baseline resize now wait for actual renderer observations. See `docs/analysis/TemporalTr3History20261023.md` for recorded results and evidence.
