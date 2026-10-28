# 고정 용량 MPSC·배열 확장·측정 편차 — 2026-10-26

> **후속 수용 완료:** 아래 81/82는 SMT 상태 제어 전의 이력이다. 같은 바이너리의 전체 재검증과 원인 분리는 [STL 소형 배열 편차 후속 보고서](SmallArrayVarianceAcceptance20261026.md)를 참조한다.

**고정 용량 MPSC와 대형 배열의 지정된 독립 성능 조건은 충족했다. 일반 라이브러리 전체의 엄격한 편차 게이트는 81/82로 보류한다. 엔진 통합은 수행하지 않았다.**

| 범위 | 결과 | 판정의 의미 |
|---|---:|---|
| 고정 MPSC | 4/4 | 용량 64/1024 × 1P1C/4P1C, 기존 yield 정책의 8회 전송 묶음, 3프로세스 모두 충족 |
| 대형 배열 | 4/4 | N=4096/65536 × 성장/reserve, 조건별 새 프로세스에서 각 3회 충족 |
| 일반 컨테이너 시간 비율 | 82/82 | 매 조건·반복 custom/MSVC STL 중앙값 ≤1.10 |
| 일반 컨테이너 자체 표본 편차 | 82/82 | 매 조건·반복 자체 x[13]/x[1] ≤1.30 |
| 일반 컨테이너 양쪽 편차까지 포함 | 81/82 | STL의 array_growth/64 1건이 초과하므로 전체 수용으로 처리하지 않음 |

편차는 정렬한 15개 표본의 x[13]/x[1]이다. 세 반복의 평균으로 느린 조건을 숨기지 않고, 모든 반복의 비율과 양쪽 편차가 한도 안이어야 해당 조건을 수용한다. 표본 제거·실패 표본 교체·한도 완화는 하지 않았다. 이는 명시한 warm-throughput 조건의 결과이며 단일 전송 지연, 엔진 프레임 p99 또는 OS 간 동등성을 입증하지 않는다.

## 환경과 독립 위치

독립 소스/솔루션은 `C:/Users/idene/source/repos/CreatorContainers`, `build/CreatorContainers.slnx`다. 엔진 헤더·패키지를 가져오지 않는다. MSVC 명령의 주변 vcpkg include 경로는 남아 있었지만, 실제 CL.read 의존 파일에 vcpkg 경로는 없었다. 배포 헤더는 STL 컨테이너·문자열·std::atomic·std::mutex에 의존하지 않으며 C 메모리 함수, C/C++ 런타임과 컴파일러 원자 primitive를 사용한다.
환경: Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz, 6코어/12논리 CPU, Microsoft Windows 11 Pro 10.0.26300. MSVC 19.50.35730, STL 145, Release /O2 /Ob2, /W4 /WX, CMake cxx_std_23(/std:c++latest), iterator debug 0.

측정 스레드는 논리 CPU4, above-normal 우선순위이며 MPSC 생산자는 소비자와 다른 물리 코어다. 다른 앱을 종료하거나 OS 전원·CPU 정책을 바꾸지 않았다. 이 PC의 실제 실행 자료이며 GCC/macOS/ARM64 및 OS 간 성능 비교는 미검증이다.

## 고정 용량 MPSC 계약과 구현

[bounded_mpsc_queue.h](../../../CreatorContainers/include/ce/bounded_mpsc_queue.h)는 고정 값 풀 N개와 64비트 epoch/index publication ring N개를 직접 보유한다. 코어에 힙 할당·락·TLS·참여자 등록은 없다. 소비자는 정확히 한 명이어야 한다.

생산자는 빈 값 슬롯을 소유하고 T를 생성한 뒤 FAA ticket을 얻어 공개한다. 소비자는 아직 공개되지 않은 빈 예약을 취소하고 다음 epoch로 전진한다. 지연된 생산자는 이미 만든 값을 유지한 채 새 ticket에서 재공개한다. 예약 생산자의 재개를 기다리지 않는다. 실패 CAS는 소비자의 진행을 뜻하고, 풀 탐색은 고정 크기 내에서 종료한다. consumer는 시작 시 tail snapshot까지만 탐색한다. FIFO/생산자별 순서를 검사했으며 생산자의 wait-free 보장은 아니다.

