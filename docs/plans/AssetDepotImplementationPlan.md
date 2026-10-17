# AssetDepot 현재 계약과 남은 수용 작업

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 서비스와 데이터의 위치

AssetDepot의 공개 facade는 `RenderEngine/DataSystem.h`다. 구현은 `AssetDepot.cpp`, Texture/Model/Material runtime 파일로 나뉜다. 별도 전역 AssetDepot 객체를 찾거나 SceneRuntime에 같은 이름의 서비스를 만들 필요가 없다.

`AssetLink<T>`는 asset/subasset identity와 expected kind를 가진 값이다. v4 또는 지원하는 v8 UUID를 검증하며, typed reference의 kind가 다르면 `FromReference`가 실패하고 기존 출력을 보존한다. link는 resident pin, weak owner, 작업 요청을 의미하지 않는다.

`MountAssetSet`는 CEMF3를 읽고 설치된 decoder의 kind/representation/schema를 검사한다. metadata 후보를 만들기 전 admission 상태·resolver revision·preparation epoch를 포착하고, 후보 검증 뒤 여전히 같은 상태인지 확인해 교체한다. mount ID는 실패한 transaction에서도 재사용하지 않는다. v2 package catalog는 별도 `MountCookedCatalog` 경로가 남아 있다.

## 획득과 비동기 요청

`TryAcquire`는 현재 link 또는 이미 획득한 descriptor에 연결된 resident 결과를 조회하는 API다. `RequestAsync`는 CPU bytes 읽기·무결성 확인·decode를 scheduler 작업으로 요청한다. 결과 상태는 Pending/Ready/Failed/Cancelled/Stale이며, Ready는 GPU upload나 fence 완료가 아니다.

요청 handle을 복사하면 같은 consumer state를 공유한다. 별도 `RequestAsync`는 다른 consumer를 만들 수 있고, 동일 generation의 work는 합류할 수 있다. 한 consumer의 Cancel은 공유 work나 다른 consumer를 취소하지 않는다. `Completion()`은 dependency token이며 GT/RT를 막아도 좋다는 허가가 아니다.

현재 typed dispatch는 texture, shader metadata, material과 graph/code program, model descriptor, mesh/skeleton/animation payload 계열을 명시적으로 다룬다. CEMF에 이름이 있다는 이유만으로 임의 T나 모든 cooked kind를 load할 수 없다. unsupported representation과 decoder 부재는 명시적 오류다.

## 세대·캐시·backing 소유권

모델 key는 typed identity/blob/resolver revision 및 exact-generation 구분을 담는다. descriptor에서 자식을 요구하는 경로는 해당 descriptor의 captured locator/source를 사용하며 최신 resolver로 구세대 자식을 재해석하지 않는다. texture는 logical descriptor key와 hash/format/recipe 기반 image key를 분리한다. byte가 같더라도 논리 asset과 runtime variant가 같다는 뜻은 아니다.

cache entry에는 weak live owner, budget으로 retained하는 strong owner, in-flight work가 각각 있다. 논리 eviction은 cache 보유를 내려놓는 일이다. 외부 consumer, work, source backing 및 GPU 사용까지 즉시 사라지는 것은 아니다. budget counter도 allocator overhead 전체나 OS resident bytes가 아니다.

`ArtifactByteSource`는 `Derived/` virtual locator와 backing owner를 묶는다. explicit capture/size/read는 I/O이고 mount/root 조회에서 몰래 실행하지 않는다. 물리 삭제는 [ArtifactStoreLeaseProtocol](../design/ArtifactStoreLeaseProtocol.md)의 별도 exclusive capability가 필요하다.

## 실제 소비자 연결

SceneManager는 문서를 worker에서 읽고 `PrepareSceneAssets`로 요청을 시작한다. owner thread가 readiness·epoch를 확인한 후 Scene/Entity를 구성하고 handoff한다. Foliage는 model descriptor → 선택 static mesh → material을 비동기로 연결하고 camera가 없어도 준비 상태를 poll한다. 두 흐름은 [AsyncAssetPreparation](../design/AsyncAssetPreparation.md), [FoliageAssetDepot](../design/FoliageAssetDepot.md)에 자세히 적었다.

## 남은 수용 기준

이 문서는 코드 존재를 확인했으며 전체 전환 완료를 선언하지 않는다. 구세대 pin을 든 unload/reload, mount 교체 중 요청, consumer cancel, shutdown drain, 손상 blob, decoder/version 불일치, hard dependency cycle, budget pressure를 실제 Editor/Player 경로에서 검증해야 한다. 모델·재질·텍스처 외의 자산 전면 통합, 자동 물리 collector와 GPU residency 일원화는 이 API의 존재만으로 구현되었다고 볼 수 없다.

## 구현 근거

- [Engine/RenderEngine/DataSystem.h](../../Engine/RenderEngine/DataSystem.h)
- [Engine/RenderEngine/AssetDepot/AssetDepot.cpp](../../Engine/RenderEngine/AssetDepot/AssetDepot.cpp)
- [Engine/RenderEngine/AssetDepot/AssetLink.h](../../Engine/RenderEngine/AssetDepot/AssetLink.h)
- [Engine/RenderEngine/AssetDepot/AssetRequest.h](../../Engine/RenderEngine/AssetDepot/AssetRequest.h)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h](../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h)
- [Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h](../../Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h)
- [Engine/RenderEngine/Experiment/Cooked/ArtifactByteSource.h](../../Engine/RenderEngine/Experiment/Cooked/ArtifactByteSource.h)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/SceneRuntime/FoliageComponent.cpp](../../Engine/SceneRuntime/FoliageComponent.cpp)
