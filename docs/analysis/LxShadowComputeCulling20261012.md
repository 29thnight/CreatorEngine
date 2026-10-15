# 그림자 compute 메시렛 컬링

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

ShadowStage는 cascade별 posed geometry와 광원 투영으로 목록·간접 명령을 선언한다. GPU survivor 목록과 bounded dispatch arguments를 실제 shadow mesh shader가 읽는다. 현재 제품은 전체 meshlet/direct indexed 대체 경로를 가정하지 않는다.

## 확인해야 할 경계

reset→cull→finalize→indirect 소비의 UAV/read/argument 경계를 검사한다. 비유한·경계·0 survivor와 최대 후보를 포함하고 실제 survivor ID·깊이 attachment를 readback으로 비교한다. camera HZB는 shadow 판정에 사용하지 않는다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang)
