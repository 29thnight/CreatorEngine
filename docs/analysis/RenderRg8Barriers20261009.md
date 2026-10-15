# 공통 barrier 계획과 큐 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

공통 barrier 계획과 큐 경계에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

queue 실행은 일반 graph의 PlanBarriers와 RecordPassBarriers/Body/FinalBarriers를 재사용한다. multi-queue 계획에서 batch boundary와 prologue/epilogue를 특수화하고 같은 batch 안의 합법적 상태는 유지한다. 반복 phase의 UAV/transition도 같은 planner가 처리한다.

## 변경·검증 기준

임의 pass name으로 barrier를 빼지 않는다. shared read 뒤 writer가 모든 reader를 기다리는지, imported final state와 queue common 경계가 복구되는지 확인한다. 실행 시도 후에는 Reset 없이 일반 Execute로 돌아가지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg8Barriers20261009.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
