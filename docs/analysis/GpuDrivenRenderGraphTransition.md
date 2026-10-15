# GPU-driven과 버전 그래프의 통합

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

현재 GPU 목록·indirect command·material tile은 dummy ordering token이 아니라 실제 buffer handle로 그래프에 연결된다. SceneHost는 ExplicitVersioned를 요구하고 stage가 발행한 정확한 handle을 다음 consumer에 전달한다. bounded input·준비 실패·제출 후 cache 게시를 같은 경로에서 다룬다.

## 확인해야 할 경계

GPU visibility/command가 graph 밖에서 숨겨진 부수효과로 생산되지 않는지 확인한다. 실패한 material/geometry admission을 다른 generation의 성공으로 대신하지 않으며 dispatch 전에 capability·buffer range·indirect state를 검증한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.h](../../Engine/RenderEngine/MaterialGraphSceneHost.h)
- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
