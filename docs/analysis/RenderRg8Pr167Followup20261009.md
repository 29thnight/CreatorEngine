# RG8 PR167 follow-up: execution evidence and acceptance contracts

Status: implementation and acceptance tooling revised; native correctness and performance **unvalidated**.
Async compute remains **default OFF**. RG7 transient aliasing remains explicitly unsupported by the owned queue executor. This document does not close RG8, award earned days, or supersede the historical measurements in `RenderRg8Barriers20261009.md`.

## Changes

### Stable measurement identity

GPU profiler display aggregation remains unchanged. Scheduler costs use the raw slices' graph-declaration signature and authored pass slot instead. Two passes with the same display name retain different identities; split pieces of one pass contribute to that one slot. Literal names such as `name(x2)` are not parsed or rewritten.

The signature is a length-delimited exact serialization of graph declarations, not a hash or a pointer to a graph. It excludes frame epochs and native handle values. Delayed profiler collection retains only an immutable signature, never the graph's resources. The live cache replaces complete per-view snapshots, rejects incompatible signatures and partial/invalid timing collections, bounds retained views, and expires old source-frame measurements. A successful resize invalidates measurements because imported native dimensions are not part of the declaration signature.

Opaque callback inputs and dynamic GPU contention are still outside this identity. A stable declaration does not establish a stable workload or a performance win.

### Model boundaries

Overlap placement requires a valid timing sample for every executed pass. A measured zero-length empty scope remains distinct from an unavailable sample; no artificial positive cost is invented. Missing timings and saturated arithmetic keep the original graphics order. Time additions saturate rather than wrap. The initial compute wait on the graphics prologue is included as a lower bound. Disabled compute and incomplete timing use a linear serial-cost sum and avoid the trial ready-list scheduler entirely.

The 20 microsecond handoff and 50 microsecond minimum-gain defaults are **uncalibrated analytical assumptions**. `predictionCalibrated` stays false. Neither a positive predicted gain nor a compute submission is an observed overlap or speedup. Candidate placement remains a heuristic with repeated ready-list scans; it does not optimize all assignments and does not model detailed GPU contention, all boundary transitions, or every extra submission cost. Scheduling CPU time is now visible rather than hidden inside an unmeasured gap.

### Actual submitted plan

The graph retains a value-only diagnostic report of its specialized schedule, recorded batches, per-batch barriers, reduced queue waits, and which submissions/waits were accepted. The same precomputed batch/wait list drives native execution and evidence publication. Prefix and final-join waits are explicit. No diagnostic batch or timeline retains graph/resource/fence ownership.

Counts come from the actual batch list. They are not derived from mode, a fixed number of compute passes, or `3 * graphs + 2 * computeBatches`. Barriers include the actual graph boundary plans and repeated phases. Empty prologue/epilogue command lists are still present in the current executor and remain visible in the counts.

`completed` means all graph batches were accepted. It is not GPU completion proof. Submission rejection keeps partial accepted counters and `recoveryRequired`; existing quarantine and no-retry behavior remains. Final resource-state writeback occurs only after all submissions are accepted.

The live Inspector copies the graph after `SubmitOwnedGraph` specializes its order, lifetime positions and barriers. A failed frame still follows the existing failure/cleared-publication path. PBR capture and the Inspector therefore refer to the same specialized graph on success.

### Honest CPU and GPU timing

Queue execution reports separate scheduling, recording, submission, and total CPU durations, including rejected attempts. Submission time includes owned queue operations' CPU submission-thread ticket waits. Those are **not CPU waits for GPU completion**, and they are not GPU queue-wait durations.

Live record-plus-submit timing covers both the original mode-0 path and the owned path through `EndFrame`. This is separate from narrow native recording time and compile time. Optional per-frame evidence is gated by `CREATOR_RG8_EVIDENCE=1`; ordinary rendering does not emit these diagnostic log records. Diagnostic capture/readback overhead must be excluded from normal-frame performance acceptance.

A frame with compute submissions is reported as compute-submitted. Real overlap requires nonzero intersection of actual graphics and compute pass intervals on a validated common CPU-clock axis. Different queues' raw GPU ticks must never be subtracted from each other.

## Authored acceptance coverage

The queue-plan and native wrappers pin versioned success identities, exact deterministic check counts, ordered command results, and binary/source metadata. A loose success substring or arbitrary check count is insufficient.

The native fixture selects Overlap explicitly. Its positive case records independent graphics work before compute-chain consumers in the new order, dispatches real GPU payload work, reads back both chains, and validates imported final states, batch/wait accounting and retention. Negative cases cover no-benefit graphics fallback and preserved declaration order. Fault cases exercise recording rejection and partially accepted queue work with quarantine/retirement safety. These are **authored tests**, not results from this change.

Live/adoption tooling consumes actual per-frame plans and calibrated timing evidence. Product-scene mode 2 may legitimately fall back. Adoption still requires both a known positive overlap workload and a known negative fallback workload; an implementation that always falls back must fail. Strict image comparison, validation messages, queue retirement, pool accounting and barrier accounting remain mandatory.

## Required future validation

No builds, compilers, linkers, shader compilation, test execution, executable probes, native GPU runs, or benchmarks were performed for this follow-up. Static review cannot establish compilation or GPU correctness.

Before native/performance acceptance:

