# 표면·그림자 타깃의 최초 writer

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

표면·그림자 타깃의 최초 writer에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

현재 SceneSurfaceTargets는 GBuffer 다섯 색과 깊이 생성·클리어만 맡는다. EnhancedShadowPass는 3-slice shadow map을 초기화하고 SceneHost의 Surface/Shadow 단계가 다음 버전을 기록한다. 과거 EnhancedGBufferPass 중심 배치·일반 표면 설명은 현재 구현이 아니다.

## 변경·검증 기준

새 target은 초기화 producer가 정확히 한 번 존재해야 한다. 빈 geometry 프레임에도 clear·최종 표시가 유지되는지 확인하고 실제 표면 shader와 resource-only target 클래스를 구분한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Geometry/SceneSurfaceTargets.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/SceneSurfaceTargets.cpp)
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp)
- [Engine/RenderEngine/MaterialGraphSceneSurfaceStage.inl](../../Engine/RenderEngine/MaterialGraphSceneSurfaceStage.inl)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
