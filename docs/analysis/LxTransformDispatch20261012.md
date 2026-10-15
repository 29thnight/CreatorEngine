# Geometry 변환 dispatch와 공유 생산자

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

MeshSurfaceBatch::PrepareDispatch는 같은 device/pipeline의 준비된 batch를 job/group table로 묶고 RecordDispatch가 하나의 compute dispatch를 기록한다. GeometryStage는 고유 pose 생산자를 선언하며 current/previous/shadow consumer가 같은 output을 공유할 수 있다. object-space output에는 world를 별도 적용한다.

## 확인해야 할 경계

같은 geometry/pose의 중복 transform, 다른 pose나 generation의 잘못된 공유를 검사한다. job/group/dispatch 한도와 finite affine matrix·bone layout을 확인하고 dispatch 감소가 동일한 vertex 작업량·출력을 보존하는지 측정한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl](../../Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
