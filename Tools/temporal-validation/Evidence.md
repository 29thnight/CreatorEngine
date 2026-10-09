# Recorded and external temporal evidence

Everything described here is authored source, not an executed result. Only run
with separate execution authorization, an already running development instance,
appropriate trusted SDK deployment and the required hardware.

## TU fault fallback

`Invoke-TemporalStage.ps1 -Stage fault-fallback` implements the following matrix:

| Provider | FaultMode | ExpectedFallback | Required source observations |
|---|---|---|---|
| fsr | capability / dispatch | none | Real FSR reconstruction first; consumed injected failure; later full-size native submission and saved FXAA preference |
| dlss | capability / dispatch | fsr | Real DLSS reconstruction first; consumed failure; later successful FSR reconstruction |
| xess | capability / dispatch | fsr | Real XeSS reconstruction first; consumed failure; later successful FSR reconstruction |
| dlss / xess | capability / dispatch | none | Real requested provider first; FSR genuinely unavailable; native fallback after the injected failure |

Example, documented only:

```powershell
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Player/endpoint.json -Stage fault-fallback -Provider fsr -FaultMode dispatch -ExpectedFallback none
```

The command is `temporal.fault capability|dispatch <provider> <view-id>` or
`temporal.fault clear`. It is registered only with `CE_DEVELOPMENT && !CE_SHIPPING`;
the control API also refuses injection in other builds. Settings are transient,
excluded from project/cooked data and external machine/SDK configuration.

Capability rejection happens only after a real adapter and valid render extent
are established. Dispatch rejection occurs immediately before the real adapter
dispatch after real setup. Both failures enter the existing failed-provider
ledger and FSR/native fallback; they do not install a fake successful provider.
Native codes -45001 and -45002 mark the deliberate failures. The failed dispatch
frame is discarded; only a later submitted real frame may satisfy fallback.

Injection targets one full view identity. `faultInjection` exposes requested
revision, consumed revision/count, view, scene and real-frame identity. Capability
consumption occurs before a real frame and reports realFrameId zero deliberately.
The stage requires actual baseline success, exact generation/view/scene and an
increased consumed count, then validates the requested failure code and fallback.
Unavailable SDKs cannot pass. The request persists until clear; the failed-provider
ledger prevents automatic repeat attempts until a genuine reconstruction change
or explicit new fault revision. Normal Off operation takes no injection mutex.

Cleanup clears only the revision owned by the stage, even on failure, and reports
cleanup failure. It leaves the chosen upscale request in place so the subsequent
configuration retries genuine SDK support. Another controller replacing the
fault makes the stage fail rather than silently overwriting that controller.
Golden preflight refuses an already armed fault instead of clearing it. Armed
fault frames are ineligible for native pixel and recorded performance gates.

This covers TU fallback submission only. It does not cover FG SDK failures, real
device removal/recreation, teardown races, pixel correctness or presentation
spacing. `dx12.live remove-device` is not an SDK-fallback substitute.

## FG fault recovery (source tooling, unexecuted)

`Invoke-TemporalStage.ps1 -Stage fg-fault-fallback` targets the development DX12
Player only. First it requires successful real requested-provider Present plus
new SDK-generated evidence, then arms the chosen fault for that exact view. A
missing SDK, unsupported device, unprepared startup or stale counter is a failure
to establish the baseline, never a successful injected-failure scenario.

| FaultMode | Deliberate code | Injection boundary |
|---|---|---|
| fg-capability | -45003 | Reject a genuinely successful active-device FG capability query before selection |
| fg-configure | -45004 | Reject a genuinely initialized proxy/context; ordinary shutdown must retire it before fallback |
| fg-prepare | -45005 | Reject successful preparation of the actual imported frame packet; discard recording and preserve adapter owners until shutdown |
| fg-evaluate | -45006 | FSR only: after real successful interpolation command recording, return callback failure; the pinned SDK workaround clears generated-output count and discards the commands |
| fg-present | -45007 | Reject the host result after successful SDK/native Present; preserve the raw Present observation and retain leases for genuine drain/recovery |
| fg-final-consumption | -45008 | Reject host acknowledgement only after actual SDK/fence drain succeeds; leave sdkConsumed false and pins retained until ordinary shutdown safely retires them |

These are host failure-path exercises, not fabricated native SDK return codes.
`faultInjection.consumedMode` and `injectedResult` retain the exact rejected stage.
The requested FG failure remains visible through fallback, separately from the
native Present observation. Capability/configure consumption has realFrameId zero;
other stages require the actual source real frame, view and scene. Fault revisions
and clear use the existing drained reconfiguration path; faults never become
cooked/project settings or a shipping injection route. Normal Off mode acquires
no extra injection mutex.

