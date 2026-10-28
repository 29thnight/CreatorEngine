# 락 프리 MPMC 큐 구현·검증 (2026-10-25)

> 후속 갱신: 현재 선택 구현·병목 분리·수용 판정은 [락 프리 큐 최적화 보고서](LockFreeQueueOptimization20261025.md)를 따른다. 아래는 당시 구현의 기록이며 현재 타입의 구조나 성능 판정으로 사용하지 않는다.

`CreatorContainers/include/ce/lock_free_queue.h`에 Michael–Scott 연결 큐와 hazard pointer 회수를 구현했다. 기존 스핀락 `ce::concurrent_queue`와 구분되는 `ce::lock_free_queue` 타입이다. 핵심 연결/제거 알고리즘 구현과 독립 검증을 완료했으나, 기본 할당·회수 경로의 PPL 처리량 목표는 아직 충족하지 않았다. CreatorEngine PPL 별칭과 엔진 소비자는 변경하지 않는다.

## 구조와 보장 범위

- Michael–Scott 방식의 head/tail CAS와 tail helping을 사용한다. 생산자가 next를 연결한 뒤 멈춰도 다른 참여자가 tail을 전진시킨다. 아직 공개되지 않은 예약 슬롯을 후속 소비자가 기다리는 형태의 링을 락 프리라고 표기하지 않는다.
- 동시 활성 호출당 hazard pointer 2개를 사용한다. 참조를 발행한 뒤 원본 포인터를 재검증하고, 순차 일관적 발행/스냅샷으로 제거된 노드를 보호한다. 보호가 사라지기 전에는 재사용·해제하지 않는다.
- `prepare(args...)`: 객체와 노드를 생성하는 단계다. heap 할당·생성자 비용을 포함한다. 실패하면 큐는 변경하지 않는다.
- `publish(prepared&&)`: 준비된 노드를 CAS로 연결한다. 성공 순간에 노드 소유권을 큐로 넘긴다. 정상 경로에서 할당·회수·스핀락·운영체제 대기를 하지 않는다.
- `try_pop(out)`: head CAS 승자만 값을 이동하고 파괴한다. 이전 dummy는 retired 목록에 넣는다. 정상 경로에서 할당·회수를 하지 않는다. 값 이동은 head CAS 뒤에 수행하므로 이동 대입과 소멸자가 noexcept인 T만 지원한다.
- `reclaim()`: retired 목록을 떼고 hazard snapshot과 대조해 안전한 노드를 해제한다. 보호 중인 노드는 다시 retired 목록에 넣는다. 여러 회수자와 생산자/소비자가 함께 실행할 수 있다. allocator 호출의 진행 시간은 보장하지 않는다.
- 편의 `push/emplace`는 prepare와 publish를 합친다. 동적 할당자·T의 연산·C++ 예외 런타임까지 포함해 전체 호출이 무조건 락 프리라고 주장하지 않는다. 핵심 연결/제거 알고리즘의 진행 보장은 원자 primitive와 T 연산이 완료되고 활성 호출 수가 설정 범위 안에 있다는 전제다.

기본 `MaxOperations=64`는 동시에 참여 중인 호출 수의 상한이다. 평생 사용한 스레드 수가 아니다. 빈 hazard record가 없으면 기다리지 않고 `ce::queue_operation_limit`를 던지며 준비된 노드의 소유권을 보존한다. 예외 처리 자체의 런타임 할당 가능성은 정상 경로의 무할당 보장에 포함하지 않는다. 큐 생성은 dummy 노드를 할당한다. clear/소멸은 참여자 join 이후에만 허용한다. prepared handle은 이동 전용이며 버리면 미공개 노드를 해제한다.

reclaim을 호출하지 않으면 제거된 노드가 누적된다. 엔진에 넣을 때는 별도 유지보수/프레임 경계 호출 지점을 정해야 한다. 생성·이동·파괴·사용자 Hook이 동일 큐로 재진입하지 않아야 한다. thread death 후 완전한 자원 회수나 wait-free 완료·스레드별 공정성은 제공하지 않는다.

