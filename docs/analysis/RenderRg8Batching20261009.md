# 큐 batch와 기록 저장소 재사용

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

큐 batch와 기록 저장소 재사용에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

SubmitQueues는 같은 큐의 연속 pass를 묶되 cross-queue producer 뒤와 consumer 앞에서 batch를 나눈다. 한 producer queue의 더 새로운 point가 이전 point를 포함하는 성질로 중복 wait를 줄인다. DX12QueueService가 native queue/batch의 수명을 관리한다.

## 변경·검증 기준

너무 큰 batch는 overlap을 없애고 너무 작은 batch는 제출 비용을 키운다. batch·native allocator/list 재사용은 같은 queue identity와 GPU 완료를 확인해야 하며 다른 queue/device 저장소를 섞지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp](../../Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp)
- [Engine/RenderEngine/RHI/RHIQueueRecorder.h](../../Engine/RenderEngine/RHI/RHIQueueRecorder.h)
