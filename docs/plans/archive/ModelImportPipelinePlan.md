# 모델 임포트 파이프라인 · 정점 레이아웃 (역사 기록)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/ModelImportPipelinePlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [AssetDepotImplementationPlan](../AssetDepotImplementationPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.

## 적용할 구조와 경계

`DataSystem → resolver snapshot → typed request → descriptor/payload owner → frame pin → RHI retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/DataSystem.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L30) — `own::shared_owner<const Texture> descriptor;`
- [Engine/RenderEngine/AssetDepot/AssetRequest.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetRequest.h#L108) — `own::shared_owner<const T> asset{};`
- [Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h:69](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h#L69) — `own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};`
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h#L18) — `std::uint64_t resolverRevision{};`
- [Engine/Utility_Framework/Ownership.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Ownership.h#L5) — `#include "../../ThirdParty/ownership_cpp/include/own/ownership.hpp"`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="-i7-의-목표-수치--실측으로-확정-2026-08-25"></span>
<span id="-i7-이행-결과-2026-08-25"></span>
<span id="-공수-재산정-2026-09-01--9ef369d7-기준--5일이-아니라-24일이다"></span>
<span id="-반성--포맷을-잘-만들어도-읽는-수단이-병목이면-소용이-없다"></span>
<span id="-작업-대상은-experiment-계층이다"></span>
<span id="-첫-피드백--b3e088a008이-산정의-축-하나를-정정했다"></span>
<span id="0-당시-phase-4-편입-기록--왜-별도-페이즈가-아니었는가"></span>
<span id="1-지금-무엇이-있는가--측정-2026-08-27-재감사"></span>
<span id="11--생산-소비자가-0이다"></span>
<span id="110-모델--머테리얼-배선-재감사-2026-08-27"></span>
<span id="12-소스cooked-디코더는-섰지만-선택-정책은-아직-없다"></span>
<span id="13-선언만-있고-소비자-0인-옵션"></span>
<span id="14-측정된-잔여-손실"></span>
<span id="15-검사-커버리지의-약점"></span>
<span id="16-서-있는-것-완료된-자산"></span>
<span id="17-정점-레이아웃--실측-2026-08-25"></span>
<span id="18--v2-68b--v3-메시별-packed-buffer-완료-생산-gpu-배선은-아직-없음-2026-08-27"></span>
<span id="19-라이트맵--uv1-의-소비자가-삭제됐다"></span>
<span id="2-무엇을-하려는가"></span>
<span id="3-설계--이미-정해진-것"></span>
<span id="31-ir-은-의도적으로-부유하다"></span>
<span id="32-좌표단위는-임포터가-통일한다"></span>
<span id="33-게이트-설계--실측으로-얻은-규칙-넷"></span>
<span id="34-실자산-비교로-담보되지-않는-것이-있다"></span>
<span id="4-v-이행--트랙-v-정점-레이아웃"></span>
<span id="4-이행--슬라이스"></span>
<span id="5-완료-기준"></span>
<span id="6-리스크"></span>
<span id="7-다른-계획과의-관계"></span>
<span id="i0-계획기준선--완료"></span>
<span id="i1-실물-디코더-배선--importermodeldecoder--완료-2026-09-01-재측정"></span>
<span id="i2-텍스처-자산-해석-정책--완료-2026-09-01--i2-e"></span>
<span id="i3-노멀-생성-패스--완료"></span>
<span id="i4-meshoptimizer-연동--둘-완료-lodlevels-잔여"></span>
<span id="i5-0-materialshadermeta-교차-계약--완료-2026-09-01--i7-c1에서-제품-합류"></span>
<span id="i5-m-experimentmaterial-정본-승격과-legacy-material-퇴역"></span>
<span id="i5-런타임-어댑터--b치환-로-확정"></span>
<span id="i6-전환--legacy-타입-은퇴-그다음-assimp-2026-09-01-정찰로-순서-정정"></span>
<span id="i7-cooked-경로--포맷코덱-완료-2026-08-25--c8e06ffa"></span>
<span id="i8-검사-자산게이트-보강"></span>
<span id="sec-0-당시-phase-4-편입-기록--왜-별도-페이즈가-아니었는가"></span>
<span id="sec-1-지금-무엇이-있는가--측정-2026-08-27-재감사"></span>
<span id="sec-2-무엇을-하려는가"></span>
<span id="sec-3-설계--이미-정해진-것"></span>
<span id="sec-4-v-이행--트랙-v-정점-레이아웃"></span>
<span id="sec-4-이행--슬라이스"></span>
<span id="sec-5-완료-기준"></span>
<span id="sec-6-리스크"></span>
<span id="sec-7-다른-계획과의-관계"></span>
<span id="v0-레이아웃-버전-규약--생산자는-v1-소비자는-i7"></span>
<span id="v1-속성-기술표--정본-하나--v-트랙의-착수점"></span>
<span id="v2-무손실-68b---구현검증-완료-2026-08-27"></span>
<span id="v3-메시별-마스크---packed-스트림-구현검증-완료-2026-08-27"></span>
<span id="v4-패스의-레이아웃-유도---i5-d로-이행-완료-2026-09-01"></span>
<span id="v5-멀티-슬롯-정점-버퍼--보류"></span>
<span id="v6-uv1-스트림---폐기-2026-09-01--lightmapbakerplan-l1이-승계"></span>
<span id="게이트--experimentcooked"></span>
<span id="구간-각각의-min--서로-더할-수-없다"></span>
<span id="남은-것"></span>
<span id="수치-release-반복-20-쿠킹-대-쿠킹"></span>
<span id="이-산정의-한계--소거율은-아직-실측이-없다"></span>
<span id="자--은퇴-래칫-게이트와-같은-자로-잰다"></span>
<span id="정정-하나--assimp-벤더링은-벤더링이-아니다"></span>
<span id="측정-①--은퇴-대상은-줄지-않았다-병행-단계가-늘렸다"></span>
<span id="측정-②--남은-대상-전수-제품-206--하네스-53--합-259"></span>
<span id="트랙-i"></span>
<span id="트랙-v"></span>
<span id="포맷-요점"></span>

</details>
