# AssetDepot implementation

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/AssetDepotImplementationPlan.md)

## 남은 작업과 완료 판정

- Scene·Prefab·audio의 typed 독립 payload loader와 source-free bootstrap 전환 범위를 확정한다. 현행 legacy-document 경로를 이미 제거했다고 가정하지 않는다.
- 관리 native-owner handle·NativeAOT 등록과 남은 legacy adapter 제거는 실제 소비 경로별로 수용한다.
- 취소와 공유 작업, resolver supersession, unmount 후 이전 세대 rehydration, 예산 회수·shutdown·GPU retirement를 현재 master의 제품 실행으로 검증한다. 캐시 retained bytes와 실제 해제량을 분리한다.

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

<span id="implemented-source-slices-2026-10-08"></span>
<span id="invariants"></span>
<span id="sec-implemented-source-slices-2026-10-08"></span>
<span id="sec-invariants"></span>
<span id="sec-slice-order"></span>
<span id="sec-support-and-verification-boundaries"></span>
<span id="sec-verification-boundary"></span>
<span id="slice-order"></span>
<span id="support-and-verification-boundaries"></span>
<span id="verification-boundary"></span>

</details>
