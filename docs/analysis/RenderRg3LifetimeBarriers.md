# 버전 컬링·수명·배리어

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

버전 컬링·수명·배리어에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

명시적 버전 모드는 side effect와 imported 최종 writer에서 데이터 생산자를 역추적한다. Read는 해당 버전, Modify는 부모 버전을 유지한다. WAR/WAW는 생존을 만들지 않고 살아남은 접근의 물리 덮어쓰기 순서를 유지한다. CreateTransients와 PlanBarriers는 생존 실행 순서를 소비한다.

## 변경·검증 기준

죽은 중간 writer를 제거해도 이전 reader와 다음 writer가 안전하게 연결되어야 한다. transient 반납은 GPU completion 이후이며 imported state writeback은 실제 upload admission 후다. Compile 성공을 외부 resource state 확정으로 해석하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg3LifetimeBarriers.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
