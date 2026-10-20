# RenderGraph 리소스 의존성 스케줄링 계획 (PHASE 4.3 · 트랙 RG)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/RenderGraphDependencySchedulingPlan.md)

## 남은 작업과 완료 판정

- RG8: 물리 자원 RAW/WAR/WAW·subresource/state·reader join·alias reuse/retirement를 공통 실행 계획으로 보존하며 합법적인 compute overlap 후보를 선택한다.
- 이미 구현된 explicit version/DAG·task dependency recording·batch/queue pool·barrier 계획·재사용 arena를 재작성하지 않는다. normal frame OFF/ON 전체 GPU 비용·overlap·fallback·RG7 조합을 현재 소스에서 수용해 기본 채택 여부를 판정한다.
- RG9: 운용 진단·실패 복구·장기 실행과 지원 행렬을 닫는다. 10월 9일의 RG8 done 기록은 후속 철회된 판정이며 활성 완료로 사용하지 않는다.

추가된 master 변경도 대조했다. 선언 usage/import 인덱스 재사용, frozen recording의 독립 target 병렬 번역과 sealed batch 조기 handoff는 구현 기반에 포함한다. 이 변경을 새 저작 체계나 RG8 전체 제품 수용 완료로 확대하지 않는다.

추가된 master 변경도 대조했다. 선언 usage/import 인덱스 재사용, frozen recording의 독립 target 병렬 번역과 sealed batch 조기 handoff는 구현 기반에 포함한다. 이 변경을 새 저작 체계나 RG8 전체 제품 수용 완료로 확대하지 않는다.

## 적용할 구조와 경계

`live pipeline → explicit graph versions → dependency recording → queue execution → completion retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `RG8` · PHASE 4.3 | multi-queue·async compute | progress |
| `RG9` · PHASE 4.3 | subresource·split barrier·Resource Inspector | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

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

<span id="0-결정-요약"></span>
<span id="1-현재-코드-기준선"></span>
<span id="2-목표-계약"></span>
<span id="2026-10-05-소스-구현-동결독립-최종-정적-검토-완료--실행-수용과-분리"></span>
<span id="2026-10-06-병합-뒤-첫-실행--성능-회귀-하나-수정-붉은-검사-남음"></span>
<span id="2026-10-07-rg5-종결-검사-후속"></span>
<span id="2026-10-07-rg6-제품-수용-완료"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="21-리소스와-버전"></span>
<span id="22-의존-edge"></span>
<span id="23-compile-순서"></span>
<span id="24-저작-순서와-실행-순서"></span>
<span id="3-구현-순서와-공수"></span>
<span id="4-슬라이스별-구현-경계"></span>
<span id="5-회귀와-실패-판정"></span>
<span id="6-비목표와-금지선"></span>
<span id="7-다른-페이즈트랙과의-의존성"></span>
<span id="q0--큐timeline-rhi-공통-기반-2026-10-08-완료"></span>
<span id="rg-v--compiled-graph-읽기-전용-viewer-2026-10-01-추가"></span>
<span id="rg0--기준선을-먼저-잠근다"></span>
<span id="rg1--최소-접근-선언으로-단일-writer-dag부터-정렬한다"></span>
<span id="rg2--버전으로-다중-writermodify-dag를-연다"></span>
<span id="rg3rg4--기존-기능을-새-순서에-다시-연결한다"></span>
<span id="rg5-남은-이관-순서--2026-10-04-기준선-이력"></span>
<span id="rg5rg6--제품을-한-번만-전환한다"></span>
<span id="rg7--aliasing은-정확성-완료-뒤-연다"></span>
<span id="rg7-종료-기준-고정--2026-10-08"></span>
<span id="rg8--async-compute는-계측으로-채택한다"></span>
<span id="rg9--rdg-운용-성숙도를-닫는다"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-현재-코드-기준선"></span>
<span id="sec-2-목표-계약"></span>
<span id="sec-2026-10-05-소스-구현-동결독립-최종-정적-검토-완료--실행-수용과-분리"></span>
<span id="sec-2026-10-06-병합-뒤-첫-실행--성능-회귀-하나-수정-붉은-검사-남음"></span>
<span id="sec-2026-10-07-rg5-종결-검사-후속"></span>
<span id="sec-2026-10-07-rg6-제품-수용-완료"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-3-구현-순서와-공수"></span>
<span id="sec-4-슬라이스별-구현-경계"></span>
<span id="sec-5-회귀와-실패-판정"></span>
<span id="sec-6-비목표와-금지선"></span>
<span id="sec-7-다른-페이즈트랙과의-의존성"></span>
<span id="sec-rg-v--compiled-graph-읽기-전용-viewer-2026-10-01-추가"></span>
<span id="sec-rg5-남은-이관-순서--2026-10-04-기준선-이력"></span>

</details>
