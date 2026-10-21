# 모델·Vertex·Mesh·Texture 구조 개선 및 비동기 로딩 계획

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/ModelGeometryTextureImprovementPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [AssetDepotImplementationPlan](AssetDepotImplementationPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- descriptor·payload 분리, typed 요청·공유 이미지 저장소·명시적 owner·frame pin은 AssetDepot의 구현을 정본으로 삼는다. 구 G3/G4/G6/G7을 별도 신규 구조로 중복 구현하지 않는다.
- 실제로 남은 legacy 선언/주석 정리, source/editor adapter 범위, 비동기 배치의 실패/취소·재임포트·씬 변경·Player 수용을 소비자별로 검증한다. cook GPU-ready 전환은 TexturePipelinePlan이 소유한다.

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

<span id="1-목표와-적용-원칙"></span>
<span id="2-현재-구조와-확인된-사실"></span>
<span id="21-assimp와-legacy-모델-잔여-항목"></span>
<span id="22-vertex"></span>
<span id="23-mesh"></span>
<span id="24-texture"></span>
<span id="3-이번에-구현한-비동기-모델-배치"></span>
<span id="31-멈춤을-유발하던-경로"></span>
<span id="32-현재-구현-경계"></span>
<span id="33-검증-결과와-해석-범위"></span>
<span id="34-아직-해결한-것으로-보지-않는-범위"></span>
<span id="4-남은-구조-개선-항목"></span>
<span id="5-권장-실행-순서"></span>
<span id="6-검증-및-완료-기록-규칙"></span>
<span id="g1-불필요-타입과-불완전-mesh-api-정리--p2"></span>
<span id="g2-텍스처-식별자조회-정책-통합--p1"></span>
<span id="g3-불변-cpu-이미지-저장소와-요청-중복-제어--p1"></span>
<span id="g4-rhi-자산-의존-축소와-검증된-업로드-뷰--p1"></span>
<span id="g5-지오메트리-종류와-전체-식별자-보존--p1"></span>
<span id="g6-gpu-준비부재실패-상태-분리--p1"></span>
<span id="g7-공통-프레임-자원-준비-추출--p2"></span>
<span id="g8-비동기-배치-후속-안정화와-적용-범위-확대--p1"></span>
<span id="g9-디코드압축을-cook-단계로-이동--p2-기존-계획-연계"></span>
<span id="sec-1-목표와-적용-원칙"></span>
<span id="sec-2-현재-구조와-확인된-사실"></span>
<span id="sec-3-이번에-구현한-비동기-모델-배치"></span>
<span id="sec-4-남은-구조-개선-항목"></span>
<span id="sec-5-권장-실행-순서"></span>
<span id="sec-6-검증-및-완료-기록-규칙"></span>

</details>
