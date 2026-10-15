# GPU geometry 정적 검토 기준

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

SceneHost는 sealed geometry와 material을 받아 mesh-indirect 경로를 요구한다. 현재 CEMC15/geometry artifact4와 cooked replacement hierarchy가 소비되므로 static opaque의 선택적 native GBuffer만 GPU-driven이라는 이전 범위가 아니다. Surface·Shadow·Blended·Temporal은 각 목적의 명시적 producer/consumer를 가진다.

## 확인해야 할 경계

cook→decoder→geometry partition→GPU payload→shader 접근→실제 pass 연결까지 대조한다. 일반 visibility helper의 fallback을 제품 SceneHost의 fallback으로 확대하지 않는다. prepared count와 GPU survivor를 구분한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [Engine/RenderEngine/Experiment/Cooked/CookedModelFormat.h](../../Engine/RenderEngine/Experiment/Cooked/CookedModelFormat.h)
