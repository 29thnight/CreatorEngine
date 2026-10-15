# 보관: 실험 import 계획에서 제품 자산으로

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 이전 I/V 트랙의 종료 의미

옛 계획은 import IR와 vertex layout을 병렬 experiment 경로에서 검증하던 문서였다. 현재 “제품 소비자 0”이나 “legacy Assimp가 화면을 그린다”는 설명으로 사용할 수 없다. 폴더명 Experiment가 남아 있다는 사실도 live 등록·실행 여부의 판정 기준이 아니다.

현재 제품 authoring entry는 AuthorModelAsset이고 결과는 stable identity, sidecar, cooked generation이다. DataSystem과 AssetDepot가 이를 resolve·준비하고 Scene renderer의 material/geometry consumer가 exact payload를 사용한다. BuildTool의 AssetCooking은 source model generation과 typed output 검증을 package 흐름에 연결한다.

## 보존할 설계 요구

정점 layout은 source importer의 내부 구조를 GPU에 그대로 노출하는 계약이 아니다. material program의 input과 model vertex layout, meshlet topology, skin pose, indirect 소비가 일치해야 한다. position만 맞는 screenshot으로 normal/UV/skin/material identity의 보존을 증명하지 않는다.

원본 decode 오류, sidecar identity 오류, GPU admission 실패를 구분해 진단한다. 재질 graph를 복구할 수 있다는 사실이 잘못된 geometry를 수용할 이유는 아니다. Player에서는 source importer fallback 대신 strict cooked closure를 요구한다.

## 후계

[모델 cutover 계약](ModelAssetBigBangCutoverPlan.md)은 identity writer, [AssetDepot](../AssetDepotImplementationPlan.md)는 runtime 요청, [GpuDrivenMeshletDxrWiring](../../design/GpuDrivenMeshletDxrWiring.md)은 실제 render 연결을 설명한다. 옛 실험 숫자와 실패 기록을 이 최신 경로의 실행 결과로 합산하지 않는다.

## 구현 근거

- [Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.cpp](../../../Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.cpp)
- [Engine/RenderEngine/DataSystem.cpp](../../../Engine/RenderEngine/DataSystem.cpp)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.cpp](../../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.cpp)
- [Engine/RenderEngine/RHI/ModelVertexInputLayout.cpp](../../../Engine/RenderEngine/RHI/ModelVertexInputLayout.cpp)
- [BuildTool/AssetCooking.cs](../../../BuildTool/AssetCooking.cs)
