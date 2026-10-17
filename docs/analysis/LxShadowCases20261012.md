# 그림자 사례별 정확도 행렬

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

현재 shadow는 material binding·cooked topology·pose와 세 cascade의 light matrix를 함께 소비한다. alpha cutoff·winding·double-sided·음수/비균일 affine transform은 geometry 경로만 검사해서 닫을 수 없는 shader 의미다.

## 확인해야 할 경계

기본/alpha pass·equal·reject/앞뒤 winding·양면/변형/split 양쪽/먼 caster/여러 meshlet 장면을 분리한다. 각 사례의 예상 coverage와 독립 대조군을 적고 “nonempty”와 정확한 depth 동치를 구분한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Graph/ShadowCasterBounds.h](../../Engine/RenderEngine/Render/Graph/ShadowCasterBounds.h)
- [Tools/regression/material_scene_shadow_decal_tests.inl](../../Tools/regression/material_scene_shadow_decal_tests.inl)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/LxShadowCases20261012.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
