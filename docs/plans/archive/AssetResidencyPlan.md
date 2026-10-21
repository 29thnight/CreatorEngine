# 자산 GPU 상주 관리 — 해제 통보 경로 설계

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/AssetResidencyPlan.md)

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

<span id="1-지금-무엇이-일어나는가--실측"></span>
<span id="11--더-급한-것--주소-재사용"></span>
<span id="2-제약--설계를-정하는-것들"></span>
<span id="3-설계--세-조각"></span>
<span id="31-원안--키를-주소에서-신원으로-correctness"></span>
<span id="31-키를-주소에서-신원으로-correctness--2026-08-08-완료"></span>
<span id="32-안-쓰는-것을-버린다-leak"></span>
<span id="33-회수-가능한-묘지-safety"></span>
<span id="34-나중에-즉시-통보-선택"></span>
<span id="4-완료-관측-②--2026-08-08"></span>
<span id="4-원안-관측--이것이-없으면-판단이-안-된다"></span>
<span id="5-순서"></span>
<span id="6-하지-않을-것"></span>
<span id="7-참고-사례--자체-엔진-방송과의-구조-비교-2026-08-08"></span>
<span id="71-그쪽-구조"></span>
<span id="72-차이와-판정"></span>
<span id="73-실패-사례에서-얻은-힌트"></span>
<span id="sec-1-지금-무엇이-일어나는가--실측"></span>
<span id="sec-2-제약--설계를-정하는-것들"></span>
<span id="sec-3-설계--세-조각"></span>
<span id="sec-4-완료-관측-②--2026-08-08"></span>
<span id="sec-4-원안-관측--이것이-없으면-판단이-안-된다"></span>
<span id="sec-5-순서"></span>
<span id="sec-6-하지-않을-것"></span>
<span id="sec-7-참고-사례--자체-엔진-방송과의-구조-비교-2026-08-08"></span>

</details>
