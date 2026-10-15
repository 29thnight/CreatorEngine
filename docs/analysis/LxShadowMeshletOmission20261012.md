# 보이는 그림자 메시렛 누락 변이

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

shadow 목록은 source meshlet ID를 유지해 indirect shader 소비와 연결한다. prepared candidate 수는 실제 선택·coverage를 증명하지 않으므로, 보이는 중간 meshlet을 의도적으로 누락시킨 결과를 oracle이 거부하는 검사가 유용하다.

## 확인해야 할 경계

여러 meshlet이 반드시 필요한 topology와 실제 shadow에 기여하는 ID를 선택한다. GPU 목록·depth 차이·최종 shadow 차이를 함께 확인하고 변이 입력을 정상 성능 표본에서 제외한다. 이번 감사에서 변이 실행은 하지 않았다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang)
