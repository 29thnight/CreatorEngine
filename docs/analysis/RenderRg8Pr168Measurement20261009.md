# RG8 PR168: Release product measurement

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/audit-rg8-adoption.py:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-rg8-adoption.py#L25) — `raise ValueError(message)`
- [Tools/regression/audit-rg8-adoption.py:56](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-rg8-adoption.py#L56) — `"skybox": Path(manifest["skyBoxPath"]).name}`
- [Tools/regression/audit-rg8-adoption.py:121](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-rg8-adoption.py#L121) — `assignment = {p: (i, b["queue"]) for i, b in enumerate(batches) for p in b["passes"]}`
- [Tools/regression/verify-rg8-queue-execution.ps1:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-rg8-queue-execution.ps1#L17) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-rg8-queue-execution.ps1:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-rg8-queue-execution.ps1#L18) — `$out = [IO.Path]::GetFullPath($OutputDirectory)`
- [Tools/regression/verify-rg8-queue-execution.ps1:75](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-rg8-queue-execution.ps1#L75) — `$process = [Diagnostics.Process]::Start($start)`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Source: `d5a6a26a78da93686b8168429183ee7e0d50a8de`, Windows, VS 2026 v18,
RTX 4070 Ti / driver 595.97. TestShadow, DX12, 1496 x 692.

**Performance acceptance failed.** The product scheduler usually has no usable
pass costs. This collection does not establish async speedup or close RG8.

Follow-up: the history rejection below was traced to SSGI physical ping-pong
declaration order and fixed. See [history fix and remeasurement](RenderRg8HistoryFix20261009.md).
The historical measurements on this page remain unchanged.

## Build and native correctness

The initial Release build failed the command registry's sorted-name assertion:
PR168 inserted `render.queue.mode` before `render.live.fence`. Moving the new
entry after `render.pbr.uv` fixes the ordering without changing its behavior.
The subsequent VS2026 Release build passed. Existing Utility_Framework PDB
LNK4020 and managed trimming warnings remain.

This was a dirty-worktree build, including the preserved GCCE/local test edits
and the earlier optimized-clear-value fixture correction. It is not a pristine
PR checkout result. The original patch/status and binary hashes are retained.

Release `verify-rg8-queue-execution.ps1 -FullRegression` passed: 428 execution
checks, 111 schedule checks, full RenderGraph regression and native GPU validation.
The wrapper did not require measured overlap. These native results do not prove
product performance or product GPU-validation acceptance.

## Normal-frame collection

Two Editor processes ran the forward 0/1/2 and reverse 2/1/0 sequences. Within
each process, `render.queue.mode` switched at render-owner frame boundaries.
Each segment warmed 16 ordinary frames and collected 64 chronological candidate
snapshots. GPU validation and image capture were disabled during timing. GPU
clocks were not locked; sparse clock/power observations are retained, not a
continuous frequency-control proof. Mode 0 is the existing executor; mode 1 is
primary-graphics queue execution; mode 2 requests overlap placement.

The original wrapper aborted after the first insufficient segment. The retained
initial run is under `normal`. The wrapper was then changed to collect all mode
diagnostics before failing. It still requires 32 eligible samples per segment,
returns failure when any segment lacks them, and leaves acceptance false. The
complete diagnostic attempt is under `normal-complete-diagnostics`.

All 384 candidate identities and calibrated intervals were independently checked
with `audit-rg8-adoption.py`'s timing verifier. The following distributions include
all 64 candidates per segment, including missing-cost fallback. They are
**diagnostic observations, not accepted warmed async-performance distributions**.

| Order / mode | GPU span median / p95 ms | Full-view CPU median / p95 ms | Record-submit CPU median ms | Eligible samples |
|---|---:|---:|---:|---:|
| Forward 0 | 2.9082 / 3.6342 | 7.3035 / 11.2548 | 1.0730 | 32 / 32 |
| Forward 1 | 3.1206 / 3.2614 | 7.1030 / 10.8199 | 1.4424 | 0 / 32 |
| Forward 2 | 2.5175 / 3.9588 | 7.8681 / 13.5453 | 1.7648 | 4 / 32 |
| Reverse 2 | 3.2809 / 3.5953 | 7.5824 / 9.8074 | 1.5270 | 0 / 32 |
| Reverse 1 | 2.6578 / 3.5062 | 7.1947 / 10.5457 | 1.5904 | 2 / 32 |
| Reverse 0 | 2.2159 / 3.5799 | 6.2903 / 8.9849 | 0.9152 | 32 / 32 |

Of 128 mode-2 candidates, 124 fell back with `missing-measurement`; only four
submitted compute. All 128 candidate overlaps were zero, including those four.
The initial attempt's full log did contain a small nonzero overlap (maximum
0.0226 ms), outside this complete candidate matrix. That observation must not be
substituted for sustained overlap or net speedup.

In the initial log, missing-cost schedules reported all 68 pass measurements
missing, rather than one unsupported pass. This points to a history admission,
freshness or graph-signature problem; the exact rejection path has not yet been
instrumented and the root cause is **not established**. More repetitions of the
same benchmark will not resolve that defect.

Full-view CPU includes preparation, graph construction/compile, recording,
scheduling, submission and retirement enqueue; it excludes main-thread scene
generation, presentation and GPU-completion waits. Opt-in evidence serialization
is included. Record-submit is a narrower scope and must not be confused with it.

## Decision and next correction

Separate correctness captures completed in six normally exited processes (four
captures per mode/order). The 16 mode-0 versus mode-1/2 comparisons passed with
all 112 standard attachments at `maxAbs=0`, no nonfinite values, and matched
controlled scene inputs. These captures ran with GPU validation disabled and
are excluded from the normal timing distributions. Both normal-timing processes
also exited zero; it is the evidence acceptance wrapper that failed.

Keep RG8 default OFF. Native correctness passes, but product performance does
not meet the existing acceptance contract. Diagnose exactly why stored normal
costs are rejected at `FindFresh` / signature matching before changing thresholds
or increasing sample counts. Then rerun this same matrix and separately establish
a sustained-overlap workload and a fully measured negative fallback workload.

Evidence root: `Build/Verification/Phase43/RG8Pr168Perf20261009/`. It contains
source/binary identities, native results, both timing attempts, the independent
`summarize.py` and `diagnostic-performance-summary.json`, and separate image
captures/comparisons. Build logs: `Build/rg8-pr168-release.log` and
`Build/rg8-pr168-release-fixed.log`.

</details>
