# JobSystem 타당성 분석의 현재 결론

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 과거 도입 질문에 대한 현재 답

공용 JobSystem을 새로 선택할 단계는 지났다. 현재 source는 enkiTS 기반 persistent workers, host dispatcher, dependency completion을 가진 job_scheduler를 사용한다. 따라서 과거 Core.ThreadPool 완료 이벤트 경쟁이나 HIGHEST priority 워커 수를 지금 존재하는 결함으로 인용하면 안 된다.

현재 평가해야 할 것은 API의 비용·정확성이다. scheduler의 waiting graph는 worker를 점유하지 않고, incomplete worker wait를 거부한다. dependency 실패가 dependent 실행을 막고, task/observer capture가 해제된 후 완료를 공개한다. 이는 AssetDepot과 scene 준비가 요구하는 수명 경계다.

## 비용이 발생하는 자리

일반 job_group은 함수 목록을 갖고, indexed batch는 callback 하나로 항목들을 처리한다. pool은 batch당 dispatcher handoff를 사용한다. 작은 task를 무조건 세분하면 dispatch·lock·capture 비용이 계산보다 커질 수 있다. 반대로 긴 decode/compile과 frame-critical 계산을 공유할 때 tail latency가 생길 수 있다. 두 가설 모두 현재 workload 측정 없이 수치화할 수 없다.

## 후속 판정 방법

같은 장면·자산·worker 수로 serial, coarse batch, indexed/chunk 형태를 비교한다. frame p95/p99, worker busy/queue delay, owner join 시간, CPU 이용률과 실패/취소 수명을 함께 본다. 단순 총 처리량만으로 renderer·animation의 응답성을 판정하지 않는다. 전용 render/submit loop를 합치는 문제는 별도 책임이다.

구현 정본은 [JobSchedulerDesign](../design/JobSchedulerDesign.md), 이관 범위는 [TaskSchedulerUnificationPlan](../plans/TaskSchedulerUnificationPlan.md)이다. 옛 실험 수치는 역사적 배경이며 이번 재작성은 재측정이 아니다.

## 구현 근거

- [Engine/Utility_Framework/ThreadPool.cpp](../../Engine/Utility_Framework/ThreadPool.cpp)
- [Engine/Utility_Framework/JobScheduler.cpp](../../Engine/Utility_Framework/JobScheduler.cpp)
- [Engine/Utility_Framework/JobScheduler.h](../../Engine/Utility_Framework/JobScheduler.h)
- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
