# 보관: 모델 단일 경로 전환의 현재 계약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 승계한 정책

현재 모델 저작 writer는 AuthorModelAsset이다. UUIDv8 model/subasset identity, schema-v2 sidecar, 새 immutable generation을 하나의 transaction으로 생산한다. v1 sidecar는 보존할 identity의 정본이 아니며 non-identity 설정만 이관한다. v2의 prior stable key와 generation을 후속 import에 사용한다.

자동 복구는 derived file을 다시 만들 수 있어도 expectedModelId를 임의로 교체하지 않는다. 실패 시 legacy loader나 병행 runtime으로 조용히 우회하는 계획은 폐기된 역사다. Editor의 재질 graph 복구와 모델 identity 복구는 별개 정책이다.

## import 설정과 게시

buildMeshlets override는 importSettings에 저장되며 변경은 기존 generation 패치가 아니라 새 generation 게시다. lodLevels는 0으로 coarse LOD를 끄거나 1..7을 요청하며 양수 override는 meshlet을 활성화한다. 이 authoring API의 기본값과 현재 SceneHost가 cooked meshlet을 요구하는 admission을 함께 확인해야 한다.

generation directory의 원자 게시 후 canonical sidecar를 마지막으로 바꾼다. runtime은 exact-generation descriptor와 payload를 잡으며 후속 resolver 변경이 이미 캡처한 자식을 바꾸지 않는다. 이 수명 계약은 [AssetDepot](../AssetDepotImplementationPlan.md)에서 다룬다.

## 보관 경계

옛 MBC0~MBC11 공수·완료 선언과 corpus 측정은 당시 전환의 기록이다. 최신 codec·GPU meshlet 구조·source-free AssetSet·비동기 scene 준비까지 그 완료에 포함시키지 않는다. 현재 후속은 [AssetSetImportReceipts](../../design/AssetSetImportReceipts.md), [AsyncAssetPreparation](../../design/AsyncAssetPreparation.md), [GpuDrivenMeshletDxrWiring](../../design/GpuDrivenMeshletDxrWiring.md)이다.

## 구현 근거

- [Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.h](../../../Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.h)
- [Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.cpp](../../../Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.cpp)
- [Engine/RenderEngine/Assets/AssetIdentityProfile.h](../../../Engine/RenderEngine/Assets/AssetIdentityProfile.h)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h](../../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
