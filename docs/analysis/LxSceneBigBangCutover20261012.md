# Scene 단일 배선의 현재 책임

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

SceneSurfaceTargets는 MRT·depth 생성과 clear만 맡는다. 실제 geometry·visibility·material reconstruction은 METEOR SceneHost가 소유한다. Geometry→Cull.First→Occluder→Cull.Final→Surface와 독립 Shadow를 BuildPipelineDesc가 연결한다. 이전 GBuffer/Deferred 알고리즘을 호환 클래스에 남긴 구조가 아니다.

## 확인해야 할 경계

소스·project 편입·shader entry와 실제 호출자를 함께 확인한다. 옛 ABI/CEMC 버전이나 indexed 복구 옵션을 가져오지 말고 현재 artifact를 재쿠킹한다. resource-only class에 재질·draw 책임이 다시 들어가지 않도록 검토한다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Passes/Geometry/SceneSurfaceTargets.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/SceneSurfaceTargets.cpp)
- [Engine/RenderEngine/RenderEngine.vcxproj](../../Engine/RenderEngine/RenderEngine.vcxproj)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/LxSceneBigBangCutover20261012.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