`false/unavailable`은 공개 FIFO의 strict full 판정이 아니다. 비공개 예약이 용량을 쓰거나 경합 중 풀 탐색을 놓친 경우도 포함한다. false 반환 시 인수를 이동하지 않는다. T 생성자 예외는 ticket 전에 정리하며 이동 대입/소멸은 noexcept를 요구한다. 사용자 T와 예외 런타임의 진행 보장은 코어와 별도다. 유한 counter 소진은 명시적 예외이며 조용히 wrap하지 않는다. 생성/소멸에는 동시 접근이 없어야 한다. 잔존 값은 풀 순서로 소멸하므로 FIFO 소멸이 필요하면 먼저 drain한다.

### PPL 비교의 최종 조건

값은 128B이고 **각 전송의 생산자별 원소 수는 기존과 같은 262,144개**다. PPL은 기본 무제한 큐, 자체 큐는 고정 용량이다. 같은 메모리 상한 비교는 아니다. ready barrier 뒤 start 공개부터 join까지 측정하며 모든 단어·생산자별 FIFO를 검증한다. 큐 생성/소멸 및 스레드 생성은 이 전송 시간 밖이다.

최종 사전 지정 조건 `yield-batch8`은 기존 OS yield 대기 정책을 유지하고, 8개 전송 시간의 합/8을 1표본으로 삼는다. 3개 고정 워밍업 묶음과 정식 15개 묶음을 사용한다. 이는 연속 단일 wall interval이 아닌 동일 작업량의 평균 전송 시간이다. 전체 대조 행렬의 정식 하위 전송 **3,420쌍**을 보존·재계산했다. 단일 전송의 지연 편차를 해결했다는 뜻은 아니다.

| 용량 | 생산자 | 중앙 비율 | 세 프로세스 비율 범위 | 최대 양쪽 표본 편차 | 수용 |
|---|---:|---:|---:|---:|---|
| 64 | 1 | 0.680 | 0.657–0.694 | 1.077 | True |
| 64 | 4 | 0.878 | 0.839–0.895 | 1.270 | True |
| 1024 | 1 | 0.686 | 0.668–0.697 | 1.094 | True |
| 1024 | 4 | 0.796 | 0.794–0.804 | 1.060 | True |

### 대기 정책과 짧은 표본의 분리

이전 단일 전송 검사에서는 자체 1P/64 편차 1.379가 초과했다. 새 계측기는 같은 바이너리 안에서 (1) 시작/재시도 모두 yield, (2) 시작만 pause32, (3) 시작/재시도 모두 pause32, (4) yield 8회 묶음, (5) pause32 8회 묶음을 모두 3회 대조했다. 반복 2는 모드 순서를 반전했다. 개별 시간뿐 아니라 외부 push 실패·empty 재시도 및 start 공개부터 작업자 대기 종료까지의 지연을 기록했다.

이번 계측기의 단일 전송 대조군에서도 이전 편차 초과가 그대로 재현되지는 않았다. 따라서 배치 길이 또는 OS yield 하나만을 유일한 원인으로 확정하지 않는다. 1P/64의 시작 지연 중앙값은 대체로 0.5~0.9µs여서, 중앙값 수준의 전체 전송 시간 차이를 시작 지연만으로 설명할 근거는 부족하다. hot retry 정책을 바꾸면 empty 시도 수와 시간 분포가 변했지만, backoff 길이·시스템 호출·큐 경합을 순수 비용으로 분해한 계측은 아니다. 외부 재시도 횟수를 PPL 내부 원자 연산/대기 횟수로 해석하지 않는다. busy polling 대조군은 엔진 CPU 전력·수면 정책 수용이 아니다.

## 대형 배열 확장

기존 1.5배 성장 정책을 유지했다. [dynamic_array.h](../../../CreatorContainers/include/ce/dynamic_array.h)는 trivial/noexcept-move 값과 구체 allocator의 명시적 `byte_relocation_allocator` 자기 타입 opt-in에만 재할당 최적화를 적용한다. 상속만 한 allocator가 우연히 CRT realloc을 호출해 소유권을 깨지 않게 한다. 자기 참조 append 인수는 재할당 전에 보존한다. 비단순 객체·opt-in 없는 allocator는 기존 생성/이동·복사/파괴 경로를 유지한다.