공개 구현은 STL 컨테이너·std::atomic·mutex·thread·PPL에 의존하지 않는다. `<new>`/언어 런타임과 컴파일러 원자 intrinsic은 사용한다. `detail/atomic_ptr.h`는 순차 일관적 포인터 load/store/CAS를 제공한다. GNU 계열은 포인터 원자가 내부 락으로 폴백하지 않는 타깃만 허용한다. 기본 MSVC x64 백엔드와 ClangCL만 실행 검증했다. 다른 OS, GCC, ARM64는 미검증이다.

## 사용 예

```cpp
#include <ce/lock_free_queue.h>

ce::lock_free_queue<command> queue;
// 할당/객체 생성은 큐의 공개 단계 이전에 수행한다.
auto ready = decltype(queue)::prepare(command_data);
queue.publish(static_cast<decltype(ready)&&>(ready));
command result;
if (queue.try_pop(result)) { /* result 처리 */ }
// 별도 유지보수 경로에서 실행. allocator 지연은 이 단계에 남는다.
queue.reclaim();
// 모든 생산자·소비자·회수자 join 뒤 queue 소멸.
```

현재 실제 ProxyCommand에는 noexcept 이동 대입 선언이 있다. 파일 경로 타입은 독립 테스트에서 확인했다. 이것만으로 엔진 모듈/ABI/리플렉션/종료 수용이 끝났다고 보지는 않는다.

## 정확성 검사와 실행 결과

- MSVC Release CTest 5/5 통과. 기존 배열/문자열, 스핀락 큐, 락 프리 큐와 공개 헤더 독립 실행을 포함한다.
- 락 프리 대조 검사 전체를 Release에서 10회 반복하고, AddressSanitizer에서 5회 반복해 통과했다. ClangCL 20.1.8 Release에서도 동일 검사를 통과했다.
- 검사 1회당 1P1C/4P1C/4P4C를 각각 3회, 총 810,000개 ID의 유실·중복을 검사한다. 1소비자에서는 생산자별 FIFO도 검사한다. 별도로 unique_ptr 소유권 MPMC 40,000개를 대조하고 여러 소비자가 회수를 함께 실행한다.
- 생성 실패, 미공개 handle 파괴/이동, 빈 handle 공개, clear 후 재사용, const empty, 실제 filesystem::path 복사/이동, 128바이트 정렬을 검사한다.
- enqueue 연결 직후 생산자 정지: 다른 스레드의 enqueue 및 두 원소 drain이 끝난 뒤 정지한 생산자를 재개했다.
- hazard 보호 직후 소비자 정지: 다른 소비자가 10,000회 enqueue/dequeue/reclaim을 진행했다. 정지한 소비자의 보호 노드와 현재 dummy만 남았고, 재개 뒤 보호 노드도 회수됐다.
- 활성 record 한도를 고의로 소진하고, 예외 뒤 prepared node의 소유권이 유지되어 다시 공개 가능한지 검사했다.
- 3스레드 × 4연산의 호출/반환 이력 100개를 수집해, 실시간 선후 관계를 지키는 순차 FIFO 설명이 존재하는지 탐색했다. 이는 관측한 작은 이력의 선형화 검사이며 전체 알고리즘의 형식 증명은 아니다.

ASan은 데이터 레이스 검출기가 아니다. ThreadSanitizer, Relacy/CDSChecker, 모든 인터리빙의 모델 검사, 실제 엔진 프레임/종료 수용은 수행하지 않았다. 중단 검사는 특정 두 경로의 진행 증거이며 모든 가능한 장애의 증명으로 확대하지 않는다.

## PPL 직접 비교

