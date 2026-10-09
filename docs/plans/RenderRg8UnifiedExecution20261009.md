# RG8 unified execution implementation

Base: `cd3fa2e2f8cf40d4c810731430f8be4503a09f1e` (master, 2026-10-09).

## Scope

- Separate normal-frame measurements from captures and report explicit scheduler fallbacks.
- Plan physical-resource access hazards, legal cross-queue states, reader joins and lifetimes without pass-name or pass-order rules.
- Reuse RG4 parallel recording and the existing RHI submission/retirement ownership for queue execution.
- Consider supported HiZ, visibility, SSGI and post-processing compute declarations only when their real dependency and state contracts allow overlap.
- Add correctness, overlap, negative-fallback and full-cost OFF/ON regression coverage. Repeated capture copies of unchanged image versions must not be mistaken for workload cost.

## Safety and acceptance

RAW, WAR, WAW, explicit dependencies, CPU-side effects, incompatible states/subresources and alias reuse remain ordered. Shared reads require legal state preparation and all-reader joins before a writer, transition or reuse, including frame retirement. Existing safety guards remain until their replacement has equivalent proof.

No builds, tests, compilers, shaders, GPU probes, repository scripts or benchmarks are run during this implementation. Added tests and measurement tooling are authored but unexecuted. Performance improvement and adoption are not claimed without separate normal-frame GPU evidence. Capture and output-validation evidence are excluded from performance acceptance.

This first commit records implementation intent for a draft PR. Subsequent commits contain the code and static review findings; this document is not evidence that the changes are complete or validated.
