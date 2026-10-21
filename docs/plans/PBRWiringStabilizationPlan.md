# PBR 배선 안정화 계획 (PHASE 4)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/PBRWiringStabilizationPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [BlenderMaterialGraphPlan](BlenderMaterialGraphPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- PBR-W0~W9는 완료된 배선이므로 활성 구현 항목은 없다. 후속 재질 품질/성능은 BlenderMaterialGraphPlan의 MAT-9, 환경/표시 품질은 RendererQualityPlan이 소유한다.

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

<span id="1-2026-09-03-소스-감사-결과"></span>
<span id="10-2026-09-06-w6--발광-색강도색공간"></span>
<span id="11-w7-첫-단위--비균등-스케일-normaltangent-2026-09-06"></span>
<span id="12-w7-두-번째-단위--uv0uv1-선택텍스처별-변환-2026-09-06"></span>
<span id="13-w7-세-번째-단위--mip-생성소비-2026-09-06"></span>
<span id="14-w8--materialdescriptorpso-generation-원자-밀봉-2026-09-14"></span>
<span id="15-w9--실장면장시간재임포트-회귀와-cutover-2026-09-14"></span>
<span id="16-w3--neutral-의-단일-출처화-2026-09-14"></span>
<span id="17-w1--normal-map-저작-유무의-실장면-판정-2026-09-14"></span>
<span id="18-w0--draw별-신원-세-축을-캡처에-싣는다-2026-09-14"></span>
<span id="19-w7-네-번째-단위--sampler-가-재질을-따른다-2026-09-14"></span>
<span id="2-목표-제품-경로"></span>
<span id="20-판정-범위를-dx12-로-좁힌다--2026-09-15-결정"></span>
<span id="21-w8-핵심-수정을-증명한다--공유-material-fixture-2026-09-16"></span>
<span id="211-왜-씬-저작으로는-못-만드는가"></span>
<span id="212-그래서-fixture-는-자산이고-저작-표면을-하나-세워야-했다"></span>
<span id="213-단정을-어디에-걸-것인가--w7-과-정확히-반대다"></span>
<span id="214-실측-2026-09-16--debug--dx12"></span>
<span id="215-이-회차가-드러낸-것"></span>
<span id="216-남은-것"></span>
<span id="22-w8-마지막-축--재임포트-실패-뒤-current-불변-2026-09-16"></span>
<span id="221-cli-는-제대로-실패한다--그런데-modelload-로는-계약이-자극되지-않는다"></span>
<span id="222-계약을-재는-검사는-이미-있었다--호출자가-0-이었다"></span>
<span id="223-fixture-를-커밋하지-않는-이유--cooked-산출물은-썩는다"></span>
<span id="224-축을-셋으로-나눠-센다--변이가-그-이유를-보였다"></span>
<span id="225-하마터면-제품-결함으로-보고할-뻔했다--증분-링크가-낡은-오브젝트를-실었다"></span>
<span id="226-자극하지-못한-것--게이트-출력에-매-회차-적는다"></span>
<span id="23-w9--배선-게이트-첫-완주와-10-분-release-soak-2026-09-16"></span>
<span id="231-배선-게이트가-처음-끝까지-갔다"></span>
<span id="232-soak-게이트의-결함-셋"></span>
<span id="233-재임포트를-자극하는-모양--재임포트만으로는-화면이-안-바뀐다"></span>
<span id="234-w9-acceptance--release--dx12--벽시계-10-분"></span>
<span id="235-w9-에-남은-것"></span>
<span id="24-animator-의-검증-전-초기화--코드는-있고-도달-경로는-없다-2026-09-16"></span>
<span id="241-호출자는-둘이고-둘-다-새-animator-다"></span>
<span id="242-실측--그-틈은-cli-와-에디터-조작으로-열리지-않는다"></span>
<span id="243-판정"></span>
<span id="25-renderdoc-표면-무늬-후속-수정--2026-09-19"></span>
<span id="251-변경"></span>
<span id="252-검증"></span>
<span id="26-현재-완료-조건과-검증-소유권-2026-09-19"></span>
<span id="27-제품-픽셀-판정과-최신-통합-검증-2026-09-19"></span>
<span id="28-w8-런타임-모델-reload-실패-교체와-w9-종료-2026-09-20"></span>
<span id="29-editor-모델-생성-데이터-자동-복구-2026-09-20"></span>
<span id="3-실행-순서와-공수"></span>
<span id="4-슬라이스별-완료-조건"></span>
<span id="5-비범위와-후속-인계"></span>
<span id="6-w0w3-첫-구현--2026-09-06"></span>
<span id="7-w2-native-slang-공용-평가--2026-09-06"></span>
<span id="8-2026-09-06-w4--alpha-mode와-양면-coverage"></span>
<span id="9-2026-09-06-w5--ao와-reflection-texture-table"></span>
<span id="descriptor-generation-은-제품에-없었다"></span>
<span id="fixture--toolsregressionfixturespbr-sampler"></span>
<span id="fixture--저장소가-소유해야-했다"></span>
<span id="fixture-둘을-더-채웠다--외부-자산-없이"></span>
<span id="gbuffer-packing-결정"></span>
<span id="pbr-w0--관측-기준선"></span>
<span id="pbr-w2w3--언어backend-단일화"></span>
<span id="pbr-w4w7--재질-의미"></span>
<span id="pbr-w8w9--플리커와-제품-cutover"></span>
<span id="rhi-어휘-구멍-둘--하나만-메운다"></span>
<span id="sec-1-2026-09-03-소스-감사-결과"></span>
<span id="sec-10-2026-09-06-w6--발광-색강도색공간"></span>
<span id="sec-11-w7-첫-단위--비균등-스케일-normaltangent-2026-09-06"></span>
<span id="sec-12-w7-두-번째-단위--uv0uv1-선택텍스처별-변환-2026-09-06"></span>
<span id="sec-13-w7-세-번째-단위--mip-생성소비-2026-09-06"></span>
<span id="sec-14-w8--materialdescriptorpso-generation-원자-밀봉-2026-09-14"></span>
<span id="sec-15-w9--실장면장시간재임포트-회귀와-cutover-2026-09-14"></span>
<span id="sec-16-w3--neutral-의-단일-출처화-2026-09-14"></span>
<span id="sec-17-w1--normal-map-저작-유무의-실장면-판정-2026-09-14"></span>
<span id="sec-18-w0--draw별-신원-세-축을-캡처에-싣는다-2026-09-14"></span>
<span id="sec-19-w7-네-번째-단위--sampler-가-재질을-따른다-2026-09-14"></span>
<span id="sec-2-목표-제품-경로"></span>
<span id="sec-20-판정-범위를-dx12-로-좁힌다--2026-09-15-결정"></span>
<span id="sec-21-w8-핵심-수정을-증명한다--공유-material-fixture-2026-09-16"></span>
<span id="sec-22-w8-마지막-축--재임포트-실패-뒤-current-불변-2026-09-16"></span>
<span id="sec-23-w9--배선-게이트-첫-완주와-10-분-release-soak-2026-09-16"></span>
<span id="sec-24-animator-의-검증-전-초기화--코드는-있고-도달-경로는-없다-2026-09-16"></span>
<span id="sec-25-renderdoc-표면-무늬-후속-수정--2026-09-19"></span>
<span id="sec-26-현재-완료-조건과-검증-소유권-2026-09-19"></span>
<span id="sec-27-제품-픽셀-판정과-최신-통합-검증-2026-09-19"></span>
<span id="sec-28-w8-런타임-모델-reload-실패-교체와-w9-종료-2026-09-20"></span>
<span id="sec-29-editor-모델-생성-데이터-자동-복구-2026-09-20"></span>
<span id="sec-3-실행-순서와-공수"></span>
<span id="sec-4-슬라이스별-완료-조건"></span>
<span id="sec-5-비범위와-후속-인계"></span>
<span id="sec-6-w0w3-첫-구현--2026-09-06"></span>
<span id="sec-7-w2-native-slang-공용-평가--2026-09-06"></span>
<span id="sec-8-2026-09-06-w4--alpha-mode와-양면-coverage"></span>
<span id="sec-9-2026-09-06-w5--ao와-reflection-texture-table"></span>
<span id="vk-도-끈다--그-이름은-범위를-속인다"></span>
<span id="w0-의-fixture-축-현황"></span>
<span id="검증"></span>
<span id="검증-1"></span>
<span id="검증-2"></span>
<span id="검증-3"></span>
<span id="검증-4"></span>
<span id="검증-5"></span>
<span id="검증-6"></span>
<span id="검증-7"></span>
<span id="검증과-남은-범위"></span>
<span id="게이트"></span>
<span id="게이트가-끝까지-못-가는-이유-둘-w7-밖"></span>
<span id="교차-백엔드-픽셀--게이트의-질문을-좁혔다"></span>
<span id="구현"></span>
<span id="구현-1"></span>
<span id="구현-2"></span>
<span id="구현-3"></span>
<span id="구현-4"></span>
<span id="구현-5"></span>
<span id="구현-6"></span>
<span id="구현-7"></span>
<span id="구현-8"></span>
<span id="구현한-검증-경로"></span>
<span id="남은-것"></span>
<span id="남은-것-정직하게"></span>
<span id="남은-것-정직하게-1"></span>
<span id="남은-완료-조건"></span>
<span id="다섯-축이-실제로-어떻게-닫히는가"></span>
<span id="다음-단위의-확인된-경계"></span>
<span id="단정하는-것과-재기만-하는-것을-갈랐다"></span>
<span id="무엇을-미루고-무엇을-안-미루는가"></span>
<span id="발견-둘"></span>
<span id="배선을-다-잇고도-축이-죽어-있었다--그리고-장부가-옳았다"></span>
<span id="변이-증명"></span>
<span id="변이-증명--두-팔을-따로-쳤다"></span>
<span id="변이-증명--둘은-섰고-하나는-설-자리가-없었다"></span>
<span id="변이-증명--예고와-어긋난-숫자가-새-사실을-줬다"></span>
<span id="빈-집합을-통과시키지-않는다"></span>
<span id="설계가-기계적인-이유--coordinates가-완주-레일이다"></span>
<span id="수정과-회귀"></span>
<span id="실제로-깨진-계약"></span>
<span id="실측-2026-09-14"></span>
<span id="실측이-드러낸-것-계획에-없던-것"></span>
<span id="실측이-드러낸-것-계획에-없던-것-1"></span>
<span id="실행-결과-2026-09-14-debug-x64"></span>
<span id="실행과-판정-범위"></span>
<span id="아직-하지-않은-것-정직하게"></span>
<span id="여덟-번째-자리--일부러-합치지-않았다"></span>
<span id="왜-지금인가"></span>
<span id="요구를-바꿨다--추적한다-가-아니라-조용히-통과하지-않는다"></span>
<span id="유도를-접었다"></span>
<span id="이-fixture-가-예상-밖으로-연-것--forward-라우트"></span>
<span id="이-결정이-바꾸지-않는-것"></span>
<span id="자극하지-못하는-축--주장하지-않는다"></span>
<span id="조용히-사라지지-않게-하는-장치"></span>
<span id="종료-판정"></span>
<span id="종료-판정-1"></span>
<span id="증명"></span>
<span id="착수-전-실측--6단"></span>
<span id="착수-전-실측이-뒤집은-것"></span>
<span id="착수-전-실측이-뒤집은-것-1"></span>
<span id="착수-전-실측이-뒤집은-것-2"></span>
<span id="착수-전-실측이-뒤집은-것-3"></span>
<span id="착수-전-실측이-뒤집은-것-4"></span>
<span id="착지"></span>
<span id="최신-실행-결과"></span>
<span id="최종-변경의-실행-증거-2026-09-20"></span>
<span id="축-회계"></span>
<span id="캐시-이행"></span>
<span id="쿠킹-버전-올림의-후속--재임포트와-쐐기-하나"></span>
<span id="판정의-범위"></span>

</details>