Windows 11 Pro 10.0.26300, i7-8700K 6C/12T, MSVC 19.50.35730, x64 Release /O2 /MD, iterator debug 0. 생산자당 16,000개, 8B/128B payload, 워밍업 2회, 표본 9개, A/B 순서 교대, 프로세스 3회. 표의 값은 프로세스별 중앙값 비율 3개의 중앙값이다. 1보다 작으면 자체 큐가 빠르다.

자체 경로는 노드별 할당을 포함한 push와 소비자별 성공 pop 64회마다 수행하는 concurrent reclaim을 포함한다. PPL은 기본 allocator/페이지 정책이다. 스레드 생성은 제외하고 시작 신호·데이터 전달·join은 포함한다. 양쪽 큐 소멸과 마지막 잔여 노드 회수는 타이머 밖이다. 할당 없는 publish만 측정한 결과로 PPL 전체 경로와 비교하지 않았다.

| 값 크기 | 생산자/소비자 | custom/PPL 중앙값 | 프로세스별 비율 범위 | 판정 |
|---:|---|---:|---|---|
| 8B | 1/1 | 2.377 | 2.163~2.421 | 목표 미달 |
| 8B | 4/1 | 4.136 | 3.913~4.302 | 목표 미달 |
| 8B | 4/4 | 2.057 | 1.986~2.241 | 목표 미달 |
| 128B | 1/1 | 1.239 | 1.081~1.243 | 목표 미달 |
| 128B | 4/1 | 2.042 | 1.916~2.094 | 목표 미달 |
| 128B | 4/4 | 1.994 | 1.909~1.998 | 목표 미달 |

대부분 경로에서 동급 기준(custom/PPL ≤ 1.10)을 넘는다. 일부 표본의 변동성도 남아 있다. 단일 스핀락 큐에서 나왔던 처리량 우위를 락 프리 구현의 결과로 가져오지 않는다. 노드 단위 할당·회수·hazard 발행·CAS는 소스상 비용 후보지만 각 비용의 실측 기여율은 아직 분해하지 않았다. 다음 성능 단계는 이 비용의 분리 계측, 준비된 노드 전달 경로, allocator/회수 정책의 A/B다. 안전한 재사용을 입증하기 전에는 단순 freelist로 노드를 재순환시키지 않는다.

실행 중 다른 프로그램을 종료하거나 전원 정책/affinity를 바꾸지 않았다. 콜드 캐시·RSS·꼬리 지연·과부하·OS 간 일관성·실제 엔진 프레임 성능은 미검증이다. 엔진 통합은 보류한다.

## 재현과 근거

- 독립 프로젝트: `C:/Users/idene/source/repos/CreatorContainers`
- 재현: `./run-lock-free-tests.ps1`
- 소스: `include/ce/lock_free_queue.h`, `include/ce/detail/atomic_ptr.h`, `tests/lock_free.cpp`
- 결과: `results/lock-free-run1.csv`~`lock-free-run3.csv`, `lock-free-ctest.txt`, `lock-free-repeat.txt`, `lock-free-asan.txt`, `lock-free-clang.txt`
- 환경/소스/실행 파일 해시: `results/lock-free-environment.json`
- 공개 헤더 단독 실행의 DLL 의존: `results/lock-free-header-dependencies.txt`. PPL/ConcRT 및 MSVC STL DLL 의존 없음. C/C++와 OS 런타임은 남는다.
- 알고리즘 원전: [Michael–Scott 설명과 의사 코드](https://ftp.cs.rochester.edu/research/synchronization/pseudocode/queues.html), [Michael의 hazard pointer 연구](https://research.ibm.com/publications/safe-memory-reclamation-for-dynamic-lock-free-objects-using-atomic-reads-and-writes). 논문 아이디어를 토대로 작성했으며 외부 큐 구현 코드를 복사하지 않았다.
- [기존 스핀락 큐/PPL 비교](PPLQueueReplacement20261025.md), [공용 설계와 통합 게이트](../design/ContainerLibraryDesign.md).
