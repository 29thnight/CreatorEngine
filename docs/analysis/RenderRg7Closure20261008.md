# RG7 DX12 closure and default-OFF decision — 2026-10-08

2026-10-09 structural audit: [RG7/RG8 review](RenderRg7Rg8StructuralAudit20261009.md) confirms real aliasing (15 resources in six heaps, 3.625MiB lower allocation per retained product graph), but identifies whole-group cache matching, retention policy and missing end-to-end memory-budget optimization. The bounded implementation/experiment closure below remains; it must not be interpreted as completed product memory optimization. RG8 product completion was separately retracted.

RG7 is **done for its bounded DX12 implementation, correctness, measurement and adoption decision**. Aliasing remains **OFF by default**, with the existing opt-in and committed fallback retained. This is not performance adoption or exact physical resident-memory acceptance. Q0/RG8/RG9 and PHASE 4.3 as a whole are not closed by this decision.

## Final evidence

VS 2026/v18 Debug and Release builds passed. The existing Editor full RenderGraph regressions and RG7 eleven GPU modes, six failure cases, two delayed-lifetime cases and native WARP removal passed with validation zero and exit zero. Native evidence is `Build/Verification/Phase43/RG7ClosureNative/{Debug,Release}`. Product compiler evidence reuses the exact same Release full-regression result and executable/runtime hashes, rather than executing that regression again per process.

Four independent Release Editor processes ran in ON→OFF and OFF→ON order. Each collected eight equivalent controlled Scene captures at 1498×730, then checked Scene/Game/Preview, scene replacement, resize and clean shutdown. All four report GPU validation problems/drops zero and exit zero. Sixteen OFF/ON capture pairs compare 112 attachments with maximum error zero. Two independent-process cross-order pairs add fourteen attachments with maximum error zero. Initial readiness comparisons also pass. Shared pool identity/retained/cache/lease accounting remains consistent.

The opt-in `CREATOR_GPU_MEMORY_SAMPLES` diagnostic starts after DX12 device creation, before device resource preparation. An owned adapter is queried on a separate thread every nominal 100ms, through device teardown. Rendering/shader preparation cannot stall this sampling thread. It records monotonic elapsed time, UTC milliseconds, query HRESULT/availability, raw current usage and budget bytes, and start/end records. Per-command stage timestamps bracket startup scene loading, captures, scene replacement, resize and quit. There are 17,684 records across four processes/eight device streams; all queries succeed and the greatest observed gap is 130ms. Each stream has a start/end record. Streams are retained separately and never summed: queries observe process usage on their adapter, not memory owned exclusively by the querying device.

| Run order | Mode | Prepare CPU median (ms) | Record CPU median / p95 (ms) | Maximum observed DXGI usage (bytes) | Memory records |
|---|---|---:|---:|---:|---:|
| ON→OFF | ON | 0.0237 | 2.7045 / 4.1389 | 1,582,292,992 | 4,554 |
| ON→OFF | OFF | 0.0055 | 2.6572 / 4.4216 | 1,579,487,232 | 4,553 |
| OFF→ON | OFF | 0.00595 | 2.72985 / 3.4688 | 1,579,487,232 | 4,345 |
| OFF→ON | ON | 0.0241 | 2.76505 / 3.6 | 1,582,174,208 | 4,232 |

ON preparation median increases in both orders; record median also increases slightly. The sampled lifecycle usage maximum is higher for ON in both orders, despite earlier warm capture-boundary snapshots being lower. Warm snapshots are not lifecycle peaks. There is no demonstrated overall performance or peak-usage benefit warranting default ON. Scopes overlap and must not be summed into a synthetic CPU frame cost.

## Closure checklist disposition

- C1–C3: supported allocation/lifetime/compatibility/fallback, poison/failure ownership, bounded multi-frame/view correctness and pixels are accepted by retained and fresh native/product evidence.
- C4: shared native allocation ownership and uninterrupted lifecycle budget-usage measurement are complete in the available diagnostic scope. Owned heap/cache/lease bytes and device-budget usage remain distinct. The memory decision uses these measured values; it does not invent per-resource committed ownership or physical residency values. A broader whole-engine committed-allocation inventory is diagnostics follow-up, not a prerequisite for keeping this optimization disabled by default.
- C5: same-binary equivalent workload, both process orders and independent repetitions are complete. **Adoption verdict: keep default OFF.** A negative optimization outcome closes the experiment; it does not keep implementation perpetually open.
- C6: controls, rollback, limitations, acceptance record and canonical status/accounting are recorded. RG7 recovers its existing 14-day planned effort once; estimated total stays unchanged. This is planned effort accounting, not elapsed engineering time.

## Controls and limits

`CREATOR_RENDERGRAPH_ALIASING=1` opts into the supported DX12 sharing path. Unset or `0` uses the existing committed/pool path; restart the Editor for a clean rollback/cache lifetime. `CREATOR_RENDERGRAPH_EXTEND_LIFETIMES=1` suppresses sharing for diagnostics. `CREATOR_GPU_MEMORY_SAMPLES=<absolute JSONL path>` enables the measurement thread only when requested; ordinary runs start no sampler. GPU validation, diagnostic readbacks and the sampler were enabled equally in these A/B runs, so the reported CPU values are not ordinary production FPS.

Sampling can miss sub-interval peaks. DXGI budget current usage is neither exact physical resident VRAM nor a per-resource committed-allocation ledger; no cross-adapter/device-stream summation or exact resident savings are claimed. Shutdown usage is not required to be zero while other device/interop owners or driver accounting remain; native ownership fixtures separately establish final heap drain zero. Debug product continuous samples were not collected; Debug build/native regression passed. Release Utility_Framework PDB LNK4020 warnings remain a debugger-symbol limitation, not repaired here.

RG7-H retains real OOM/spontaneous hardware loss/rebuild/genuine Signal-failure hardening. RG7-S retains expanded poison, longer content stress, broader workloads and deeper committed/residency attribution. Vulkan product acceptance remains PHASE 4.9. RG8 owns cross-queue shared-heap handoff dependencies; current sharing relies on serialized execution. These are explicit follow-ups, not reopened RG7 closure conditions.

## Reproduction and identity

Evidence: `Build/Verification/Phase43/RG7Closure-20261008/{closure-result.json,OnFirst,OffFirst,cross-order-On.json,cross-order-Off.json,CompilerFixtureEvidence}` and `Build/Verification/Phase43/rg7-closure-product-acceptance.log`. Each case retains `memory-continuous.jsonl`, `memory-stages.jsonl`, manifests, frozen graphs, results and hashes. `Tools/regression/audit-rg7-closure.ps1` audits completed series, valid queries, maximum gaps, stage coverage, identical binaries and cross-order parity. The two product wrappers audit same-input samples and ON cache reuse.

Release runtime SHA-256: `CD7D5D1CBA17F43B7D64D9A311085865BFB077F3983B32219CF3E60BDFE2A0A0`. Launcher SHA-256: `BB9B72CD991CBC647599848E096BCB0AAD8E423E42DEF3181226D0BE596DCC54`. Evidence was collected from the uncommitted worktree based on HEAD `4602aff1c469b8d4af8a18a178b70f3e9ed3c37a`; HEAD alone is not the tested source identity. Binary hashes and source receipts are retained. Commit/push were not performed in this closure task.
