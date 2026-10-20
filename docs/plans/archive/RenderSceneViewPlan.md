# Light·Camera 관리 재편 — RenderScene(보유) × RenderView(프레임) 이층 구조

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/RenderSceneViewPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [EditorWorkspaceRedesignPlan](../EditorWorkspaceRedesignPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.

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

<span id="1-판단--그-구도는-맞고-결합은-절충이-아니라-정석이다"></span>
<span id="2-지금-무엇이-일어나는가--실측-2026-08-09"></span>
<span id="21-라이트--보유층-밖에서-매-프레임-재수집"></span>
<span id="22-컬링--계산은-있는데-소비자가-0"></span>
<span id="23-카메라--고정-슬롯과-하드코딩"></span>
<span id="24-이미-맞게-되어-있는-것-건드리지-않는다"></span>
<span id="3-설계--두-층의-책임-경계"></span>
<span id="4-순서--네-슬라이스"></span>
<span id="5-왜-이-순서인가"></span>
<span id="6-하지-않을-것"></span>
<span id="sec-1-판단--그-구도는-맞고-결합은-절충이-아니라-정석이다"></span>
<span id="sec-2-지금-무엇이-일어나는가--실측-2026-08-09"></span>
<span id="sec-3-설계--두-층의-책임-경계"></span>
<span id="sec-4-순서--네-슬라이스"></span>
<span id="sec-5-왜-이-순서인가"></span>
<span id="sec-6-하지-않을-것"></span>
<span id="①-라이트를-보유층으로-단일-진실--구현-완료-2026-08-09--release유니티-그린"></span>
<span id="②-visiblelights--뷰별-라이트-목록--구현-완료-2026-08-09"></span>
<span id="②③-pix-실측-검증--2026-08-09"></span>
<span id="③-b-옥트리-계통-제거--2026-08-09"></span>
<span id="③-visibledraws--뷰별-프러스텀-컬링--구현-완료-2026-08-09"></span>
<span id="④-b-기즈모-뷰-분리--아이콘-알파--2026-08-09"></span>
<span id="④-카메라-등록-정리--구현-완료-2026-08-09"></span>

</details>
