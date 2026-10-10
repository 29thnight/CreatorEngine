# RG8 graphics-only texture state retention — 2026-10-09

Base: `d5a6a26a78da93686b8168429183ee7e0d50a8de` plus the recorded local history, dependency scratch and alternative-order changes. Default OFF / progress remains unchanged.

## Finding and change

The queue barrier planner returned every used resource to COMMON at every batch boundary. This included textures used only by graphics, even when a boundary was unrelated to that texture and all its accesses remained on one FIFO graphics queue. Before adding a guessed cost penalty, remove this proven avoidable work.

`RHITextureInfo::retainsExplicitStateOnGraphicsQueue` is a conservative backend guarantee, default false. DX12 reports true only for actual texture resources without ALLOW_SIMULTANEOUS_ACCESS. The guarantee concerns explicitly transitioned states; automatic promotion is not used by this optimization. Buffers and unknown backends retain existing behavior.

The selected queue plan classifies resources from all live pass and repeated-phase declarations. A texture is eligible only if the backend guarantee is present and no compute pass uses it. Eligible textures begin in their tracked initial state, keep their explicit state between graphics batches, and restore the compiled final state in the graphics epilogue. State and previous-write tracking remain continuous. All buffers, compute-only or shared textures and unverified textures continue using COMMON at boundaries. No read/read queue-sharing rule or producer/consumer edge changes.

Reference: [Microsoft D3D12 resource state promotion and decay](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12). In particular buffers and simultaneous-access textures decay across ExecuteCommandLists even after explicit transitions, while ordinary explicitly transitioned textures do not. Excessive COMMON transitions can hurt GPU performance.

## Validation scope

The native fixture now clears one graphics-only render-target texture before and after a compute batch. It checks the backend guarantee and absence of the redundant second state transition, retains the existing buffer ordering/decay checks, verifies final-state writeback and validates actual GPU execution. A native simultaneous-access texture also verifies the backend refuses the retention guarantee. Native semantic checks increase from 428 to 434; the strict wrapper is updated with the test. Planning checks remain 119.

Build: VS 2026 Release, `Build/rg8-graphics-retention.log`. Evidence directory: `Build/Verification/Phase43/RG8GraphicsRetention20261009/`. Final runtime, output and timing results are appended after verification.

The handoff prediction model is still uncalibrated and has not been replaced by this slice. Reducing actual boundary work is distinct from proving an accurate batch/barrier cost model or accepting async performance.

## Release verification and measurements

Final VS 2026 Release build passed (`Build/rg8-graphics-retention-probe.log`; initial enum naming and per-resource probe corrections are preserved in earlier build/evidence files). Native execution 434 checks, planning 119 checks and full RenderGraph regression passed with GPU validation (`native-final`, wrapper exit 0). Existing Utility_Framework PDB LNK4020 warnings and profiler shutdown `abandoned=1 retained=1 foreign=0` diagnostic remain separate from GPU validation. Debug and Vulkan execution were not tested in this slice.

The product same-process forward 0/1/2 and reverse 2/1/0 matrix collected all 192 accepted chronological normal samples (32 per segment), with complete history. Schedules, barrier accounting and calibrated GPU intervals passed the independent audit. Clocks were not locked; timings exclude GPU validation/readback and retain evidence serialization.

| Order | Mode 0 GPU median / p95 | Mode 1 | Mode 2 |
|---|---:|---:|---:|
| Forward | 2.3603 / 2.7136 ms | 2.4914 / 2.6061 ms | 2.7029 / 3.1129 ms |
| Reverse | 3.0295 / 4.1503 ms | 3.1985 / 3.2717 ms | 2.9543 / 4.7647 ms |

Mode 2 selected compute in 64/64 frames. Only 5/64 frames had overlap above clock error (all reverse), with median zero in both orders and maximum 0.258 ms. Forward async remains slower; reverse async has a slightly lower median but a higher p95. This does not establish a consistent performance benefit. Default OFF / progress / earned 0 remains.

Boundary-work reduction (both orders, median): planned barriers **767 -> 422**, prologue **109 -> 49**, epilogue **117 -> 57**. The previous numbers are from the immediately preceding alternative-order run; pass assignments are cost-driven and can vary, so these are observed product counts, not a fixed per-frame savings guarantee. The changed planner removes only provably unnecessary graphics texture transitions and preserves buffer/cross-queue safety.

Current mode-2 scheduling CPU median: 1.1312 / 1.1417 ms; recording 1.5160 / 1.6390 ms; submission 0.5959 / 0.5780 ms. Planned batches remain median 11 and actual waits 4. Absolute GPU-time changes across builds are not controlled A/B results, as the mode-0 baseline also moved substantially.

Evidence: `normal/`, `performance-summary.json`, `boundary-summary.json`, `native-final/`, binary metadata and `local.patch` under `Build/Verification/Phase43/RG8GraphicsRetention20261009/`. No commit/push is performed by this request.

## Product output and GPU validation

GPU-based validation captured forward modes 1 and 2, four samples each: two processes exited 0, both reported problems 0 / dropped messages 0. Current mode 1 versus mode 2 is bit-identical across 64 attachment/stage comparisons. This validation wrapper intentionally covers only forward modes 1/2; it is not a six-mode validation matrix.

The initial comparison against the previous build mixed validation OFF baseline with validation ON current output and failed: maximum absolute difference 0.003921572 (one display quantization step). That failed result is preserved in `gpu-output-comparison.json`; it is not counted as an accepted cross-build comparison.

A fresh validation-OFF capture matrix completed both orders and modes 0/1/2: six processes exited 0, 24 captures. Controlled inputs, dimensions and finite values were checked. Previous mode 0 versus current mode 0 and current mode 0 versus modes 1/2 passed **384 exact comparisons, error 0** (`output-comparison.json`). A separate direct previous/current mode-1 and current mode-1/2 comparison passed 128 exact comparisons, error 0 (`mode1-normal-comparison.json`). These comparison sets reuse captures and are not independent additional samples.

The same current binary with validation OFF versus ON reproduced the 0.003921572 maximum difference in modes 1 and 2 (`validation-flag-comparison.json`, diagnostic only). Matching validation configurations establish output equivalence; the mixed-configuration difference does not establish a regression caused by this change. This probe does not identify the exact instrumented shader instruction responsible for the difference.

Product output equivalence and GPU validation do not establish GPU performance improvement. RG8 remains progress / earned 0 / default OFF. The remaining performance work is to explain the gap between predicted and measured overlap, account for actual batch synchronization/barrier costs, and verify CPU recording/submission costs before adoption. No new phase completion condition was added by this slice.
