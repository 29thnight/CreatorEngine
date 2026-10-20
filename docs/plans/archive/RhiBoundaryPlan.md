# RHI 경계 재설계 (PHASE 3-1 재정의)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/RhiBoundaryPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [RenderGraphDependencySchedulingPlan](../RenderGraphDependencySchedulingPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

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

<span id="0-현재-상태-2026-08-15"></span>
<span id="01-지표--진척은-이-셋으로만-보고한다"></span>
<span id="010-완료--imgui-renderer-backend-rhi-경계--2026-08-14"></span>
<span id="011-완료--imgui-texture-completion-수명--2026-08-14"></span>
<span id="012-완료--공통-completion-retire-queue--2026-08-15"></span>
<span id="013-완료--slice-d-persistent-buffer-heap--2026-08-15"></span>
<span id="014-완료--texture-compatibility-pool--device-local-budget--2026-08-15"></span>
<span id="015-완료--device-scoped-memory-budget-coordinator--2026-08-15"></span>
<span id="016-완료--memory-pressure-기반-asset-eviction--2026-08-15"></span>
<span id="017-완료--slice-e-a-descriptor-versioned-recycler--2026-08-15"></span>
<span id="018-완료--g-2-rendergraph-배리어-완전-중립화--2026-08-15"></span>
<span id="019-완료--g-3-rendergraph-병렬-기록-중립화--2026-08-15"></span>
<span id="02-완료--5번-a-완료-그리드가-두-백엔드에서-돈다--2026-08-12"></span>
<span id="020-a-보존-기록--5번의-완료-경위"></span>
<span id="020-완료--slice-7--8-a-8-b--2026-08-15"></span>
<span id="021-a-압축된-잔여-순서"></span>
<span id="021-b-완료--r6-a-dx12-테스트-raw-texture-import-제거--2026-08-15"></span>
<span id="021-c-완료--r6-b-리드백-계약-완전-중립화--2026-08-15"></span>
<span id="021-d-완료--r6-c-rendergraph-dx12-잔여-삭제--2026-08-15"></span>
<span id="021-e-완료--최종-통합-회귀--2026-08-15"></span>
<span id="021-완료--slice-8-c-불변-backend-선택표시-회귀--2026-08-15"></span>
<span id="022-프로덕션-dx12-접촉면-잔량-2026-08-15-실측"></span>
<span id="03-완료--첫-텍스처-소비-공용-패스-skybox--2026-08-12"></span>
<span id="04-완료--실제-png-소비-공용-패스-gizmoicon--2026-08-12"></span>
<span id="05-완료--기본-지오메트리-4패스-shadow--gbuffer--forward--deferred--2026-08-14"></span>
<span id="06-완료--decal-공용-패스--vulkan-editor-live-연결--2026-08-14"></span>
<span id="07-완료--ssao-공용-패스--실제-scene-gbuffer-소비--2026-08-14"></span>
<span id="08-완료--ssgi-공용-패스--뷰별-temporal-live-연결--2026-08-14"></span>
<span id="09-완료--공용-render-물리-계층--vs-필터-정리--2026-08-14"></span>
<span id="1-측정과-동기-2026-08-07"></span>
<span id="11-기존-rhi-의-사인--이-계획서-전체의-반면교사"></span>
<span id="12-문제의-방향--dx12-가-새는-게-아니라-상위가-갇혀-있다"></span>
<span id="13-결합-실측이-슬라이스를-정했다"></span>
<span id="14-동기-셋"></span>
<span id="2-설계--지금-서-있는-형태"></span>
<span id="21-계층"></span>
<span id="22-경계-인터페이스-7종과-구현-현황"></span>
<span id="23-그래프"></span>
<span id="3-완료-대장"></span>
<span id="4-규칙--이-저장소가-돈-주고-배운-것"></span>
<span id="41-순서와-값-매기기"></span>
<span id="42-세는-자"></span>
<span id="43-검증-하네스"></span>
<span id="44-성능-측정"></span>
<span id="45-조용히-틀리는-부류"></span>
<span id="46-미루기와-회수"></span>
<span id="47-vulkan-대응"></span>
<span id="5-완료-기준"></span>
<span id="6-이-계획이-넘겨받지-않는-것"></span>
<span id="ag축--경계-마감-08-11"></span>
<span id="dt축--디바이스-소유권과-자산-잔량-08-07--08-08"></span>
<span id="r축--경계-세우기-08-07--08-09"></span>
<span id="sec-0-현재-상태-2026-08-15"></span>
<span id="sec-1-측정과-동기-2026-08-07"></span>
<span id="sec-2-설계--지금-서-있는-형태"></span>
<span id="sec-3-완료-대장"></span>
<span id="sec-4-규칙--이-저장소가-돈-주고-배운-것"></span>
<span id="sec-5-완료-기준"></span>
<span id="sec-6-이-계획이-넘겨받지-않는-것"></span>
<span id="v축--vulkan-전제-재편-08-10--08-11"></span>

</details>
