# 머테리얼 · 셰이더 파이프라인 재설계 (PHASE 3.5)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/MaterialPipelinePlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [BlenderMaterialGraphPlan](../BlenderMaterialGraphPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/Experiment/MaterialResolver.cpp:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/MaterialResolver.cpp#L23) — `bool NormalizeMaterialKeywordSelections(const Material& material,`
- [Engine/RenderEngine/LXMaterialPipeline.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/LXMaterialPipeline.h#L18) — `std::string dependencies, sealedProgramIdentity;`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:90](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L90) — `using LiveGraphSnapshot = std::shared_ptr<const EnhancedRenderGraph::DiagnosticSnapshot>;`
- [Engine/RenderEngine/RenderScene.h:59](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RenderScene.h#L59) — `bool BeginProxyFrame(uint64_t sceneEpoch);`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0--이-문서가-하루도-못-가-낡았다-2026-08-11-정정"></span>
<span id="1-지금-무엇이-있는가--측정"></span>
<span id="10-지금-실제-위치-2026-08-24-재실측"></span>
<span id="101-slang-전환-결정-2026-08-24"></span>
<span id="102-실제-소비modelimport-교차-상태-2026-08-29-재감사"></span>
<span id="11-셰이더-컴파일-시스템이-둘이고-화면에-나오는-쪽은-애셋-파이프라인이-아니다"></span>
<span id="12-퍼뮤테이션은-개념이-없다--필요한-곳마다-손으로"></span>
<span id="13-렌더스테이트는-애셋-어디에도-없다"></span>
<span id="14-컴파일러가-목적지에-못-간다"></span>
<span id="15-그-밖의-실측--재설계가-밟게-될-자리"></span>
<span id="2-무엇을-하려는가"></span>
<span id="3-설계"></span>
<span id="31-계층"></span>
<span id="32-shadermeta--정규식-dsl의-후계"></span>
<span id="33-퍼뮤테이션-모델"></span>
<span id="34-컴파일러-서비스--v5의-실물"></span>
<span id="35-파이프라인-기술--v6의-실물"></span>
<span id="36-material-데이터-모델"></span>
<span id="4-이행--슬라이스"></span>
<span id="5-완료-기준"></span>
<span id="6-리스크"></span>
<span id="7-다른-계획과의-관계"></span>
<span id="m6-이후-phase-4-계열-인계"></span>
<span id="sec-0--이-문서가-하루도-못-가-낡았다-2026-08-11-정정"></span>
<span id="sec-1-지금-무엇이-있는가--측정"></span>
<span id="sec-2-무엇을-하려는가"></span>
<span id="sec-3-설계"></span>
<span id="sec-4-이행--슬라이스"></span>
<span id="sec-5-완료-기준"></span>
<span id="sec-6-리스크"></span>
<span id="sec-7-다른-계획과의-관계"></span>

</details>
