# RG8 PR167 follow-up: execution evidence and acceptance contracts

Status: Scheduler CPU reduction and Debug/Release native correctness/workload evidence verified on 2026-10-11; controlled performance and adoption **unvalidated**.
Async compute remains **default OFF**. RG7 transient aliasing remains explicitly unsupported by the owned queue executor. This document does not close RG8, award earned days, or supersede the historical measurements in `RenderRg8Barriers20261009.md`.

The original sections below describe the 2026-10-09 source-only follow-up. Their
"not run" statements apply to that date. The 2026-10-11 checkpoint appended below
records actual current-binary execution and the remaining acceptance boundary.

## Changes

### Stable measurement identity

GPU profiler display aggregation remains unchanged. Scheduler costs use the raw slices' graph-declaration signature and authored pass slot instead. Two passes with the same display name retain different identities; split pieces of one pass contribute to that one slot. Literal names such as `name(x2)` are not parsed or rewritten.

The signature is a length-delimited exact serialization of graph declarations, not a hash or a pointer to a graph. It excludes frame epochs and native handle values. Delayed profiler collection retains only an immutable signature, never the graph's resources. The live cache stores complete snapshots for bounded exact-signature variants per view, rejects incompatible signatures and partial/invalid timing collections, bounds retained views, and expires old source-frame measurements. A successful resize invalidates measurements because imported native dimensions are not part of the declaration signature.

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

## 2026-10-11 executed workload checkpoint

Release/DX12 offscreen verification passed on the current PR169 working-tree
binary. Debug was not rebuilt or accepted by this checkpoint. The reused private
`Mat9Robot.creator` scene contains three Aria geometry batches; the private
`RG8Empty.creator` provides the actual warm negative workload. Native AA was used,
with frame generation, spatial scaling and DVC disabled. This is not an SDK matrix
or a master-build performance comparison.

- Native queue execution: **428 checks**, queue scheduling: **117 checks**, full
  render-graph regression, normal exit and D3D12 GPU validation passed.
- Actual viewports: **332×152** and **1108×512**, with **192 ordinary frames per
  viewport**. Each same-process order, 0→1→2 and 2→1→0, uses the first 32 eligible
  chronological observations per mode after ordinary warmup; capture submissions
  do not supply the normal performance population.
- Four captures per mode/order passed strict attachment and diagnostic-stage
  equality, finite values and exact submitted-plan/timing identity. Instrumented
  validation captures also match the uninstrumented mode-0 reference. Main-scene
  geometry recording has three batches; the larger view has 2,500 foreground depth
  pixels. The negative scene has zero draws and cleared depth.
- The canonical workload gate passed both actual normal-frame positive overlap
  and complete-cost negative `insufficient-gain` fallback. Each proof retains four
  actual qualifying submissions from its ordinary warm observation window. These
  are existence proofs, separate from the 32-frame performance population. GPU
  validation reported zero problems and zero dropped messages. Native timing
  admission accepted 384 retained observations and rejected 52 damaged inputs.

This run repaired three source defects: alternating SSGI history-buffer roles
invalidated a single-signature cost cache; delayed GPU timing used a mutable
backend generation instead of its submitted generation; capture JSON serialized
`normalFrameComparable` as numeric zero. Exact signatures now have a bounded
four-variant per-view history, without mixing pass costs or weakening age/domain
guards. Six new history contract checks are included in the 117 scheduling checks.
Submission generation is frozen, and the manifest contains a typed `false`.

Two harness failures were reviewed before retry: instrumented shader preparation
took about 79 seconds and exceeded the capture's 60-second Pending deadline, and
an endpoint replacement raced its initial file read. Ordinary valid-frame readiness
now precedes readback, and endpoint I/O retries within the existing startup bound.
The capture deadline remains enforced. Failed logs and actual retry paths are
retained; failed runs were not relabeled as successful executions.

Evidence: `Artifacts/PR169/rg8-workload-20261011/rg8-workload-checkpoint-20261011.json`,
both `verified` and `full-viewport` strict audit reports, native full-regression
results, workload manifests and retention ledgers. Final cleanup recovered
**3.42 GiB** by actual drive free-space change and left **111.54 GiB** free.
The task directory has about **2.30 GiB of unique file lengths**, including about
**0.47 GiB** of representative raw pixels; hardlinked logs are counted once.
Historical verdicts/logs/hashes remain, but removed samples cannot be pixel-replayed.

Both canonical reports retain `evidenceValid=true`, `performanceValidated=false`,
`adoptionEstablished=false` and `phaseComplete=false`. Actual overlap is established;
net speedup is not. Full CPU measurements include opt-in evidence serialization.
Debug acceptance, cost-model calibration, controlled net-performance comparison
and RG7 combined lifetime/aliasing acceptance remain open. Default OFF and RG8
progress/zero earned days are unchanged.

## 2026-10-11 Debug workload checkpoint

The subsequent current-source Debug build and complete DX12 workload matrix
passed. The collector now accepts `-Configuration Debug|Release` (default Release),
records that configuration, and hashes the runtime DLL alongside the selected
executable both before and after collection. Previously both executable selection
and the final runtime immutability guard were fixed to Release paths.

The existing `Bin/x64-Debug` and build intermediates were reused. The private Aria
and empty scenes were also reused, with actual viewports 1108×512 and 332×152
respectively. SDK composition is outside this native-AA/FG-off checkpoint.

- Debug native execution **428 checks**, scheduling **117 checks**, full render
  graph regression and normal exit passed.
