# 스케줄러 CPU 비용과 scratch 수명

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

스케줄러 CPU 비용과 scratch 수명에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

QueuePlanningScratch는 한 BuildQueueSchedule 호출의 여러 후보 시뮬레이션에서 predecessor/successor·reader epoch 등 저장소를 재사용한다. 이는 allocation churn을 줄이는 구현이며 GPU 작업량 감소가 아니다. 통계에는 dependency·simulation 비용을 구분하는 경로가 있다.

## 변경·검증 기준

후보마다 모든 의미 상태를 초기화하고 capacity만 재사용해야 한다. graph 크기·후보 수를 고정해 schedule CPU wall time과 할당을 보고하고 기록·제출 비용을 별도로 남긴다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg8SchedulerCost20261009.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