일반 정렬은 malloc/realloc/free, 오버얼라인은 정렬 new/delete로 확보·복사한다. MSVC x64의 CRT malloc 정렬 한도 16B와 그 밖의 alignof(max_align_t)를 사용하며 컴파일러 new-alignment 옵션과 구분한다. [Microsoft malloc 계약](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/malloc?view=msvc-170)을 참조했다. 일반 heap은 전역 operator new hook을 경유하지 않으므로 엔진 계측은 명시적 resource를 사용해야 한다. 원본 주소와 같아 보여도 확장의 참조 무효화 계약은 유지한다. 모든 소비자를 같은 헤더로 재빌드해야 한다.

아래 ns는 **N개 배열을 16번 생성·채움·합산·파괴하는 1회 작업 묶음**이다. 단일 원소 ns가 아니다. 각 조건은 새 프로세스에서 3회 측정했다.

| 작업, N | 중앙 비율 | 세 반복 비율 범위 | 자체 중앙값 ns 범위 | STL 중앙값 ns 범위 | 최대 양쪽 표본 편차 |
|---|---:|---:|---:|---:|---:|
| core, array_growth, 4096 | 0.743 | 0.462–0.837 | 102289–116301 | 138464–221168 | 1.014 |
| core, array_growth, 65536 | 0.188 | 0.145–0.193 | 1237406–1630881 | 8437444–8620675 | 1.045 |
| core, array_reserved, 4096 | 0.764 | 0.761–0.776 | 70010–71258 | 91694–91963 | 1.039 |
| core, array_reserved, 65536 | 0.804 | 0.800–0.807 | 1172431–1183028 | 1465227–1470286 | 1.018 |

### 할당·이동·페이지 폴트의 별도 계측

대조 profile은 현재 CRT backend에서 재할당 경로만 제외한 before와 최적화 after다. 전체 이전 버전의 모든 차이를 뜻하지 않는다. 시간은 단일 배열의 별도 계측이므로 위 16배열 작업과 직접 빼지 않는다. growth는 allocator/이동/중간 해제를 포함하는 포괄 시간이며 표의 시간을 합하면 안 된다. memcpy만의 순수 시간을 측정했다고 주장하지 않는다.

| 경로 | reserve | 전체 ns 중앙값 | 확장 ns | allocator ns | free ns |
|---|---:|---:|---:|---:|---:|
| after/custom | 0 | 218400 | 21500 | 20300 | 2000 |
| after/custom | 1 | 69600 | 0 | 200 | 300 |
| after/stl | 0 | 624100 | 407700 | 30300 | 117700 |
| after/stl | 1 | 94500 | 0 | 200 | 300 |
| before/custom | 0 | 583800 | 417700 | 24200 | 155100 |
| before/custom | 1 | 69700 | 0 | 200 | 300 |
| before/stl | 0 | 666200 | 464100 | 25400 | 154800 |
| before/stl | 1 | 101000 | 0 | 200 | 300 |

| 경로 | reserve | 호출수 | 재할당수 | 주소 변경수 | 논리 재배치 B | 주소 변경시 old capacity B | 페이지 폴트 |
|---|---:|---:|---:|---:|---:|---:|---:|
| after/custom | 0 | 23 | 23 | 3 | 1137832 | 472 | 93 |
| after/custom | 1 | 1 | 1 | 0 | 0 | 0 | 0 |
| after/stl | 0 | 29 | 0 | 0 | 1474808 | 0 | 307 |
| after/stl | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| before/custom | 0 | 23 | 0 | 0 | 1137832 | 0 | 282 |
| before/custom | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| before/stl | 0 | 29 | 0 | 0 | 1474808 | 0 | 381 |
| before/stl | 1 | 1 | 0 | 0 | 0 | 0 | 0 |

배열 65,536개에서 자체 성장 횟수 23회와 논리 재배치 요구량 1,137,832B는 유지된다. 주소 변경 횟수는 실제 realloc 결과다. 주소 변경시 old capacity는 보존 대상량이지 물리 DRAM 전송량이 아니다. 재할당 경로는 대부분 기존 저장소를 늘려 쓰지만 CRT 내부 동작과 메모리 상태의 영향은 남는다. 프로세스 간 절대 중앙값 차이를 프로세스 내부 15표본 편차와 혼동하지 않는다.

