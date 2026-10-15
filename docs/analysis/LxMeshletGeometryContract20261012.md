# 메시렛 쿠킹·GPU 식별 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

현재 모델 CEMC는 15, granular geometry는 4, descriptor는 5이며 meshlet profile/builder는 2, LOD builder는 3이다. SceneGeometryIdentity는 64-byte로 geometry·placement·materialSlot을 구분한다. hierarchy의 실제 LOD는 각 cooked descriptor에서 읽는다.

## 확인해야 할 경계

원본 polygon 순번과 최종 LOD triangle ordinal을 혼동하지 않는다. chunk remap 뒤에도 cluster/primitive·원본 vertex 신원을 복원할 수 있어야 한다. decoder의 version 거부를 임의 런타임 변환으로 우회하지 않는다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/Experiment/Cooked/CookedModelFormat.h](../../Engine/RenderEngine/Experiment/Cooked/CookedModelFormat.h)
- [Engine/RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.h](../../Engine/RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.h)
- [Engine/RenderEngine/Experiment/MeshletData.h](../../Engine/RenderEngine/Experiment/MeshletData.h)
- [Engine/RenderEngine/Experiment/MeshLodData.h](../../Engine/RenderEngine/Experiment/MeshLodData.h)
- [Engine/RenderEngine/MaterialGraphGeometryContract.h](../../Engine/RenderEngine/MaterialGraphGeometryContract.h)
