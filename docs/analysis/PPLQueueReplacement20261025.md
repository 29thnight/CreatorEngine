# PPL 큐 대체 후보 구현·실측 (2026-10-25)

> 후속 갱신: 현재 선택 구현·병목 분리·수용 판정은 [락 프리 큐 최적화 보고서](LockFreeQueueOptimization20261025.md)를 따른다. 아래는 당시 구현의 기록이며 현재 타입의 구조나 성능 판정으로 사용하지 않는다.

CreatorEngine의 현재 PPL 컨테이너 소비자에 필요한 자체 큐 2종을 독립 프로젝트에 구현했다. 엔진의 기존 PPL 별칭과 모듈은 변경하지 않았다. 마이크로벤치 처리량과 엔진 수용, 락 프리 진행 보장은 서로 다른 판정이다.

## 실제 소비자와 구현

- `Engine/RenderEngine/ProxyCommandQueue.h`: `ce::concurrent_queue<ProxyCommand>`, 여러 생산자에서 push, CapturePending에서 try_pop. 명령이 arena/owner를 보유하므로 비단순 객체 이동·파괴가 필요하다.
- `Editor/EngineEntry/EditorAssetPresentation.h/.cpp`: `ce::concurrent_queue<file::path>`, const 참조 push, try_pop, empty, 종료 시 drain.
- 별칭은 `Engine/Utility_Framework/ConcurrentQueue.h`와 `Engine/Contracts/Creator.Concurrency.ixx`에 있다. concurrent_vector 등은 이번 소스 검색에서 실제 PPL 컨테이너 소비자로 확인되지 않았다. 과거 분석 문서의 사용처 수를 현재 소스에 그대로 적용하지 않는다.

공개 구현은 독립 `include/ce/concurrent_queue.h`에 있다.

| 타입 | 계약 |
|---|---|
| `ce::concurrent_queue<T,64,4>` | 자동 확장 MPMC FIFO. 블록당 64개, 소비 블록 최대 4개 재사용. push(const T&), push(T&&), emplace, try_pop, empty, size, unsafe_size, clear. 전역 스핀락으로 직렬화. 락 프리 아님 |
| `ce::bounded_spsc_ring<T,Capacity>` | 생산자 1개·소비자 1개 전용. 2의 거듭제곱 용량, 모든 슬롯 사용, 실패 시 bool. 생성 후 할당 없음. 생산자/소비자가 상대의 슬롯 예약 완료를 기다리는 락이 없음. 고정 연산 경로의 진행 보장은 T 연산과 원자 primitive가 완료된다는 전제 |

두 타입 모두 단순 타입 제한이 없고 비기본생성·이동 전용 객체와 정렬된 타입을 저장한다. T의 소멸자는 noexcept여야 한다. 큐는 copy/move를 제공하지 않는다. `try_pop`의 이동 대입이 예외를 던지면 원소 수와 슬롯 소속을 유지하지만 원본·목적지 값은 T의 예외 보장에 따른다. SPSC에서 full 반환 시 입력을 이동하지 않는다.

MPMC의 객체 생성·이동·파괴는 락 안에서 이루어지므로 동일 큐로 재진입하면 안 된다. 락 소유 스레드가 멈추면 다른 작업도 지연되며 공정성·최대 지연은 보장하지 않는다. clear는 동기화하지만 생산자가 그 직후 다시 push할 수 있으므로 shutdown 프로토콜을 대신하지 않는다. 소멸은 참여 스레드 join 후에만 허용한다. SPSC의 clear/reset 및 동시 iterator는 제공하지 않는다.

## PPL의 락 프리 여부

현재 비교한 MSVC 14.50.35717 설치 헤더의 `concurrent_queue.h:145`에는 `A queue using simple locking` 주석이 있고, `:159`에는 `_Micro_queue::_Page_mutex_flag`가 있다. 이 설치본을 모든 경로에서 락 프리인 구현이라고 가정할 근거는 없다. 다만 내부 구현 일부는 ConcRT 라이브러리에 있어 헤더만으로 경합 빈도나 전체 경로 비용을 판단하지 않는다. 이 증거가 자체 단일 스핀락 큐와 PPL의 내부 구조가 같다는 뜻도 아니다.

