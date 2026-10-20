# 카메라별 표시 슬롯 도입 계획 (씬뷰/게임뷰 동시 표시)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/MultiCameraRenderPlan.md)

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

<span id="-다음에-같은-함정을-밟을-자리"></span>
<span id="0-2026-08-24-카메라-아키텍처-이행-결과"></span>
<span id="1-왜-하는가--증상과-원인"></span>
<span id="10-잔상-회귀와-수정-2026-08-07"></span>
<span id="11-볼류메트릭-포그-배선-2026-08-07"></span>
<span id="12-투명-렌더링-2026-08-07"></span>
<span id="13-투명-재질-텍스처-2026-08-07"></span>
<span id="14-forward를-pbr로-2026-08-07"></span>
<span id="15-투명에-캐스케이드-그림자-2026-08-07"></span>
<span id="2-결정된-방침"></span>
<span id="3-현재-구조"></span>
<span id="4-바꿀-지점-아홉-곳-한꺼번에"></span>
<span id="5-설계-상세"></span>
<span id="6-주의할-점"></span>
<span id="7-검증-방법"></span>
<span id="8-관련-완료-작업"></span>
<span id="9-구현-기록-2026-08-07"></span>
<span id="appcpp-호출부"></span>
<span id="cameraview"></span>
<span id="sec-0-2026-08-24-카메라-아키텍처-이행-결과"></span>
<span id="sec-1-왜-하는가--증상과-원인"></span>
<span id="sec-10-잔상-회귀와-수정-2026-08-07"></span>
<span id="sec-11-볼류메트릭-포그-배선-2026-08-07"></span>
<span id="sec-12-투명-렌더링-2026-08-07"></span>
<span id="sec-13-투명-재질-텍스처-2026-08-07"></span>
<span id="sec-14-forward를-pbr로-2026-08-07"></span>
<span id="sec-15-투명에-캐스케이드-그림자-2026-08-07"></span>
<span id="sec-2-결정된-방침"></span>
<span id="sec-3-현재-구조"></span>
<span id="sec-4-바꿀-지점-아홉-곳-한꺼번에"></span>
<span id="sec-5-설계-상세"></span>
<span id="sec-6-주의할-점"></span>
<span id="sec-7-검증-방법"></span>
<span id="sec-8-관련-완료-작업"></span>
<span id="sec-9-구현-기록-2026-08-07"></span>
<span id="ticklive-재구성"></span>
<span id="⑥⑦을-잡은-방법--셰이더-프로브"></span>
<span id="걸림돌이-아닌-것-확인-완료"></span>
<span id="검증"></span>
<span id="검증-1"></span>
<span id="검증-2"></span>
<span id="검증-중에-드러난-것-이번-범위-밖"></span>
<span id="계획과-달라진-점"></span>
<span id="기본-꺼짐"></span>
<span id="남은-것"></span>
<span id="남은-차이"></span>
<span id="남은-차이-알파-1-대조에서-확인"></span>
<span id="뒷면이-앞면을-덮던-문제"></span>
<span id="리뷰에서-잡힌-것"></span>
<span id="리뷰에서-잡힌-것-1"></span>
<span id="무엇을-맞췄나"></span>
<span id="받는-쪽만이다"></span>
<span id="배선"></span>
<span id="배선-1"></span>
<span id="빠져-있던-것-일곱"></span>
<span id="수정"></span>
<span id="원인"></span>
<span id="원인-규명-방법-재사용-가능"></span>
<span id="입력-텍스처에서-걸린-것-둘"></span>
<span id="자리"></span>
<span id="합성-자리"></span>
<span id="후속-과제-리뷰에서-확인-이번-범위-밖"></span>
<span id="흰색-폴백-대신-플래그를-쓴-이유"></span>

</details>