DLSS and XeSS perform generation inside vendor-owned Present; neither exposes an
application-owned evaluation operation here. `fg-evaluate` for those providers is
explicitly rejected by command and control API. It is not reclassified as a
successful evaluation test. Their prepare and Present boundaries remain separate.

For FSR, use each mode with ExpectedFallback none. For DLSS/XeSS, use all modes
except fg-evaluate with ExpectedFallback fsr when FSR is genuinely eligible, or
none when it is genuinely unavailable (or the requested MFG count excludes it).
The same requested interpolation count is retained; a multi-output request is
not silently reduced to a one-output FSR configuration.

The stage requires exact generation, consumed mode/revision/code and source
identity, then a later successful fallback Present in that same view/scene.
An FSR fallback additionally needs fresh completed-generated-output evidence and
its real final-consumption identity. Cleanup clears only the stage-owned fault
revision. The selected FG request remains in place for normal retry after clear.

The final-consumption scenario does **not** simulate a stuck GPU fence or prove
release-before-consumption safety under a real device loss. No injected code ever
claims a fence completed, sets sdkConsumed early, or bypasses adapter shutdown.
Failed genuine drains still retain/quarantine owners. Source control fixtures and
the scenario driver are authored but have not been run; GPU lifetime races,
teardown/device-loss, pixel output and physical presentation acceptance remain
open. Armed-fault observations stay ineligible for ordinary quality/performance
gates even when a fallback Present succeeds.

## External latency and pacing

### Continuous presenter-owner evidence

The Player now publishes one typed `cpu_presenter_return` record (axis 3) per
owner observation through the existing profiler producer pages, collector,
600-frame live window and full continuous writer. Snapshot v6/chunk 7 v2 and
continuous v7 persist and reopen this axis. Readers retain snapshot v1/v2/v4 and
continuous v3/v5 support; earlier formats reject the new axis. Axis 1/2 payloads
remain unchanged. The fixed 100-byte record carries a separate presenter payload,
not reinterpreted renderer provenance.

The container engine frame is the current recording frame when published. It is
not the source render frame. Full-width real/publication/view/scene identities,
requested/presenter generations, provider, configured interpolation count, fault
revision/mode, source-valid flag and raw status/native result remain together.
The sequence is process-local and counts owner observations outside recording
too; it is not a GPU or generated-frame identity. Unknown sources and failed or
not-queried outcomes remain records, not successful presentations.

`observedQpc` is sampled at the owner immediately after the native/SDK wrapper
returns, using the capture's exact `ticks_per_second` QPC clock. A wrapper can
include post-Present work, so this is neither an exact native API return nor an
API-call duration. No GPU completion, SDK final-consumption, OS displayed or
physical photon endpoint is implied. There is no generated output timestamp or
ordinal synthesized from the SDK's aggregate frame counts.

Admission follows existing CPU Stop/generation semantics. Owner pages share the
existing bounded pool and frame/Stop seal requests; no per-observation lock or
new unbounded queue is added. Pool/ring/collector/writer losses remain in existing
capture loss fields. The summary separately reports sequence gaps, ordering,
first/last observed sequence and completeness only within those bounds. A paused
or stalled owner can leave an explicitly incomplete tail. Frames before recording
admission or after Stop are outside the interval; neither boundary is filled in.

The existing `DxTimingCaptureLibrary` integration was inspected, not executed:
`DxTimingCollector.cpp` implements GPU execution/work and Dxgk object callbacks;
`DxCaptureProtocol.h` has session, execution begin/work/end, submission and status
records. Its present tokens are correlations, not OS displayed events. It does
not export per-output generated identities or display timestamps. Consequently
OS-displayed pacing still requires a verified external instrument with exact
frame association. Physical input-to-photon still needs physical endpoints.

There is no in-engine input-to-photon instrument. `temporal.latency` proves only
SDK marker observations. Aggregate generated counters and CPU Present returns
are also insufficient to reconstruct a presentation interval series.

1. Choose and verify the FG-off condition with the existing staged commands
2. Start the external instrument and run `Record-TemporalEvidence.ps1` against
   the Player endpoint with a new output directory
3. Repeat with FG on at the desired provider and interpolated count, retaining
   the same build, scene, motion workload, resolution, reconstruction and spatial
   settings; control experimental conditions independently
4. Keep each original instrument export unchanged. Convert timestamps into a
   CSV on the calibrated source clock, retaining exact source-frame IDs
5. Build the manifest below with SHA256 hashes, actual instrument details and
   explicit project acceptance limits. Run `analyze-temporal-evidence.py`

