# 멀티큐 채택 상태를 읽는 기준

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

멀티큐 채택 상태를 읽는 기준에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

현재 소스에는 queue planning·실행·diagnostics가 구현되어 있으나 이것을 “RG8 성능 최적화 완료”로 표시할 근거는 이번 감사에 없다. live owned queue는 선택 경로이고 aliasing 조합은 거부한다. 과거 종료 뒤 철회된 단계 판정은 현재 완료 주장으로 복원하지 않는다.

## 변경·검증 기준

기능 존재, correctness, 제품 workload overlap, CPU+GPU 총 비용, 메모리·전환 수명과 backend parity를 별도 게이트로 둔다. 이득이 없으면 기존 경로 유지가 유효한 채택 결정이며 결과 없는 인일 회수는 하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
