# PHASE 4 계열 재배치 — C++ Pass·그래프·기능 완료선

> 2026-10-23 갱신: RG8 현행 구현(16인일)과 RG9 범위·split·Inspector(10인일) 제품 수용. RG9 D/R 빌드·각 512프레임 GPU 및 전체 회귀 완료. 기본 OFF 유지. 추가 개선은 [RG8-F 미래 계획](../plans/RenderGraphDependencySchedulingPlan.md#rg8-f). TR2의 7-route·SDK 품질·해상도·멀티뷰와 TR3의 history·SSGI 복구도 Debug/Release 검증으로 수용했다. 그 외 항목은 10월 22일 평가를 유지한다.

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/RenderPhaseRoadmap.md)

## 2026-10-22 현재 진척 재평가

현재 master ea87639e·작업트리 구현과 보존된 실행 기록을 대조해 대시보드 항목을 다시 작성했다. 작업 진척은 명시적 하위 산출물의 완료 비율을 기존 인일로 가중하고, 제품 수용 공수는 독립 원장으로 유지한다.

| PHASE | 작업 진척 | 제품 수용 | 수용/기존 산정 인일 | 미산정 잔여 |
|---|---:|---:|---:|---|
| 4 | 100% | 100% | 18/18 | 없음 |
| 4.25 | 88% | 80% | 32/40 | LX-1 1/3, LX-3 3/5, LX-3H 2/4 |
| 4.3 | 100% | 100% | 100/100 | 없음 |
| 4.5 | 68% | 31% | 22/71 | 없음 |
| 4.7 | 6% | 6% | 2/35 | 없음 |
| 4.75 | 34% | 0% | 0/28 | 없음 |
| 4.8 | 37% | 0% | 0/9 | 없음 |
| 4.85 | 0% | 0% | 0/208 | 없음 |
| 4.9 | 0% | 0% | 0/22 | 없음 |

전체 작업 진척 **41%**, 제품 수용 **33% (174/531인일)**. LX 잔여 3행은 미산정이며 RTP 208인일은 과거 중앙 추정이다. [항목별 평가·계산 규칙](../analysis/Phase4Progress20261022.md) · [원장](../analysis/Phase4Progress20261022.json). 4.x의 현재 수치는 이 표와 대시보드를 따른다. 아래 10월 20일 기준 설명은 구조/소유권 기록이다.

## 남은 작업과 완료 판정

- RG9은 2026-10-23 범위·transient mip·동일 반복 pass split·Inspector와 D/R 회귀를 수용했다. [구현·검증](../analysis/RenderRg9Implementation20261023.md).
- Temporal/TR/TU/FG, Lightmap, Renderer quality/Environment, GPU 설계/RT/PT, backend 비교의 완료선을 전문 계획별로 유지한다. 완료 기반을 잔여 공수에 다시 더하지 않는다.
- DX12 D/R 제품 수용과 Vulkan 비교(PHASE4.9), source 구현과 GPU/SDK/표시/성능 수용을 분리한다. 선행 관계는 공통 frame/graph 계약과 실제 소비 경로에서만 부여한다.

## 적용할 구조와 경계

`live pipeline → explicit graph versions → dependency recording → queue execution → completion retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 각 완료선의 단일 소유자

- [BlenderMaterialGraphPlan](BlenderMaterialGraphPlan.md)
- [LatticeAdoptionPlan](LatticeAdoptionPlan.md)
- [RenderGraphDependencySchedulingPlan](RenderGraphDependencySchedulingPlan.md)
- [TemporalReconstructionPlan](TemporalReconstructionPlan.md)
- [LightmapBakerPlan](LightmapBakerPlan.md)
- [RendererQualityPlan](RendererQualityPlan.md)
- [EnvironmentRenderingPlan](EnvironmentRenderingPlan.md)
- [GpuFeaturePlanningPlan](GpuFeaturePlanningPlan.md)
- [GpuDrivenGeometryImplementationPlan](GpuDrivenGeometryImplementationPlan.md)
- [PathTracingHybridPipelinePlan](PathTracingHybridPipelinePlan.md)
- [DxrShaderTraversalExperimentPlan](DxrShaderTraversalExperimentPlan.md)
- [BackendParityPlan](BackendParityPlan.md)

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:75](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L75) — `enum class RGSchedulingMode { DeclarationOrder, ExplicitSingleWriter, ExplicitVersioned };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:188](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L188) — `if (graph->m_scheduling == RGSchedulingMode::ExplicitVersioned && !graph->ValidVersionHandle(handle)) return {};`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L6) — `void EnhancedRenderGraph::RequireQueueIdle() const`
- [Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h#L8) — `class render_graph_declaration_lock final`
- [Engine/RenderEngine/RHI/IRHICommandTranslator.h:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRHICommandTranslator.h#L54) — `virtual bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHICommandTranslator.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHICommandTranslator.cpp#L6) — `bool RHICommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHISubmissionThread.cpp:1099](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHISubmissionThread.cpp#L1099) — `bool RHISubmissionThread::EnqueueSoftwareBatch(const void* owner, IRHIDeviceResources& completionSource,`
- [Engine/RenderEngine/RHI/IRHICommandTranslator.h:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRHICommandTranslator.h#L54) — `virtual bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHICommandTranslator.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHICommandTranslator.cpp#L6) — `bool RHICommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHISubmissionThread.cpp:1099](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHISubmissionThread.cpp#L1099) — `bool RHISubmissionThread::EnqueueSoftwareBatch(const void* owner, IRHIDeviceResources& completionSource,`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="1-페이즈와-회계"></span>
<span id="2-선행-그래프"></span>
<span id="2026-10-01-ui환경-후속-추적"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="3-정본-경계"></span>
<span id="4-완료선-분리와-검증"></span>
<span id="sec-1-페이즈와-회계"></span>
<span id="sec-2-선행-그래프"></span>
<span id="sec-2026-10-01-ui환경-후속-추적"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-3-정본-경계"></span>
<span id="sec-4-완료선-분리와-검증"></span>

</details>
