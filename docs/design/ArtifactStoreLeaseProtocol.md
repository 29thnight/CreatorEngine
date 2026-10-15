# Artifact backing lease와 물리 회수

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 보호하는 대상

`ArtifactStoreGuard`는 로컬 파일시스템의 immutable release 디렉터리 하나 또는 PAK 하나를 보호한다. 개별 decoded object의 reference count나 GPU fence를 대신하지 않는다. sibling `.asset-store.guard`는 영구 protocol metadata이며 backing cleanup과 함께 이동·교체·삭제하지 않는다.

상태는 Acquired, Unmanaged, Busy, Invalid로 구분된다. guard가 없는 legacy backing은 읽을 수 있으나 collection 자격이 생기지 않는다. 오래된 파일이거나 mount 목록에서 빠졌다는 이유만으로 삭제 가능 판정을 내릴 수 없다.

## 발행 → 읽기 → 회수

1. 생산자는 새 backing 경로에 `BeginPublication`으로 exclusive guard를 획득한다. 이미 존재하는 backing을 사후 등록하지 않는다
2. candidate를 atomic rename한 뒤 `CommitPublication`으로 backing의 file identity와 incarnation을 guard에 기록·flush한다. 이 단계가 실패하면 정상 등록된 발행으로 보고하지 않는다
3. source/archive 생성 시 `OpenShared`를 호출하고 owner가 guard를 유지한다. alias·reparse·hardlink 및 guard/backing identity 변조를 검사한다
4. collector는 모든 active release/manifest 참조를 먼저 retire한 뒤 `TryAcquireCollection`으로 exclusive capability를 얻는다. 물리 mutation 직전에 `RevalidateCollection`을 호출하고 작업이 끝날 때까지 guard를 유지한다

`TryAcquireCollection`은 잠깐 관측한 bool이 아니라 들고 있어야 하는 권한 객체다. Busy와 Invalid를 무시하고 별도 delete를 실행하면 계약을 깨뜨린다.

## bytes 수명과 경계

`ArtifactByteSource` owner는 backing을 pin한다. loose artifact는 capture로 열린 파일을 좁혀 보유할 수 있으며, immutable PAK/memory source는 빈 narrowed 결과와 기존 owner 유지로 계약을 만족할 수 있다. capture 실패 시 원래 pathname으로 fallback해서는 안 된다. capture 성공 자체는 typed SHA/schema 검증이 아니고 이후 reader가 이를 수행한다.

PAK은 실제 FILE을 연 뒤 `ValidateOpenedFile`로 guard와 파일 open 사이 identity 변경을 검사한다. 외부 프로그램의 비협조적 삭제·교체를 모든 OS에서 막는 보장은 없으며 변조 감지와 fail-closed가 목적이다. network/distributed storage는 범위 밖이다.

## 구현 수준

AssetSet 생산자는 새 output 발행에 guard를 사용하고 output blob은 cache의 독립 복사본으로 만든다. cache GC가 발행된 release를 깨지 않도록 hardlink 공유를 전제하지 않는다. 이 protocol이 존재한다고 자동 TTL/LRU disk collector, quota UI 또는 복구 daemon이 구현된 것은 아니다. 별도 collector가 추가될 때에도 active manifest retirement와 exclusive guard를 둘 다 만족해야 한다.

## 구현 근거

- [Engine/Utility_Framework/ArtifactStoreGuard.h](../../Engine/Utility_Framework/ArtifactStoreGuard.h)
- [Engine/RenderEngine/Experiment/Cooked/ArtifactByteSource.h](../../Engine/RenderEngine/Experiment/Cooked/ArtifactByteSource.h)
- [Engine/Utility_Framework/Paklib.hpp](../../Engine/Utility_Framework/Paklib.hpp)
- [Tools/AssetCooker/AssetSetBuild.cpp](../../Tools/AssetCooker/AssetSetBuild.cpp)
