# PHASE 4 계열 통합 계획 — PBR 안정화에서 차세대 렌더링까지

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/Phase4UnifiedPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [RenderPhaseRoadmap](RenderPhaseRoadmap.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 옛 통합 작업·공수·SRP 방안을 현재 계획으로 재사용하지 않는다. 현재 잔여와 단일 소유권은 RenderPhaseRoadmap 및 각 전문 계획으로 이관한다.

## 적용할 구조와 경계

`live pipeline → explicit graph versions → dependency recording → queue execution → completion retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

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

<span id="0-정본-경계"></span>
<span id="1-범위-분리-결정"></span>
<span id="10-공통-완료-기준"></span>
<span id="11-phase-4--현재-제품-배선을-먼저-정상화"></span>
<span id="11-금지하는-재혼합"></span>
<span id="12-phase-425--blender형-재질-저작과-결과-계약"></span>
<span id="12-변경-이력"></span>
<span id="13-phase-43--공통-밀봉-하네스와-rendergraph-의존성-스케줄링"></span>
<span id="14-phase-45--시간축-재구성-계층"></span>
<span id="15-phase-475--남은-renderersrpgpu-기능"></span>
<span id="16-phase-49--renderdoc-백엔드-비교"></span>
<span id="2-phase-375에서-받는-계약"></span>
<span id="3-전체-실행-순서"></span>
<span id="4-phase-4--pbr-배선-안정화-18일"></span>
<span id="5-phase-425--blender형-pbrmaterial-graph-34일"></span>
<span id="6-phase-43--공통-밀봉-하네스와-rendergraph-의존성-스케줄링-119일"></span>
<span id="61-공통-기준선--6일"></span>
<span id="62-트랙-rgqueue--113일--q0-미산정"></span>
<span id="7-phase-45--시간축-재구성-계층-71일"></span>
<span id="71-트랙-tr--시간축-기반-21일"></span>
<span id="72-트랙-tu--업스케일-20일"></span>
<span id="73-트랙-fg--프레임-생성-26일"></span>
<span id="74-통합-게이트--4일"></span>
<span id="8-phase-475--남은-렌더-파이프라인-955일"></span>
<span id="80-축-분류-2026-09-23"></span>
<span id="81-공통gpu-설계-게이트--75일"></span>
<span id="82-라이트맵--35일"></span>
<span id="83-일반-srp--33일"></span>
<span id="84-renderer-품질후처리--20일"></span>
<span id="9-공수와-진행률-규칙"></span>
<span id="phase-4"></span>
<span id="phase-425"></span>
<span id="phase-43"></span>
<span id="phase-45"></span>
<span id="phase-475"></span>
<span id="sec-0-정본-경계"></span>
<span id="sec-1-범위-분리-결정"></span>
<span id="sec-10-공통-완료-기준"></span>
<span id="sec-11-금지하는-재혼합"></span>
<span id="sec-12-변경-이력"></span>
<span id="sec-2-phase-375에서-받는-계약"></span>
<span id="sec-3-전체-실행-순서"></span>
<span id="sec-4-phase-4--pbr-배선-안정화-18일"></span>
<span id="sec-5-phase-425--blender형-pbrmaterial-graph-34일"></span>
<span id="sec-6-phase-43--공통-밀봉-하네스와-rendergraph-의존성-스케줄링-119일"></span>
<span id="sec-7-phase-45--시간축-재구성-계층-71일"></span>
<span id="sec-8-phase-475--남은-렌더-파이프라인-955일"></span>
<span id="sec-9-공수와-진행률-규칙"></span>

</details>