## 안정된 STL 기준에서 드러난 맵 경로

map_miss/4096은 편차가 작아진 중간 검사에서 약 1.26~1.31배로 실패했다. 해시 상위 7비트 태그만 추가하면 별도 probe의 equality 호출이 192,000→1,664회로 줄었지만 자체 시간은 1.449ms→1.687ms로 악화했다. 태그만으로 해결했다고 판정하지 않았다.

최종 [flat_hash_map.h](../../../CreatorContainers/include/ce/flat_hash_map.h)는 8개 제어 바이트를 읽어 SWAR로 태그/empty를 찾고 첫 empty 이전의 후보만 비교한다. wrap용 첫 8바이트 복제본, home 슬롯 hit 경로와 capacity≤128 scalar 경로를 함께 사용한다. 같은 분리 probe는 약 0.666ms까지 내려갔다. 이 fixture는 메인 전체 RNG와 다르므로 전체 판정은 아래 표를 따른다. wrap·tombstone·clear·copy·rehash 회귀와 sanitizer를 검사했고 실패한 scalar 후보도 보존했다. 문자열은 양쪽의 실제 복사 생성자를 같은 방식으로 호출하고 pointer+length 생성은 별도 항목으로 나눴다.

## 측정 편차에서 확인한 것과 남은 것