```powershell
./Tools/temporal-validation/Record-TemporalEvidence.ps1 -EndpointFile C:/Player/endpoint.json -OutputDirectory C:/Evidence/fg-off -Seconds 30
python ./Tools/temporal-validation/analyze-temporal-evidence.py C:/Evidence/latency.json --output C:/Evidence/latency-result.json
```

The recorder saves `actual.ceprof`, `presenter-responses.jsonl` and `capture.json`.
New recordings require stream v7. The importer uses the continuous axis-3 records
for source joins and rejects missing/lost/ambiguous series. Status polling remains
a control-change audit, not the source of every frame. Legacy v4/v5 evidence can
still use its sampled JSONL; missing identities remain unavailable.
It neither launches an instrument nor fabricates timestamps. Polling has overhead
and may skip control changes between polls; keep that overhead identical between
conditions. An immutable recorded presenter observation must match every imported sample's full
realFrameId/publicationFrameId/viewId/sceneEpoch. A missing observation is an
explicit blocker; do not substitute the latest snapshot or infer intervening
frames. Continuous presenter-owner records do not provide continuous display evidence.

Each presenter observation belongs to the actual presented source packet and
retains source request generation, presenter configuration generation, provider,
interpolated count, native gate and fault state. Source/presenter generations must
match, provider/count/control revision must be constant within a run, and the
actual Present result must succeed. Present success is used only to bind control
conditions, never as a photon endpoint or physical display timestamp. Renderer
profile measurements remain real-frame render costs and correctly have no FG
provider; they are not rewritten to look generated.

### Manifest contract

The input object has schema `temporal.external-measurement.v1`, kind `latency`
or `pacing`, `source`, `acceptance`, and exactly two `runs` (fgEnabled false/true).
All artifact references are objects with `path` and lowercase `sha256`; paths are
relative to the manifest or absolute. Hash mismatches, missing files, unknown
provenance, losses, mixed extents/effects and unmatched source identities reject.
Hashes bind supplied evidence; the importer cannot independently certify a
physical instrument, experimental workload or a raw-to-CSV conversion.

Required `source` strings: `instrument`, `instrumentId`, `toolVersion`, `clockId`,
`frameAssociationMethod`, `method`, `timestampUnit` (must be `ms`) and `axis`.

- Latency: method `photodiode`, `high-speed-camera` or `ldat`; axis
  `input-to-photon`; sharedClock true; nonnegative clockUncertaintyMs. The input
  endpoint is the physical input event, the endpoint is an observed photon event
  on the same calibrated clock. CPU/SDK marker endpoints are rejected
- Pacing: a physical method with axis `physical-display`, `presentmon-displayed`
  with axis `os-displayed`, or `sdk-present-timestamps` with axis
  `sdk-presentation`. These scopes remain distinct in the result; SDK spacing is
  not physical scanout. No aggregate SDK count or CPU-call timestamp method exists

Each run requires `runId`, Boolean `fgEnabled`, artifact references `profile`,
`presenterObservations`, `rawArtifact`, `samples`, and a nonempty `conversion`
description identifying how original instrument data became the CSV. `profile`
must be a complete eligible v6/v7 capture, or legacy v4/v5 with sampled evidence.
`presenterObservations` is the recorder's
unaltered response JSONL. Original raw data and normalized CSV may be the same
file if the instrument already writes the required calibrated columns.

Latency CSV columns:

```text
sampleId,realFrameId,publicationFrameId,viewId,sceneEpoch,inputTimestampMs,photonTimestampMs
```

Pacing CSV columns, in observed timestamp order:

```text
sampleId,realFrameId,publicationFrameId,viewId,sceneEpoch,frameKind,generatedOrdinal,presentationTimestampMs
```

The frame IDs on a generated presentation identify its source real frame. A real
presentation has ordinal 0; a generated presentation has ordinal 1 through the
actual presenter's configured count. IDs/ordinals may not repeat. FG-on pacing
requires observed generated presentations. This association must be supplied by
the measurement source; aggregate DLSS/FSR counters cannot supply it. Lack of
source association remains missing evidence, not a synthetic generated identity.

`acceptance.minimumSamples` must be at least 20. Latency additionally requires
`maximumOnP95Ms` and `maximumP95RegressionMs`; source clock uncertainty is added
conservatively to these comparisons. Pacing requires
`maximumOnP95IntervalMs` and `maximumOnIntervalMs`. Limits must be chosen for the
project before interpreting results. The importer reports off/on distributions,
their p95 difference and acceptance only for the declared source axis. It does
not establish MFG output ratio, all hardware coverage or physical latency from
an SDK-presentation source.
