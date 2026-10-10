# RG8 PR167 follow-up: execution evidence and acceptance contracts

Status: VS 2026 Debug/Release builds and schema-2 native correctness/full regression passed on 2026-10-09 after the fixture clear-value correction below. Product overlap and performance remain **unvalidated**. The original static-only record below describes the PR authoring stage.
Async compute remains **default OFF**. RG7 transient aliasing remains explicitly unsupported by the owned queue executor. This document does not close RG8, award earned days, or supersede the historical measurements in `RenderRg8Barriers20261009.md`.

## Changes

### TestShadow performance run after native validation (2026-10-09)

Measured the same Release binary from the Windows validation below, with GPU validation OFF and no concurrent build or GPU test. This compares current mode 0 (baseline), 1 (owned graphics executor), and 2 (overlap scheduler), not a rebuilt pre-PR binary. TestShadow at 1496x692 was run in separate processes in forward 0/1/2 and reverse 2/1/0 order, with 32 unique normal submissions per case (192 total). NVIDIA RTX 4070 Ti/595.97 clocks and power were sampled. Clocks were not locked, and this is not a same-process randomized comparison; the numbers below are observations, not a causal speedup percentage.

| Order/mode | GPU span median / p95 ms | Full record-submit CPU median / p95 ms |
|---|---:|---:|
| Forward 0 | 3.0080 / 3.6188 | 0.7989 / 1.1046 |
| Forward 1 | 2.9793 / 3.3679 | 1.1593 / 1.5392 |
| Forward 2 | 2.7182 / 3.2481 | 1.1120 / 1.7007 |
| Reverse 2 | 2.8227 / 3.4509 | 1.2185 / 1.6168 |
| Reverse 1 | 3.0761 / 3.2983 | 1.0766 / 1.4813 |
| Reverse 0 | 3.1565 / 3.9086 | 0.8812 / 1.1532 |

The mode-2 normal samples had **zero compute submissions and zero measured overlap in all 64 frames**. Only one of those 64 schedules had complete cost prediction input; the remaining 63 fell back with incomplete input. Thus this is not proof that a fully informed scheduler finds TestShadow unprofitable. Across the full two mode-2 logs, two earlier submissions selected compute; both also reported zero measured overlap. No async-compute performance gain was demonstrated. Full CPU median increased by 0.3131/0.3373 ms versus mode 0 in the two orders; this includes opt-in evidence serialization. Mode-2 scheduling median was 0.0081/0.00835 ms, recording 0.5615/0.5825 ms, and submission 0.2333/0.24425 ms.

Separate controlled captures (4 per case, 24 total) matched graph inputs and all 7 float attachments exactly: **168 comparisons, maxAbs=0, no nonfinite output**. These diagnostic captures are excluded from the normal timing distributions. All six timing and six capture processes exited zero.

The strict per-run adoption audit failed for owned modes with `Timing has no matching actual schedule`. There were 1-2 unmatched early timing records per run, around backend-generation changes. Selected normal samples had matching execution/timing/CPU identities; they were summarized separately without calling the strict audit a pass. The generation mismatch cause and missing normal cost inputs require follow-up. A raw timing difference with no compute work must not be presented as RG8 overlap benefit.

Evidence: `Build/Verification/Phase43/RG8Pr167Perf20261009/` contains normal/capture runs, exact binary identities, `performance-summary.json`, `pixel-summary.json`, GPU state samples, and the retained summarization scripts. The results do not close RG8 or establish product adoption. Existing process-separated tooling cannot satisfy the requested controlled same-process performance gate.

### Windows validation after synchronization (2026-10-09)

- Fast-forwarded master from `2965dc48` to `4bb4978897e2c99ae7f64233d3e691b0a113ad9b` (PR #167). Existing local GCCE/command test edits were preserved and included in the build; this is not a clean-checkout-only result.
- VS 2026 v18 MSBuild built `Editor/CreatorEditor.vcxproj`, x64 Debug and Release. Release reported LNK4020 warnings for existing `Utility_Framework.pdb` type records; build exit code was zero. ScriptCore trimming warnings were also retained in the logs.
- The first Debug native run failed the zero-validation-message gate: all seven new overlap cases cleared the final render target to `(0.25, 0.5, 0.75, 1)` while its optimized creation clear value remained zero. Set the fixture's creation clear value to the actual clear value. No warning filters, payload expectations, or acceptance thresholds were relaxed.
- After correction, both configurations passed `verify-rg8-queue-execution.ps1 -FullRegression`: native 361 checks, queue-plan 82 checks, full RenderGraph regression, payload error zero, validation problems zero, process exit zero.
- Evidence root: `Build/Verification/Phase43/RG8Pr167Sync20261009/`. The initial failed run remains in `native-debug`; successful runs are `native-debug-fixed` and `native-release`, each with raw command results, logs and binary/source hashes in `execution-result.json`. Build logs and the pre-test local diff are retained beside them.
- This run did not measure product-scene overlap or GPU speedup. Both receipts retain `measuredOverlapEstablished=false`, `liveSceneQueueCutover=false`, and `phaseComplete=false`. RG8 remains incomplete/default OFF.

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
