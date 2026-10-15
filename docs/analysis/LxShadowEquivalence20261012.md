# 세 cascade의 그림자 동치 기준

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

그림자 결과는 최종 color만이 아니라 각 cascade depth·matrix·caster selection을 함께 결정한다. 같은 두 그림이 보여도 한 cascade가 비어 있거나 양쪽이 같은 오류를 가진 가능성을 배제해야 한다. 현재 shadow의 실제 producer는 METEOR.Scene.Shadow.C0~C2다.

## 확인해야 할 경계

동일 frame·pose·light input으로 세 map을 비교하고 independent coverage expectation을 둔다. 과거 indexed/meshlet 실행 옵션이 없는 현재 코드에 존재하지 않는 A/B 명령을 안내하지 않는다. comparator 허용치는 이유와 함께 고정한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp)
- [Tools/regression/material_scene_shadow_decal_tests.inl](../../Tools/regression/material_scene_shadow_decal_tests.inl)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/LxShadowEquivalence20261012.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
