# 그림자 기준선과 backOff 검사

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

EnhancedShadowPass는 receiver cascade를 만들고 관련 caster가 광원 쪽으로 멀면 backOff depth 범위를 확장한다. SceneHost는 3-slice D32 shadow를 각 cascade별로 갱신한다. 화면 밖 caster와 카메라 가시성은 같은 판정이 아니다.

## 확인해야 할 경계

원거리 가림체·skin pose·최종 IBL shade를 현행 바이너리로 확인한다. 과거 Debug/Release pass 표는 현재 build 결과가 아니며 source/runtime hash, validation dropped count와 정상 shutdown까지 다시 수집한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp)
- [Engine/RenderEngine/Render/Graph/ShadowMath.h](../../Engine/RenderEngine/Render/Graph/ShadowMath.h)
- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/LxShadowBaseline20261011.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