Microsoft의 [concurrent_queue 공식 계약](https://learn.microsoft.com/en-us/cpp/parallel/concrt/reference/concurrent-queue-class?view=msvc-170)은 push/try_pop/empty의 동시 호출 안전성과 clear/unsafe_size/반복자의 제한을 설명한다. [병렬 컨테이너 문서](https://learn.microsoft.com/en-us/cpp/parallel/concrt/parallel-containers-and-objects?view=msvc-170)의 iterator 설명도 함께 따른다. 빈 큐에서 try_pop이 false를 반환하는 API와 lock-free 알고리즘의 시스템 전체 진행 보장은 구분한다. CAS를 사용하거나 sleep을 하지 않는 것만으로 lock-free가 되지는 않는다.

로컬 증거는 `results/ppl-contract-evidence.txt`에 헤더 위치·해시·해당 부분을 기록했다. SDK 코드를 복사해 구현하지 않았다.

## 의존성과 원자 연산

공개 큐 헤더는 STL 컨테이너·문자열·mutex·atomic·thread·PPL·Windows SDK API를 사용하지 않는다. C/C++ 언어 런타임과 `<new>`, 컴파일러 intrinsic은 사용한다. 원자 연산은 `include/ce/detail/atomic.h`에 모았다. x86/x64 MSVC는 정렬된 32비트 intrinsic load/store와 컴파일러 acquire/release 장벽, exchange는 Interlocked를 쓴다. 다른 MSVC 타깃은 보수적 Interlocked 경로, GCC/Clang 계열은 `__atomic` 경로를 둔다. Windows x64 외 백엔드는 실행 미검증이다.

`queue_header_probe`는 큐 헤더 하나만 포함하고 STL/PPL 헤더 없이 이동 전용 객체를 넣고 꺼내는 실행 파일이다. `results/queue-header-dependencies.txt`에서 PPL/ConcRT 및 MSVC STL DLL 의존이 없음을 확인했다. C/C++ 런타임·OS 런타임까지 없다는 뜻은 아니다.

## 정확성 검증

- MSVC Release CTest 3/3 통과: 기존 컨테이너, 동시성 큐, 공개 헤더 독립 실행.
- MPMC 1P1C/4P1C/4P4C를 각 3회, 생산자당 30,000개. 총 810,000개 원소를 ID별 정확히 한 번 소비했는지 검사하고 1소비자에서는 생산자별 FIFO도 검사했다.
- 이동 전용 owner MPSC 80,000개, SPSC 200,000개의 생성·이동·소비를 확인했다.
- 블록 경계, 재사용, full/empty, clear 후 재사용, 잔존 원소 파괴, 128바이트 정렬, 파일 경로 문자열, 생성·이동 대입 예외 후 재사용을 검사했다.
- MSVC Release AddressSanitizer 및 ClangCL 20.1.8 Release에서 같은 검사를 통과했다. ASan은 데이터 레이스 검출기가 아니다. ThreadSanitizer·모델 검사·실제 엔진 종료 및 씬 전환 검증은 수행하지 않았다. 32비트 SPSC 시퀀스의 정수 전체 wrap은 실험하지 않았으며 용량 제한과 unsigned 연산에 의한 설계 검토 범위다.

## PPL 직접 비교

Windows 11 Pro 10.0.26300, i7-8700K 6C/12T, MSVC 19.50.35730, x64 Release /O2 /MD, iterator debug 0. 기존 스레드의 ready 이후에 타이머를 시작하므로 스레드 생성은 제외하고 시작 신호·데이터 전달·join 시간은 포함한다. 생산자당 16,000개, payload 8B/128B, warmup 2회, 9개 표본, A/B 순서 교대, 프로세스 3회. 아래는 프로세스 중앙값 비율 3개의 중앙값이다. 비율 1 미만이 자체 큐가 빠르다는 뜻이다.

| 큐 | 값 크기 | 생산자/소비자 | custom/PPL 중앙값 | 프로세스별 비율 범위 | 표본 편차 경고 |
|---|---:|---|---:|---|---|
| mpmc | 8B | 1/1 | 0.350 | 0.293~0.369 | 있음 |
| mpmc | 8B | 4/1 | 0.538 | 0.511~0.540 | 있음 |
| mpmc | 8B | 4/4 | 0.312 | 0.272~0.316 | 있음 |
| mpmc | 128B | 1/1 | 0.544 | 0.414~0.559 | 있음 |
| mpmc | 128B | 4/1 | 0.912 | 0.794~0.979 | 있음 |
| mpmc | 128B | 4/4 | 0.616 | 0.550~0.631 | 있음 |
| spsc | 8B | 1/1 | 0.205 | 0.171~0.216 | 있음 |
| spsc | 128B | 1/1 | 0.221 | 0.209~0.247 | 있음 |

표본 편차 경고는 어느 프로세스에서든 대략 p90/p10이 어느 쪽이든 1.25를 넘었음을 뜻한다. 다른 프로그램은 실행 중이고 전원 정책·CPU affinity를 바꾸지 않았다. 동일 시각에 시작하는 연속 전송 처리량이며 저빈도 요청·혼합 프레임 부하·최대 지연·락 소유자 중단을 대변하지 않는다. 큰 값의 나머지 바이트도 생산자가 채우지만 소비 체크섬은 첫 워드를 사용한다. 메모리 사용량 동등 비교는 수행하지 않았다.

SPSC는 제한 용량의 역할 고정 큐이므로 자동 확장 MPMC인 PPL과 대체 가능 조건이 다르다. 처리량 우위만으로 PPL 전체 API나 진행 보장을 대체했다고 표현하지 않는다. MPMC도 엔진 명령의 재진입 여부·소유권·예외·프레임 시간·꼬리 지연을 통과해야 실제 통합한다. 현재는 독립 구현과 정확성·마이크로벤치 단계까지 완료했다.

재실행: `C:/Users/idene/source/repos/CreatorContainers/run-concurrent-tests.ps1`. 원시 표본은 `results/concurrent-run1.csv`~`concurrent-run3.csv`, 정확성·ASan·Clang 로그와 환경/소스/실행파일 해시는 같은 results 디렉터리의 `concurrent-*` 파일이다. 최종 통합 기준은 [공용 설계](../design/ContainerLibraryDesign.md)를 따른다.
