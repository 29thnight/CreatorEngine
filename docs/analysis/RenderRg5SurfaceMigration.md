# SceneHost 단계별 표면 출력

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

SceneHost 단계별 표면 출력에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

SceneHost의 Geometry/FirstCull/Occluder/FinalCull/Surface가 실제 생산한 handle을 전달한다. Surface는 visibility를 바탕으로 material reconstruction을 수행하고 color·temporal은 명시적 material tile/argument와 visibility 계약을 받는다. 일반 legacy GBuffer에 LX가 덧그리던 과거 구조와 다르다.

## 변경·검증 기준

단계 핸들 누락·외부 graph epoch·잘못된 visibility 또는 material argument를 거부해야 한다. NormalizeSceneReads의 compatible read 병합을 사용하고 중복 writer를 단순 dedup으로 숨기지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.h](../../Engine/RenderEngine/MaterialGraphSceneHost.h)
- [Engine/RenderEngine/MaterialGraphSceneSurfaceStage.inl](../../Engine/RenderEngine/MaterialGraphSceneSurfaceStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneAccess.h](../../Engine/RenderEngine/MaterialGraphSceneAccess.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
