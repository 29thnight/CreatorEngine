# 카메라 메시렛·HZB 복구·GPU LOD

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

현재 FirstCull/Occluder/FinalCull은 첫 목록·실제 깊이·복구 목록을 독립 handle로 연결한다. current HZB는 occluder depth에서 만들어 최종 opaque recovery에 사용한다. GeometryVisibility shader는 cooked replacement group의 error와 projected metric으로 cut을 선택하고 허용된 부모가 있으면 child를 중복 선택하지 않는다.

## 확인해야 할 경계

급격한 카메라 변화, near plane·비유한 bounds·jitter·viewport 변화에서 보수적으로 유지하는지 확인한다. 공간 patch의 parent/children 원자 교체와 경계 균열·중복/누락 primitive를 검사한다. blended는 ordered frustum-only 목록으로 별도 검증한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneFirstCullStage.inl](../../Engine/RenderEngine/MaterialGraphSceneFirstCullStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneFinalCullStage.inl](../../Engine/RenderEngine/MaterialGraphSceneFinalCullStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneOccluderStage.inl](../../Engine/RenderEngine/MaterialGraphSceneOccluderStage.inl)
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp](../../Engine/RenderEngine/GpuGeometryOcclusion.cpp)
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/MeteorCameraMeshletCulling20261013.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
