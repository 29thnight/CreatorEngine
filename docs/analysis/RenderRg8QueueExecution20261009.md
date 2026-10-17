# 큐 기록·제출·완료 실행

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

큐 기록·제출·완료 실행에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

SubmitQueues는 fresh owned versioned graph, 같은 device incarnation의 distinct graphics/compute queue, pinned owner와 nonaliased 자원을 요구한다. DX12 live RenderOnce의 owned 경로가 SubmitOwnedGraph를 호출한다. 일반 경로는 RecordParallel과 EnqueueRecordedBatch다.

## 변경·검증 기준

CPU queue admission, GPU timeline wait와 CPU drain을 구분한다. 일부 제출 뒤 실패한 자원은 이미 GPU에 노출될 수 있으므로 단순 rollback으로 파괴하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp](../../Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg8QueueExecution20261009.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
