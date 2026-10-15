# Lattice 재질과 메시렛의 현재 연결

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

Lattice compiler의 재질 프로그램은 SceneHost의 geometry/material 배선을 통해 실제 mesh shader와 간접 명령에 연결된다. 원래 LX.MeshWorld 중심 표현은 현재 object-space posed output와 instance world 분리를 함께 설명해야 한다. 현재 opaque·blended·shadow는 cooked meshlet을 요구하며 과거 CREATOR_LX_MESHLETS A/B 옵션을 제품 fallback으로 안내하지 않는다.

## 확인해야 할 경계

cook primitive/corner 순서, geometryKey/placement/materialSlot과 GPU descriptor를 대조한다. 지원 장치 판정과 cache 예산 실패를 실제 오류로 보고하고 과거 indexed 동치 측정으로 현재 hybrid visibility를 수용하지 않는다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [Engine/RenderEngine/MaterialGraphGeometryContract.h](../../Engine/RenderEngine/MaterialGraphGeometryContract.h)
- [Lattice/Material/LXMaterialCompiler.cpp](../../Lattice/Material/LXMaterialCompiler.cpp)
