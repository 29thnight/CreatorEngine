# 태스크 스케줄러 통합의 현재 범위

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 결정

공용 실행 기반은 enkiTS를 감싼 thread_pool/job_scheduler다. 이전 Core.ThreadPool·WorkerPool 이름이나 전용 animation pool을 새 consumer에 복구하지 않는다. 실행 정책을 복제하지 않고 domain별 preparation, chunk 구성, join 위치를 소비자가 소유한다.

`EngineBootstrap::InitializeRuntime`은 profiler 등록 hook을 먼저 설치하고 scheduler를 시작한다. shutdown은 SceneManager 정리와 accepted graph drain을 경계로 서비스 수명을 끝낸다. SceneManager가 전역 worker lifecycle을 소유하는 과거 설계는 현재 정본이 아니다.

## 이관 시 보존해야 할 계약

scene load는 worker 문서/asset 준비와 owner scene 게시를 나눈다. AssetDepot은 consumer 상태와 공유 generation work를 분리한다. renderer의 command recording은 CPU task가 끝나도 command allocator·resource·submission 소유권을 GPU 완료까지 유지해야 한다. animation 또한 계산 task 종료와 scene 상태 적용의 경계를 구분한다.

통합의 수용 기준은 “std::thread 또는 std::async가 0개”가 아니다. 장기 blocking/IPC/Presentation/RHI loop는 전용 수명이 필요하다. 짧은 계산 batch를 공용 scheduler로 옮길 때 생존 capture, dependency, terminal error와 종료 drain이 동등한지를 판정한다.

## 남은 최적화 의도

priority/fairness, animation grain 크기, 긴 import와 frame 작업의 경합은 workload 근거가 생긴 뒤 조정한다. 전용 풀을 추가하거나 자체 work-stealing runtime을 다시 만드는 것은 기본 계획이 아니다. 옛 단독 microbenchmark와 machine-specific thread 수는 현재 runtime의 성능 보장으로 재사용하지 않는다.

API·실패 계약은 [JobSchedulerDesign](../design/JobSchedulerDesign.md)이 정본이다. 현재 문서는 통합 범위와 이후 수용 순서를 남긴다.

## 구현 근거

- [Engine/Utility_Framework/ThreadPool.cpp](../../Engine/Utility_Framework/ThreadPool.cpp)
- [Engine/Utility_Framework/JobScheduler.cpp](../../Engine/Utility_Framework/JobScheduler.cpp)
- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/RenderEngine/AssetDepot/AssetRequest.h](../../Engine/RenderEngine/AssetDepot/AssetRequest.h)
