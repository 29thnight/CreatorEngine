# 공용 thread_pool과 job_scheduler 계약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 소유권과 진입점

`ce::get_thread_pool()`은 영속 enkiTS worker를 제공하고 `ce::get_job_scheduler()`가 그 pool의 독점 제출·lifecycle owner다. 소비자는 pool에 직접 task를 넣지 않고 scheduler를 사용한다. pool은 scheduler보다 오래 살아야 한다. EngineBootstrap이 profiler와 worker hooks를 설치한 다음 scheduler를 시작한다.

thread_pool은 host의 제출을 전용 dispatcher에 handoff한다. dispatcher는 enkiTS 외부 thread로 등록되어 pinned handoff만 수행한다. host가 제출할 때 사용자 callback을 직접 실행하지 않도록 만든 경계다. 이것이 모든 completion observer까지 항상 worker에서 실행된다는 뜻은 아니다.

## API

- `job_group::add`: single-producer batch 구성. submit은 group을 소비하고 봉인한다
- `submit`: 독립 batch 또는 callback 제출
- `submit_after`: 같은 scheduler의 유효 dependency들 뒤에 제출. dependency 대기 동안 worker를 점유하지 않는다
- `submit_indexed`: 하나의 indexed callback을 공유하며 index별 함수 벡터를 만들지 않는다
- `parallel_for`: count/grain으로 범위를 나눈다. capture한 저장소는 완료까지 살아 있어야 한다
- `job_handle`: 복사 가능한 completion token. drop은 취소도 join도 아니다

기본/빈 handle은 `is_complete()`가 true지만 `valid()`는 false다. 이를 dependency로 넣으면 거부된다. 다른 scheduler의 handle도 거부된다.

## 오류와 완료의 정확한 의미

worker callback 예외는 completion error로 보존된다. prerequisite 실패 시 dependent body는 실행하지 않고 오류를 전파한다. 긴 실패/빈 dependency chain은 반복 notification pump로 전파하여 재귀 stack 증가를 피한다.

`job_group::on_complete`는 수락된 work의 terminal observer다. dependency/dispatch 실패도 받으며 task capture를 해제한 뒤, token readiness를 공개하기 전에 호출된다. observer capture도 readiness 전에 해제된다. submit_after 중 inline 호출될 수 있으므로 worker 전용 API나 caller가 잡은 lock을 재획득하는 observer를 무심코 두면 안 된다.

`wait()`는 완료 뒤 해당 job의 exception을 다시 던진다. pool worker에서 미완료 handle을 기다리면 logic_error를 낸다. worker는 blocking wait 대신 dependency를 표현한다. 이 검사는 임의 외부 mutex/파일 I/O의 교착까지 탐지하지 않는다.

## 종료와 소비자 경계

shutdown은 admission 중단 → 수락 graph drain → worker join 순서다. worker가 shutdown을 호출하면 terminate한다. lifecycle은 engine owner가 직렬화한다. EngineBootstrap 종료에서는 SceneManager를 정리하고 scheduler를 drain한 뒤 DataSystem 등 나머지 서비스를 해제한다.

CPU asset 준비, scene 문서 parse, animation chunk, PSO compile, backend command recording이 같은 실행 기반을 사용할 수 있다. 그러나 장기 Render/Presentation/RHI 제출/IPC loop와 GPU fence completion은 job 완료와 다른 소유권이다. scheduler 통합이 이 루프를 범용 worker로 대체한다는 뜻은 아니다.

## 재검증할 항목

단일 worker의 의존 chain, exception·dispatch failure, observer 재진입, shutdown 중 제출, capture destructor와 readiness 순서, 큰 indexed batch를 검증한다. 이 revision에서 실행하지 않았으므로 과거 pool microbenchmark 수치를 현재 성능 개선으로 인용하지 않는다.

## 구현 근거

- [Engine/Utility_Framework/ThreadPool.h](../../Engine/Utility_Framework/ThreadPool.h)
- [Engine/Utility_Framework/ThreadPool.cpp](../../Engine/Utility_Framework/ThreadPool.cpp)
- [Engine/Utility_Framework/JobScheduler.h](../../Engine/Utility_Framework/JobScheduler.h)
- [Engine/Utility_Framework/JobScheduler.cpp](../../Engine/Utility_Framework/JobScheduler.cpp)
- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
