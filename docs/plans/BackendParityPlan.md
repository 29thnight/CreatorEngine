# RenderDoc 백엔드 비교 계획 (PHASE 4.9)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/BackendParityPlan.md)

## 남은 작업과 완료 판정

- BP-0~BP-5: 동일 입력·장치/바이너리 신원을 고정하고 DX12/Vulkan RenderDoc 캡처·재생, Pass별 자원/상태/바인딩 대조, 선형 픽셀 추출·오차 비교를 수행한다.
- 불일치 최초 event를 특정해 수정·재캡처하고 D/R·장치별 반복과 손상/누락 변이를 보존한다. 다른 페이즈의 DX12 완료 조건으로 역연결하지 않는다.

## 적용할 구조와 경계

`live pipeline → explicit graph versions → dependency recording → queue execution → completion retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `BP-0` · PHASE 4.9 | 비교 입력·시간축·지원 행렬 고정 | todo |
| `BP-1` · PHASE 4.9 | RenderDoc 양 backend 실제 프레임 캡처 | todo |
| `BP-2` · PHASE 4.9 | Pass별 입력·출력 리소스와 바인딩 대조 | todo |
| `BP-3` · PHASE 4.9 | 선형 리소스 추출·픽셀별 수치 비교 | todo |
| `BP-4` · PHASE 4.9 | 불일치 원인 수정과 RenderDoc 재캡처 | todo |
| `BP-5` · PHASE 4.9 | 반복·실패 변이·회귀 패키지와 수용 보고서 | todo |

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

<span id="1-수행-순서"></span>
<span id="2-작업과-공수"></span>
<span id="2026-10-07-graph-only-재질-전환-인계"></span>
<span id="2026-10-11-lx-그림자-메시렛-검증-이관"></span>
<span id="3-이관-목록"></span>
<span id="4-수용-기준과-보존할-증거"></span>
<span id="5-인계받은-관측-기록"></span>
<span id="lx-scene-단일-배선-후속--2026-10-12"></span>
<span id="sec-1-수행-순서"></span>
<span id="sec-2-작업과-공수"></span>
<span id="sec-3-이관-목록"></span>
<span id="sec-4-수용-기준과-보존할-증거"></span>
<span id="sec-5-인계받은-관측-기록"></span>
<span id="sec-lx-scene-단일-배선-후속--2026-10-12"></span>

</details>