- cold 단일 호출로 반복 수를 정하면 실제 배치가 목표보다 짧아졌다. 현재는 warmed batch를 구현별 100ms 목표로 보정하고 양쪽 반복 수를 기록해 1회 작업당 시간으로 환산한다. 느린 구현의 배치가 속도 비율만큼 수초로 늘어나지 않게 했다.
- 통계 벡터 4개를 표본 전에 reserve(15)하여 표본 사이 재할당을 제거했다. 작은 layout probe는 reserve 유무 모두 통과했으므로 이것만을 편차의 유일한 원인으로 확정하지 않는다.
- 구현별 고정 1초 워밍업, grouped A/B 및 두 번째 반복의 B/A 반전, affinity/우선순위, 자기 프로세스 힙 캐시 정리를 각각 기록했다. [HeapOptimizeResources](https://learn.microsoft.com/windows/win32/api/heapapi/nf-heapapi-heapsetinformation)는 자기 프로세스의 사용하지 않는 힙 캐시를 정리한다. LFH 강제 활성화는 최종 조건에 포함하지 않았다.
- 일반 최종 검사는 82개 조건 각각을 새 프로세스에서 3회, 총 246개 프로세스로 실행했다. 입력 생성은 유지하고 벤치마크 호출만 선택했다. 두 번째 반복은 조건 순서도 반전했다. 원시 자료/바이너리는 이전 연속 검사와 따로 보존했다.
- Windows GetThreadTimes는 약 15.625ms 단위로 관측되어 짧은 표본의 정밀 CPU 점유율로 쓰지 않았다. thread cycle이 시간 변화와 같이 변한 사례가 있어 전부를 선점으로 처리하지 않았지만, cycle을 정확한 CPU 시간·주파수로 환산하지도 않았다.

연속 전체 행렬 final4는 일반 81/82, MPSC 4/4였고 STL map_miss/65536 편차 1.354가 남았다. 같은 바이너리의 해당 조건 단독 3회는 통과했다. 이후 일반 82개 전체를 조건별 격리한 final5에서도 STL array_growth/64 편차 1.409가 초과했다. 따라서 여러 작업의 실행 이력만이 원인이라고 결론낼 수 없다. 힙/주소 배치·워밍업·OS·SMT·주파수·IRQ를 완전히 분해했다는 주장도 하지 않는다. 실패를 OS 노이즈라며 무시하지 않았고 일반 전체 수용은 보류한다.

### 남은 엄격한 품질 게이트

| 조건 | 최악 시간 비율 | 최대 양쪽 표본 편차 | 상태 |
|---|---:|---:|---|
| core, array_growth, 64 | 0.604 | 1.409 | STL 편차 초과, 보류 |

## 정확성·진행 보장 검증

MSVC 전체 CTest 10/10과 MPSC 추가 20회, MSVC ASan·ClangCL의 일반/확장/MPSC/Windows 헤더 조합/정렬 검사를 통과했다. Linux Release와 ASan+UBSan은 일반/확장/MPSC/정렬을 각 5회, MPSC TSan은 5회 통과했다. 정렬은 기본 및 -fnew-alignment=64 대조를 포함한다. 공개 상위 헤더는 각각 독립 컴파일했다.

고정 MPSC는 용량 1/7/256, 4/8생산자 유일성·FIFO 스트레스, 100개 짧은 전역 FIFO 이력 선형화 탐색, 이동 전용 값, 생성 예외, 유한 counter 소진을 검사했다. 풀 소유 직후/ticket 직후/공개 직전/공개 직후에 생산자 하나를 멈춘 채 각각 10,000회 전송을 완료했다. 실행 검사는 모든 가능한 실행에 대한 형식 증명은 아니다.

sanitizer 로그는 final2에 보존한다. 측정 helper를 바꾼 final4에서 MSVC CTest 10/10을 다시 실행했고, final5는 동일한 바이너리를 사용했다. 포함 헤더 22개가 검사본과 일치함을 확인했다. 이후 MPSC sampler는 검사 코드만 바꿨다. 최종 generic 두 실행 파일은 final5와 같고, 이전 MPSC 실행 파일/소스는 baseline/20261026-measurement-stage5에 보존했다.

## 자료와 재현

| 자료 | 위치/명령 |
|---|---|
| 최종 범위별 판정 | [component-acceptance.json](../../../CreatorContainers/results/20261026-component-acceptance.json) |
| 일반 82개 격리 표 | [final5-summary.json](../../../CreatorContainers/results/20261026-final5-summary.json) |
| MPSC 5모드 분리 표 | [mpsc-sampler-summary.json](../../../CreatorContainers/results/20261026-mpsc-sampler-summary.json) |
| 일반 환경/해시 | [final5-environment.json](../../../CreatorContainers/results/20261026-final5-environment.json) |
| MPSC 환경/해시 | [mpsc-sampler-environment.json](../../../CreatorContainers/results/20261026-mpsc-sampler-environment.json) |
| 정확성·sanitizer | results/20261026-final2-*.txt |
| 원인 대조/배열 profile | results/20261026-final4-variance-*.csv, final4-array-*.csv |
| 조건별 일반 원본 | results/20261026-final5-cases/ |
| MPSC 개별 전송 원본 | results/20261026-mpsc-sampler-*.csv |
| 선행 행렬 재현 | run-mpsc-variance.ps1 |
| 조건별 격리 재현 | run-isolated-acceptance.ps1 |
| MPSC 대조 재현 | experiments/20261026-mpsc-sampler/run.ps1 |
| 원시값·보존본 대조 | verify-mpsc-results.ps1 -Tag 20261026-final5 -ArchiveRoot baseline/20261026-measurement-stage5 |
| MPSC 하위 전송 검증 | summarize-mpsc-sampler.ps1 |
| 이 보고서 생성 | make-component-report.ps1 |

이전 실패·중단 자료를 삭제하지 않았다. 초기 74/82·3/4, 중간 77/82·4/4, 연속/격리 81/82 결과와 각 중단 사유를 유지한다. 새 재현에는 새 태그를 사용한다. 일반 양쪽 편차 게이트가 여전히 남으므로 전체 PASS 파일을 만들지 않는다.

## 엔진 통합 경계

엔진 PPL 별칭과 실제 소비자는 바꾸지 않았다. 프로파일러 collector_work에는 frame/counter/control/barrier가 섞여 있고, 프레임 드롭 한도를 barrier나 제어 명령에 적용할 수 없다. ProfileService.cpp의 큐 외 프레임 수·pending gap·control ticket·semaphore·종료 상태를 함께 보존해야 한다. 단순 고정 큐 치환이나 false 무시는 안전한 통합이 아니다.

남은 범위는 일반 엄격한 품질 게이트 1건, 실제 소비자별 overflow/소유권/수명/ABI 확인과 엔진 빌드·씬 전환·종료·프레임 검증, GCC/macOS/ARM64 및 OS 간 성능 행렬이다. 이 결과만으로 엔진 프레임 성능이나 모든 PC OS에서의 일정 성능을 달성했다고 표기하지 않는다.

## 일반 컨테이너 전체 표

| 작업, N | 중앙 비율 | 세 반복 비율 범위 | 최대 양쪽 표본 편차 | 수용 |
|---|---:|---:|---:|---|
| core, array_growth, 23 | 0.511 | 0.504–0.521 | 1.021 | True |
| core, array_growth, 24 | 0.531 | 0.512–0.547 | 1.017 | True |
| core, array_growth, 4096 | 0.743 | 0.462–0.837 | 1.014 | True |
| core, array_growth, 64 | 0.588 | 0.586–0.604 | 1.409 | False |
| core, array_growth, 65536 | 0.188 | 0.145–0.193 | 1.045 | True |
| core, array_growth, 8 | 0.177 | 0.176–0.179 | 1.029 | True |
| core, array_reserved, 23 | 0.857 | 0.847–0.857 | 1.182 | True |
| core, array_reserved, 24 | 0.846 | 0.835–0.882 | 1.123 | True |
| core, array_reserved, 4096 | 0.764 | 0.761–0.776 | 1.039 | True |
| core, array_reserved, 64 | 0.821 | 0.780–0.834 | 1.090 | True |
| core, array_reserved, 65536 | 0.804 | 0.800–0.807 | 1.018 | True |
| core, array_reserved, 8 | 0.940 | 0.936–0.944 | 1.027 | True |
| core, fifo_batch, 23 | 0.322 | 0.321–0.322 | 1.112 | True |
| core, fifo_batch, 24 | 0.327 | 0.293–0.328 | 1.017 | True |
| core, fifo_batch, 4096 | 0.408 | 0.406–0.410 | 1.074 | True |
| core, fifo_batch, 64 | 0.356 | 0.352–0.357 | 1.009 | True |
| core, fifo_batch, 65536 | 0.367 | 0.358–0.368 | 1.082 | True |
| core, fifo_batch, 8 | 0.363 | 0.359–0.363 | 1.024 | True |
| core, map_build, 23 | 0.146 | 0.143–0.148 | 1.015 | True |
| core, map_build, 24 | 0.137 | 0.135–0.138 | 1.076 | True |
| core, map_build, 4096 | 0.161 | 0.160–0.161 | 1.052 | True |
| core, map_build, 64 | 0.099 | 0.088–0.102 | 1.040 | True |
| core, map_build, 65536 | 0.233 | 0.223–0.234 | 1.257 | True |
| core, map_build, 8 | 0.248 | 0.238–0.253 | 1.029 | True |
| core, map_hit, 23 | 1.058 | 1.050–1.059 | 1.071 | True |
| core, map_hit, 24 | 1.022 | 1.022–1.023 | 1.053 | True |
| core, map_hit, 4096 | 0.826 | 0.821–0.828 | 1.019 | True |
| core, map_hit, 64 | 1.065 | 1.064–1.065 | 1.011 | True |
| core, map_hit, 65536 | 0.534 | 0.533–0.536 | 1.048 | True |
| core, map_hit, 8 | 1.031 | 1.022–1.031 | 1.012 | True |
| core, map_miss, 23 | 0.959 | 0.959–0.963 | 1.015 | True |
| core, map_miss, 24 | 0.882 | 0.881–0.882 | 1.019 | True |
| core, map_miss, 4096 | 0.600 | 0.598–0.605 | 1.062 | True |
| core, map_miss, 64 | 1.030 | 1.027–1.031 | 1.025 | True |
| core, map_miss, 65536 | 0.337 | 0.335–0.338 | 1.027 | True |
| core, map_miss, 8 | 1.028 | 1.028–1.029 | 1.021 | True |
| core, string_append, 23 | 0.627 | 0.625–0.644 | 1.017 | True |
| core, string_append, 24 | 0.983 | 0.974–0.986 | 1.012 | True |
| core, string_append, 4096 | 0.812 | 0.810–0.851 | 1.094 | True |
| core, string_append, 64 | 0.792 | 0.786–0.795 | 1.047 | True |
| core, string_append, 65536 | 0.827 | 0.819–0.853 | 1.010 | True |
| core, string_append, 8 | 0.982 | 0.974–0.983 | 1.044 | True |
| core, string_copy, 23 | 0.234 | 0.233–0.235 | 1.042 | True |
| core, string_copy, 24 | 1.009 | 1.007–1.076 | 1.080 | True |
| core, string_copy, 4096 | 1.001 | 1.000–1.007 | 1.023 | True |
| core, string_copy, 64 | 1.002 | 0.997–1.002 | 1.015 | True |
| core, string_copy, 65536 | 1.000 | 0.998–1.003 | 1.010 | True |
| core, string_copy, 8 | 0.991 | 0.987–1.005 | 1.040 | True |
| core, string_find, 23 | 0.750 | 0.743–0.752 | 1.050 | True |
| core, string_find, 24 | 0.772 | 0.771–0.777 | 1.040 | True |
| core, string_find, 4096 | 0.218 | 0.217–0.223 | 1.090 | True |
| core, string_find, 64 | 0.419 | 0.415–0.419 | 1.030 | True |
| core, string_find, 65536 | 0.223 | 0.221–0.224 | 1.204 | True |
| core, string_find, 8 | 0.660 | 0.647–0.678 | 1.133 | True |
| core, string_view_construct, 23 | 0.280 | 0.269–0.282 | 1.038 | True |
| core, string_view_construct, 24 | 0.957 | 0.952–0.978 | 1.056 | True |
| core, string_view_construct, 4096 | 1.001 | 1.000–1.007 | 1.016 | True |
| core, string_view_construct, 64 | 0.969 | 0.965–0.973 | 1.052 | True |
| core, string_view_construct, 65536 | 1.001 | 1.000–1.003 | 1.015 | True |
| core, string_view_construct, 8 | 0.807 | 0.795–0.808 | 1.043 | True |
| expanded, age_expiration, 256 | 0.725 | 0.706–0.729 | 1.019 | True |
| expanded, age_expiration, 4096 | 0.099 | 0.099–0.101 | 1.022 | True |
| expanded, atom_duplicate, 256 | 0.804 | 0.787–0.808 | 1.062 | True |
| expanded, atom_duplicate, 4096 | 0.724 | 0.719–0.724 | 1.055 | True |
| expanded, fixed_arena_churn, 256 | 0.558 | 0.546–0.560 | 1.120 | True |
| expanded, fixed_arena_churn, 4096 | 0.568 | 0.554–0.572 | 1.127 | True |
| expanded, function_dispatch, 256 | 0.989 | 0.979–0.990 | 1.138 | True |
| expanded, function_dispatch, 4096 | 0.959 | 0.950–0.962 | 1.017 | True |
| expanded, lru_touch, 256 | 0.258 | 0.238–0.263 | 1.198 | True |
| expanded, lru_touch, 4096 | 0.279 | 0.275–0.282 | 1.172 | True |
| expanded, owned_array, 256 | 0.973 | 0.904–1.031 | 1.030 | True |
| expanded, owned_array, 4096 | 0.912 | 0.893–0.947 | 1.031 | True |
| expanded, radix_sort_u64, 256 | 0.884 | 0.882–0.890 | 1.024 | True |
| expanded, radix_sort_u64, 4096 | 0.383 | 0.383–0.386 | 1.013 | True |
| expanded, sequence_window, 256 | 0.070 | 0.069–0.070 | 1.018 | True |
| expanded, sequence_window, 4096 | 0.043 | 0.042–0.044 | 1.078 | True |
| expanded, slot_lifetime, 256 | 0.268 | 0.266–0.269 | 1.050 | True |
| expanded, slot_lifetime, 4096 | 0.225 | 0.223–0.226 | 1.099 | True |
| expanded, string_map_build, 256 | 0.611 | 0.572–0.622 | 1.271 | True |
| expanded, string_map_build, 4096 | 0.666 | 0.666–0.672 | 1.162 | True |
| expanded, string_map_hit, 256 | 0.653 | 0.651–0.658 | 1.046 | True |
| expanded, string_map_hit, 4096 | 0.625 | 0.619–0.625 | 1.079 | True |
