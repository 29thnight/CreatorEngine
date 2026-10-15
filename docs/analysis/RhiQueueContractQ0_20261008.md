# RHI 큐·타임라인의 기초 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 신원과 완료

RHIQueueIdentity는 process-unique device incarnation, queueId, Graphics/Compute/Copy 종류를 포함한다. RHITimelinePoint는 fence owner와 발행된 비영 값이다. IsComplete는 Available 상태이며 실제 completedValue가 point 이상일 때만 true다. Unavailable·device lost·예약만 된 값은 완료가 아니다.

ValidateQueueWait는 같은 device generation과 유효 point를 요구한다. ValidateQueueHandoff는 서로 다른 queue와 producer 자신의 timeline, common 경계를 통한 release/acquire를 확인한다. queue Wait는 GPU 대기를 enqueue하는 계약이며 CPU 전체 drain으로 바꾸지 않는다.

## 실제 구현과 소비

DX12QueueService는 native queue·fence·봉인된 one-shot command batch를 구현한다. Submit은 CPU admission이고 queue가 GPU 완료까지 batch/resource를 보존한다. EnhancedRenderGraph::SubmitQueues가 이를 graph batch·cross-queue wait·최종 join과 연결한다. RHICompletionPoint의 단일 graphics scalar와 RHITimelinePoint를 섞어 비교하지 않는다.

## 실패와 검증

signal 실패는 성공 point를 게시하지 않아야 하며 일부 native 제출이 발생한 뒤 실패한 resource는 안전한 completion 증거 없이 회수하지 않는다. 장치 세대가 바뀌거나 같은 batch를 재제출하는 입력을 거부해야 한다. 정상·실패·device-loss, signal/wait 순서와 CollectCompleted를 실제 장치에서 확인한다.

이 문서는 기초 API와 DX12 소비의 정적 확인이다. 모든 queue가 하드웨어에서 겹친다는 보장, copy queue의 제품 자동 배치, Vulkan queue service 동등 수용을 포함하지 않는다.

## 소스 근거

- [Engine/RenderEngine/RHI/RHIQueueContract.h](../../Engine/RenderEngine/RHI/RHIQueueContract.h)
- [Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp](../../Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