1. Build the exact appended head on Windows in Debug and Release after authorization. Preserve commit, executable, runtime, shader and source identities.
2. Run the versioned queue planning and native execution wrappers, full render-graph regression, and D3D12 GPU validation. Retain raw results rather than only summary text.
3. Collect actual-schedule and common-clock interval evidence for an overlap-capable workload and a serial/fallback workload. Neither analytical test costs nor `computeBatches > 0` satisfy the positive overlap gate.
4. Calibrate overhead assumptions, control/report GPU clock and power conditions, and collect normal-frame p50/p95 for GPU end-to-end span and CPU compile/schedule/record/submit cost.
5. Evaluate **mode 0 versus mode 1** as executor/parallel-recording overhead separately from **mode 1 versus mode 2** as queue-placement benefit. Graphics fallback in mode 2 still uses the owned executor; it is not mode-0 performance parity.
6. Keep RG7 guards until queue-aware resource lifetimes and aliasing savings have their own validation. Do not remove a safety guard to obtain a passing combined run.

Known structural work remains: mode-0 parallel recording is not integrated into owned execution, cross-queue physical resource uses (including read/read) remain conservatively serialized, and model calibration has not been established. These limits are separate from the implementation fixes above.

## Evidence collection contract (authored, not run)

The updated harnesses remain unvalidated source changes. No command below was
executed for this follow-up. Existing historical results are not schema-2 evidence.
Queue execution remains opt-in/default OFF.

`measure-rg8-adoption.ps1` now sets `CREATOR_RG8_EVIDENCE=1`, records complete
per-submission `[rg8.execution]`, `[rg8.timing]`, and `[rg8.cpu]` JSON, and preserves
these records in each run's `queue-evidence.json`. `-ScenePath` selects an explicit
real workload scene; omitting it retains TestShadow. Selecting a scene does not
prove that it can overlap. A workload that always falls back cannot satisfy the
positive gate.

A future authorized Windows collection uses separate fresh output directories:

```powershell
./Tools/regression/measure-rg8-adoption.ps1 -OutputDirectory <captures>
./Tools/regression/measure-rg8-adoption.ps1 -OutputDirectory <normal> -TimingOnly
./Tools/regression/measure-rg8-adoption.ps1 -OutputDirectory <validation> -ValidateGpu
# Repeat the collection with -ScenePath <positive-scene> and <negative-scene>
# for explicitly prepared overlap-capable and known non-overlappable workloads.
python Tools/regression/audit-rg8-adoption.py <captures> <normal> `
  --native-evidence <same-binary-native-full-regression> `
  --validation-evidence <validation> --workload-evidence <workload-evidence.json>
```

The supplied workload manifest is a required evidence index, not a test result or
an automatically generated success declaration. Its exact schema is:

```json
{
  "schemaVersion": 2,
  "binary": { "head": "<exact source SHA>", "exe": "<SHA256>", "runtime": "<SHA256>" },
  "workloads": [
    {
      "kind": "positive-overlap",
      "stdout": "<positive normal mode-2 run>/stdout.log",
      "runBinary": "<positive normal collection>/binary.json",
      "validationBinary": "<positive validation collection>/binary.json",
      "captureBinary": "<positive capture collection>/binary.json",
      "nativeEvidence": "<same-binary native run>/execution-result.json",
      "validationEvidence": "<positive validation mode-2 run>/validation.json",
      "pixelComparison": "<exact mode-0 versus mode-2 comparison>/comparison.json",
      "frames": [
        { "backendGeneration": 1, "frameId": 100, "viewId": 1,
          "submissionId": 100, "captureGeneration": 0 }
      ]
    },
    {
      "kind": "negative-fallback",
      "stdout": "<negative normal mode-2 run>/stdout.log",
      "runBinary": "<negative normal collection>/binary.json",
      "validationBinary": "<negative validation collection>/binary.json",
      "captureBinary": "<negative capture collection>/binary.json",
      "nativeEvidence": "<same-binary native run>/execution-result.json",
      "validationEvidence": "<negative validation mode-2 run>/validation.json",
      "pixelComparison": "<exact mode-0 versus mode-2 comparison>/comparison.json",
      "frames": [
        { "backendGeneration": 1, "frameId": 100, "viewId": 1,
          "submissionId": 100, "captureGeneration": 0 }
      ]
    }
  ]
}
```

All placeholder values and frame identities must be replaced with actual retained
records. Paths are relative to the manifest. The comparison report is produced by
`compare-material-captures.py`; the new gates require every attachment's `maxAbs`
and `nonfinite` to be zero even if that script's older tolerance gate passes.
Native evidence must be current schema 2 and include full regression, reordered
execution/failure, declaration-order, shared-read ownership, and retirement
coverage. It does not itself claim calibrated overlap.

The positive workload needs actual compute batches, complete measured pass costs,
and positive intersection of the two queues' unioned raw pass intervals in the
calibrated QPC domain, larger than reported calibration drift. The negative
workload must have complete measured costs and select no compute; a cold start
with missing cost history cannot satisfy it. Timing/queue identities, all batch
and wait counts, barriers, and recording-pool leases are reconciled with the
executed plan. Diagnostic capture submissions cannot masquerade as normal frames.

`verify-rg8-live-queues.ps1 -AsyncCompute` also requires `-WorkloadEvidence` with
this manifest. It may accept a particular TestShadow frame falling back only when
the separate positive and negative workload gates are both satisfied. Until those
real artifacts exist, this strict gate is intentionally blocked rather than
claiming an unmeasured GPU overlap result.

The report distinguishes 0→1 owned-executor overhead from 1→2 compute-placement
delta, with ordinary full record/schedule/submit CPU and graph scheduling CPU.
Full CPU timing includes opt-in adapter evidence serialization; graph executor
phase timings exclude that serialization. Process-separated forward/reverse
measurements, drift indicators, and the uncalibrated cost model do not establish
speedup. Even valid evidence reports `evidenceValid=true`,
`performanceValidated=false`, `adoptionEstablished=false`, and
`phaseComplete=false`; controlled performance/calibration acceptance remains open.