- The main scene supplied **192 ordinary frames** across same-process forward
  and reverse mode orders. The empty negative scene supplied **32 ordinary
  observations**. All 224 passed native measurement admission; **52 mutations**
  were rejected without changing the source observations.
- Four captures per required mode/order passed exact attachment/stage equality
  and submitted-plan/timing joins. GPU-validation captures also match their
  uninstrumented mode-0 references. Model recording has three meshlet batches
  and 2,500 foreground pixels; the negative scene has zero draws/foreground.
- Both actual workload gates passed: 28 qualifying positive-overlap submissions
  and 651 complete-cost negative fallback submissions occurred in the ordinary
  observation windows. Each workload manifest uses four qualifying submissions
  as existence proof, not as the normal-frame performance population.
- GPU validation reported zero problems and zero dropped messages. All six
  collection families completed without a retry in this Debug run.

Debug mode-2 scheduling CPU medians are **78.556 ms forward / 77.819 ms reverse**;
mode-1 scheduling medians are **0.199 / 0.189 ms**. These are executor scheduling
timers, separate from opt-in adapter log serialization. Full graph CPU medians
are 94.409/93.474 ms in mode 2 and 12.864/12.520 ms in mode 1; those full-scope
numbers include evidence serialization. The active placement heuristic is the
next measured CPU bottleneck to address. Actual overlap does not establish net
speedup, and this checkpoint does not accept default async-compute adoption.

After both strict audits passed, redundant pixels were pruned while retaining
eight representatives, all metadata/logs and normal-frame evidence. Actual drive
free-space change recovered **3.12 GiB**, leaving **105.79 GiB** free. Unique file
lengths total about **0.69 GiB**, including about **0.45 GiB** of raw pixels; this
is not an allocated-space or recovery estimate. The retained representatives were
read back and checked for correct payload sizes and finite values after pruning.

Evidence: `Artifacts/PR169/rg8-debug-workload-20261011/rg8-debug-workload-checkpoint-20261011.json`,
`matrix-audit.json`, `normal-final/adoption-measurements.json` and `final-retention.json`.
Debug correctness/workload acceptance is now closed. Cost-model calibration,
scheduler CPU reduction, controlled net-performance acceptance and RG7 combined
lifetime/aliasing acceptance remain open. Default OFF, progress and zero earned
days remain unchanged.

## Scheduler CPU follow-up — 2026-10-11

This supersedes the preceding scheduler CPU backlog. The default stays OFF;
controlled net-performance adoption and RG7 combined acceptance remain open.

The placement heuristic still trials the same candidate components and individual
toggles, using current complete measurements and the same minimum-gain guard.
Planning scratch is local to one invocation and resets every dependency epoch,
queue clock and trial state. Adjacency lists, epoch frontiers and simulation arrays
reuse storage between trials. The two-queue ready/future heaps preserve earliest
start, compute-ancestor priority and compiled-position ties, including the change
to tie ordering when the queue clock clamps multiple readiness times. Simulation
now uses O(P log P + E) selection rather than repeated whole-frontier scans.
Byte flags, fixed span traversal and stable pointer ranges for sorting/heaps reduce
MSVC Debug iterator bookkeeping without changing build-wide iterator settings.
No plan or measurement is reused across frames by this scratch object.

Actual planning dependency/simulation CPU subtotals and placement-trial counts
are logged separately from GPU estimates. The strict auditor requires finite,
nonnegative subtotals bounded by the enclosing scheduling duration. A hand-derived
67-pass wide-frontier fixture checks ancestor hoisting, declaration ties, analytic
span and restoration after a measurement-triggered serial fallback. Scheduling
coverage is now 121 checks. The standalone plan wrapper also reports the correct
121 checks (its old numeric metadata still said 111).

| Configuration | Earlier schedule p50, forward/reverse | Current schedule p50, forward/reverse | Reduction | Earlier/current full CPU p50, forward/reverse |
|---|---:|---:|---:|---:|
| Debug | 78.556/77.819 ms | 16.082/16.140 ms | 79.5%/79.3% | 94.409/93.474 / 31.282/31.512 ms |
| Release | 3.319/3.198 ms | 0.783/0.726 ms | 76.4%/77.3% | 6.316/6.096 / 3.623/3.829 ms |

Each order/mode uses the first 32 eligible chronological ordinary observations
from 64 candidates after 16 ordinary warm observations. Both final configurations
passed 428 native execution checks, 121 scheduling checks, the full render-graph
regression, 192 model observations and 32 empty-scene observations. Strict pixel,
stage, queue/common-clock, real positive-overlap and negative-fallback oracles and
GPU validation passed; output max absolute error is zero. Per configuration,
224 unchanged actual snapshots passed admission and 52 damaged variants were
rejected. The corrected standalone Debug plan wrapper also passed.

These are historical before/after CPU measurements on the same private scene and
viewport, not controlled old/new-binary ABBA or master-build performance evidence.
Full CPU scopes include opt-in evidence serialization; executor scheduling timers
exclude adapter log serialization. CPU reduction is verified, while overall net
speedup and adoption remain unvalidated. The remaining Debug dependency-planning
cost and placement-search budget, handoff/batch/contention cost-model calibration,
conservative boundary transitions and RG7 composition are still open. RG8 remains
progress with zero earned days.

Evidence: `Artifacts/PR169/rg8-scheduler-cpu-20261011/rg8-scheduler-cpu-checkpoint-20261011.json`,
final `Debug-optimized` and `Release` matrix audits, normal adoption-measurements,
native/mutation results and retention ledgers. Intermediate `Debug`/`Debug-final`
verdicts are preserved but their superseded raw pixels are no longer replayable.
