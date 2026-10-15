# 합법적인 대체 큐 순서 탐색

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

합법적인 대체 큐 순서 탐색에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

SimulateQueues는 기존 순서와 compute 입력을 우선하는 후보를 비교한다. BuildQueueDependencies가 만든 실제 resource hazard를 만족하는 topological schedule 안에서만 탐색한다. 선행 graphics를 먼저 배치해 뒤 compute와 독립 graphics가 겹칠 여지를 평가한다.

## 변경·검증 기준

예측 span이 줄어도 GPU 실행 검증을 생략하지 않는다. 측정 누락, reader join, state handoff 비용과 검색 CPU 비용을 포함해 이득을 비교하고 동일 비용의 결정성을 확인한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
