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
| TU1–5 | `temporal.upscale none\|fsr\|dlss\|xess [quality]` | Independently request SR and quality |
| FG0–4 | `temporal.fg none\|fsr\|dlss\|xess` | Independently request FG; non-none is rejected in Editor |
| FG1 | `temporal.latency` | Real-frame marker identity; no invented input-to-photon timing |
| TFG9 | `temporal.fallback` | Requested, selected and active providers plus concrete failure results |
| Setup | `temporal.runtime <absolute-trusted-directory> [dlss-project-id]` | Supply a preinstalled trusted SDK runtime directory; installs/downloads nothing |
| Async | `temporal.status [generation]` | Snapshot; optional acknowledgement check against live consumers |

Quality names: `native-aa`, `quality`, `balanced`, `performance`, `ultra-performance`.

All read-only commands return the same complete versioned snapshot so each saved stage preserves provenance. A successful command means its query or enqueue operation succeeded. Only `activeUpscaler`/`activeFrameGenerator`, actual evaluation results and SDK presentation counts describe execution. Requested and selected providers are not activation.

Request commands return `receiptGeneration`. Poll `temporal.status <receiptGeneration>` in a later frame; `requestState: pending` with `acknowledged: false` is expected until the real renderer (and Player presenter in Player) observes the request. A pending query is successful and does not poison the batch exit status; the staged driver fails only on its bounded deadline. Superseded requests are reported separately and cannot pass a stage. No command blocks the game thread waiting for render/GPU completion. Renderer acknowledgement and presenter acknowledgement are separate from GPU completion, CPU Present return and SDK final consumption. `simulationDeltaMilliseconds` is the SDK input simulation delta, not measured renderer duration or a performance budget. Full-width identities are decimal strings. A zero completion identity means unobserved. Generated presentation count is not a frame ID; a generated identity is the source real-frame ID plus SDK-output ordinal and remains null until observed. Completed generated-output count and SDK-reported presentation count are separate.

## Staged driver, for separately authorized later execution

Attach to an already running command service using its endpoint file. The driver never builds, launches or installs anything. For example:

```powershell
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Project/Library/CommandService/endpoint.json -Stage metadata
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Project/Library/CommandService/endpoint.json -Stage upscale -Provider fsr -Quality quality
```

Stages: `discover`, `support`, `metadata`, `motion`, `reset`, `upscale`, `fg`, `disable`, `latency`, `fallback`, `golden-baseline`. Use a Player endpoint for `fg`. Provider-specific stages require the requested provider actually active; successful fallback does not pass the requested-provider stage. FG additionally requires increased SDK-observed evidence: completed generated output for FSR, or SDK-reported presentations for DLSS/XeSS. The report names the evidence and never treats FSR callback completion as an observed native presentation. Missing SDKs, runtimes, inputs, unsupported hardware/API and setup failures remain visible and do not become synthetic green checks.

`support`, `discover` and `fallback` are diagnostic stages only. `motion` requires all declared production coverage but cannot establish vector direction/magnitude without the plan's independent pixel fixtures. `latency` checks marker presence, not latency improvement. `golden-baseline` explicitly disables both independent features, waits for real-frame acknowledgement, and requires matching native extents before a later pixel capture; it does not compare pixels. It deliberately leaves TU/FG off so the caller's next golden capture is not silently re-enabled.

The driver changes runtime settings only for its chosen mutation stage or explicit runtime directory option. It leaves requested settings in place for inspection; use `disable` to return to native rendering. Poll deadlines fail closed. Saved `commands.json`, `responses.jsonl` and `stage.json` omit endpoint tokens.

## Acceptance still required

Run Debug/Release DX12 compilation and actual TR/TU/FG acceptance only after separate execution authorization. Add/run the plan's static/skinned/instanced/decal/alpha direction/magnitude fixtures, deterministic jitter sequence and resize/camera-cut/reset checks, provider/API support matrix and fault-induced fallback checks, completed real-frame pixel captures, generated output proof, final-consumer lifetime checks and real-frame/latency measurements. Mutations deleting extent metadata or relabeling generated frames must fail the acceptance gates. Source publication and diagnostic command success do not close those gates. Vulkan runtime parity remains PHASE 4.9.

## Pre-device startup settings and optional latency support

Set `CREATOR_TEMPORAL_UPSCALER` and/or `CREATOR_TEMPORAL_FRAME_GENERATOR` before creating devices when selecting DLSS or XeSS Vulkan. Values are `none`, `fsr`, `dlss`, `xess`. `CREATOR_TEMPORAL_RUNTIME_DIRECTORY` must be an absolute authorized SDK deployment directory; `CREATOR_TEMPORAL_DLSS_PROJECT_ID` must be the real project identity. Streamline manual factory/device hooks and XeSS Vulkan extension/feature requirements cannot be retrofitted by a late command. Such requests report integration-required until restart with the prerequisites.

`CreatorEnableAntiLag2=true` with `CreatorAntiLag2Root` selects the external official Anti-Lag2 v2.0.0a header for FSR latency control. The wrapper uses runtime `Initialize` support, pre-input `Update`, real render-submission marking and FSR's official context binding. `latencyResult`/`latencyProvider` remain independent from FG support; a marker is not an input-to-photon measurement. No SDK or driver is installed by the commands.

Authored CPU fixtures are `Tools/regression/TemporalRuntimeContractTests.cpp`, `TemporalMeasurementProvenanceTests.cpp`, and the updated `ProfilerRenderingDiagnosticsTests.cpp`. They exercise the production jitter/validation/selection/control and provenance codec code, not a mock successful provider. They remain uncompiled/unexecuted here. Motion pixel fixtures still need the actual scene route and GPU captures; these CPU fixtures do not stand in for those checks.

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
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage fallback
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile $endpoint -Stage golden-baseline
```

The motion stage needs a live fixture containing every reported motion route; an empty/static-only scene must not pass all-route coverage. Missing optional Anti-Lag2/driver support must fail the latency stage rather than turn valid FSR rendering into a latency pass. For pixel capture use an Editor endpoint with the existing `render.pbr.capture <new-absolute-directory> [game|editor|material]` command; Player deliberately does not register Editor capture/authoring commands. The capture command itself holds the native-only gate even if user TU/FG preferences are enabled.

The SDK build switches apply consistently to the solution and final consumers: `Configuration=Debug` or `Release`, `Platform=x64`, `EngineShipping=false`; then enable only the authorized supplied SDKs using `CreatorEnableFsrDx12=true`, `CreatorFsrRoot`, `CreatorFsrLibraries`; `CreatorEnableDlssStreamline=true`, `CreatorStreamlineRoot`; `CreatorEnableXeSS=true`, `CreatorXeSSRoot`; and optionally `CreatorEnableAntiLag2=true`, `CreatorAntiLag2Root`. FSR requires full paths to the externally source-built upscaler/optical-flow/interpolation/DX12-backend libraries and their import dependencies. These settings neither fetch nor build an SDK. Never enable both FSR native backend static libraries in the same binary. See the [pinned integration requirements](../../docs/plans/TemporalVendorIntegration20261009.md) before any separately authorized build or runtime deployment.
