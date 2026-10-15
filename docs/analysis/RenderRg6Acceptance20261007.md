# 제품 versioned 전환의 수용 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

제품 versioned 전환의 수용 경계에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

EnhancedSceneRenderer는 현재 ExplicitVersioned/DependencyOrder를 사용한다. 그 사실은 source wiring으로 확인되지만 과거 legacy/product CPU·GPU 수치를 현행 SceneHost의 비용으로 읽을 수 없다. geometry stage와 visibility workload 자체가 바뀌었기 때문이다.

## 변경·검증 기준

A/B는 실제로 존재하는 진단 경로만 사용한다. 같은 장면이어도 pass/workload가 다르면 scheduler overhead만의 비교가 아니며 CPU compile/record와 GPU pass sum을 합산 frame time으로 표시하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg6Acceptance20261007.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
