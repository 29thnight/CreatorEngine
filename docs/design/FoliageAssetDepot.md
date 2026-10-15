# Foliage의 typed asset 준비와 세대 보존

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## binding 순서

`FoliageComponent::RebindFoliageType`은 이전 binding consumer를 취소하고 새 binding을 만든다. 모델 typed link를 요청한 뒤 `PollAssetBindings`에서 descriptor의 선택 mesh를 찾는다. 현재 AssetDepot 경로는 static mesh를 요구하며 skinned mesh를 `UnsupportedRepresentation`으로 거부한다.

descriptor를 pin한 채 mesh와 material을 요청한다. resident mesh는 `TryAcquire`로 얻고 없으면 descriptor의 exact generation을 사용하는 `RequestAsync`를 시작한다. material도 해당 descriptor와 material link에 연결한다. 이름만으로 최신 cache를 다시 조회해서 구세대 mesh와 신세대 material을 섞는 계약이 아니다.

`FoliageSystem::Update`는 camera/renderScene 조회 전에 binding 상태를 poll한다. 카메라가 없거나 가시성이 없다고 CPU 준비가 멈추지 않는다. 실제 culling/render proxy publication은 이후 렌더 경계다.

## legacy authoring의 제한

model GUID가 없는 name-only 데이터는 한 번 stem→GUID로 이관하며 legacy-source flag를 설정할 수 있다. 이미 저장된 typed link는 mount가 없다고 이름 검색으로 되돌아가지 않는다.

legacy source 준비는 mounted record가 없고, 해당 type에서 허용되며, `PathFinder::IsAssetAuthoringEnabled()`인 경우에만 가능하다. 잘못된 kind·representation이나 존재하는 mount의 오류를 source import로 숨기지 않는다. legacy 준비가 바뀌거나 실패하면 명시적 rebind를 요구하고 stale/failure를 남긴다.

## 관찰과 검증

binding status는 Pending/Ready/Failed/Cancelled/Stale이며 message/error를 함께 보아야 한다. Ready를 GPU resident 또는 화면 검증으로 해석하지 않는다. 재바인딩, component 제거, model 세대 교체, material 오류, Player의 source 없는 상태, camera 없는 준비 진행을 각각 검증해야 한다. 이 문서에서는 코드 경로만 확인했다.

## 구현 근거

- [Engine/SceneRuntime/FoliageComponent.cpp](../../Engine/SceneRuntime/FoliageComponent.cpp)
- [Engine/SceneRuntime/FoliageComponent.h](../../Engine/SceneRuntime/FoliageComponent.h)
- [Engine/SceneRuntime/FoliageSystem.cpp](../../Engine/SceneRuntime/FoliageSystem.cpp)
- [Engine/RenderEngine/AssetDepot/AssetRequest.h](../../Engine/RenderEngine/AssetDepot/AssetRequest.h)
