# CreatorContainers — C++23 독립 컨테이너 검증

2026-10-25 사용자 지시에 따라 기존 자체 컨테이너 제작 중단 결정을 폐기한다. 과거 중단 사유를 현재 구현 금지 조건으로 사용하지 않는다. 이 문서가 기존 `ContainerLibraryDesign`의 본문과 판정을 대체한다.

## 목표와 위치

STL 컨테이너·알고리즘·문자열 구현에 의존하지 않는 자체 라이브러리를 작성한다. 현재 독립 위치는 `C:/Users/idene/source/repos/CreatorContainers`다. CreatorEngine 솔루션, Directory.Build 설정, PhysX·reflgen·vcpkg·엔진 헤더를 가져오지 않는다. CMake가 생성하는 `build/CreatorContainers.slnx`를 Visual Studio에서 연다.

기준 문서는 [컨테이너·자료구조 분석](../analysis/ContainerDataStructureAnalysis20261025.md)이다. 해당 문서의 비용 추정은 성능 입증으로 사용하지 않는다. 비교 대상은 로컬 MSVC STL Release다. 정확성 검사의 STL 사용은 허용하고 배포 헤더와 분리한다.

## 현재 구현

| 타입 | 범위와 계약 |
|---|---|
| `ce::dynamic_array<T>` | 비단순/이동 전용 값, emplace/insert/erase/resize, copy/move, 예외 복구. 1.5배 성장, 오버얼라인·사용자 allocator 지원 |
| `ce::bounded_ring<T>` | 단일 스레드 제한 용량 FIFO. 생성 후 할당 없음. full/empty를 bool로 반환. SPSC/MPSC 아님 |
| `ce::flat_hash_map<V>` | uint64 키 호환 별칭. 일반 키 basic_flat_hash_map, 문자열 키 string_hash_map, flat_hash_set 추가. 비단순 값, 선형 조사, 최대 적재율 0.5, tombstone 삭제. 재해시 시 주소 무효화 |
| `ce::string` | UTF-8 바이트 문자열, 23바이트 SSO, NUL 종료, 길이 지정 시 내부 NUL 허용. copy/move, reserve, append/assign/insert/erase/resize, 검색·비교, 사용자 allocator |
| `ce::string_view` | 비소유 바이트 범위, 길이 보존 조회. 원본의 수명과 재할당에 종속 |
| `ce::concurrent_queue<T>` | 비단순 객체를 지원하는 자동 확장 MPMC FIFO. 블록 재사용·스핀락 기반. 락 프리 아님 |
| `ce::lock_free_queue<T>` | FAA 세그먼트 + hazard + 재사용 슬랩. prepare/publish/try_pop/reclaim 분리. 초기 등록·CRT·T 연산은 핵심 진행 보장과 별도 |
| `ce::bounded_spsc_ring<T,Capacity>` | 역할이 고정된 1생산자/1소비자 전용, 무할당 제한 링. 실패를 bool로 반환 |
| `ce::bounded_mpsc_queue<T,Capacity>` | 고정 값 풀 + 취소 가능한 FAA publication ring. 다중 생산자/단일 소비자, 코어 무할당·락 프리. 예약 저장소도 Capacity에 포함 |
| `slot_map<T>` | 256슬롯 청크, 원소 주소 안정성, 소유 테이블/64비트 generation 핸들, stale/cross-owner 거부 |
| `sequence_buffer<T,N>` | 고정 저장, 단조 uint64 시퀀스 윈도, 범위 밖 입력 거부, 자동 만료. 모듈러 wrap 미지원 |
| `intrusive_lru<Index>` | 호출자 링크, O(1) touch/erase, 같은 리스트에 링크 중복 소속 금지 |
| `string_atom_table` | 안정 문자열 저장, 중복 할당 방지, owner/generation 검증, clear로 핸들 무효화 |
| `age_buckets<T,N>` | 고정 시간창에 예약, 같은 tick은 삽입 순서 만료, noexcept 콜백 |
| `inplace_function<Sig,Bytes>` | 고정 inline 용량, 이동 전용 캡처, 힙 할당 없음. 초과 크기/throwing move는 컴파일 거부 |
| `radix_sort` | unsigned 32/64비트 키 안정 정렬, 작은 범위는 안정 insertion/merge, 큰 범위는 LSD. 임시 allocator 지원 |
| `memory_resource` | heap/fixed-buffer/monotonic/TLSF. allocator는 저장소 소유권과 함께 전파. 중첩 값 allocator는 자동 전파하지 않음 |

STL의 완전 대체 API가 아니다. iterator는 제공 타입의 범위 순회 계약이며 STL iterator 전체 요구를 충족한다고 주장하지 않는다. 이동/복사/할당자·예외 보장은 [확장 구현 보고서](../analysis/ContainerLibraryExpansion20261025.md)에 정리한다. `string`의 인덱스는 바이트 기준이며 유니코드 정규화·문자 단위 인덱싱·locale 처리는 제공하지 않는다. C 런타임 `memcpy/memmove/memchr`와 `<new>`의 placement new·정렬 할당·실패 예외는 사용한다. 표준 컨테이너에 대한 의존 제거와 C/C++ 런타임 제거는 구별한다.

## OS 일관성 정책

성장률, 해시 혼합, 적재율, 탐색·삭제 규칙, SSO 용량은 OS와 STL 구현에 무관하게 동일하게 고정한다. 핵심 정책은 OS와 분리하며 원자 연산·비트 검색은 컴파일러별 작은 백엔드로 제공한다. 공통 allocator와 비단순 객체 수명을 구현했다. 기본 string 크기는 MSVC 40B이며 ClangCL에서는 no_unique_address 지원 차이로 더 클 수 있어 모듈/컴파일러 사이 raw ABI 공유를 가정하지 않는다. OS별 heap·CRT·컴파일러·CPU·스케줄러 차이 때문에 동일 시간은 보장할 수 없다.

현재 확인 대상은 Windows x64 + MSVC/ClangCL, WSL Ubuntu x86_64 + Clang 18이다. Linux는 정확성·ThreadSanitizer 실행까지 수행했고 성능은 비교하지 않았다. GCC, macOS AppleClang, ARM64 실행은 미검증이다. 이 행렬의 실측 없이는 '모든 PC OS에서 거의 일정한 성능'을 달성했다고 표기하지 않는다.

## 독립 검증과 수용 기준

2026-10-26 최종 독립 수용은 [소형 배열 편차 후속 보고서](../analysis/SmallArrayVarianceAcceptance20261026.md)가 정본이다. 일반 82/82와 고정 MPSC 4/4를 같은 SMT 제어 조건에서 통과했으며 엔진 제품 수용과 구분한다. `CreatorContainers/run-isolated-acceptance.ps1`을 자체 SMT 제어 helper 안에서 실행해 재현한다. 기존 구현 계약과 단계별 실패 자료는 [MPSC·배열·편차 보고서](../analysis/MPSCVarianceAcceptance20261026.md)에 보존한다. [확장 보고서](../analysis/ContainerLibraryExpansion20261025.md)와 아래 초기 `run-tests.ps1` 결과는 당시 범위의 기록이다.

`CreatorContainers/run-tests.ps1`은 Release 빌드, 프로세스 3회 반복 측정, ASan 정확성 실행, 소스·바이너리 해시 및 환경 기록을 남긴다. 결과 원본은 `results/release-run1.csv`부터 `release-run3.csv`, `asan.txt`, `clang.txt`, `environment.json`이다.

측정은 N=8/23/24/64/4096/65536, 워밍업 3회, 표본 15개, 작은 작업은 약 2ms 이상으로 묶어 수행한다. A/B 실행 순서를 교대하고 체크섬으로 결과를 소비한다. 표본은 각 작업 묶음의 시간이며 단일 원소 ns와 혼동하지 않는다. 세 프로세스의 중앙값 비율을 함께 본다. 0.90 미만은 우위 후보, 0.90~1.10은 동급 후보, 1.10 초과는 보완 대상으로 삼는다. 표본 편차가 큰 항목은 확정 판정하지 않는다.

배열은 reserve 유무를 나누고, FIFO는 제한 링 대 deque의 용도 비교다. 해시맵은 자체 적재율 0.5 대 STL 기본 정책이며 더 큰 버킷 저장 공간을 사용할 수 있다. 동일 메모리 예산 비교는 아직 없다. 문자열 복사·문자 추가·고정 패턴 검색을 비교한다. 짧은 문자열은 SSO 용량 차이의 효과를 포함한다. 캐시는 워밍업 후이며 콜드 캐시·페이지 폴트·실제 엔진 분포 측정이 아니다. 다른 프로그램을 종료하거나 PC 전원 정책을 바꾸지 않는다.

정확성 검사는 STL과 대조하는 해시맵 50만 연산, 링 wrap/full/empty, 배열 자기 참조 확장·복사/이동·128바이트 정렬·크기 오버플로, 문자열 SSO 경계·자기/부분 범위 추가·내부 NUL·UTF-8·검색 경계 등을 포함한다.

## 엔진 통합 게이트

독립 마이크로벤치 통과는 엔진 프레임 성능 입증이 아니다. 다음 조건을 소비자별로 만족한 범위만 통합한다.

1. 해당 타입·분포·연산에서 MSVC STL과 동급 또는 우위이고 변동성이 수용 가능해야 한다. 문자열 등 아직 느린 경로를 숨긴 전체 평균으로 통과시키지 않는다.
2. 실제 소비자의 원소 수, 수명, 순서·주소 안정성, 예외·동시 접근 요구를 확인하고 필요한 API를 구현한다.
3. 순서가 필요한 쿠킹·검증·렌더 그래프·안정 정렬 경로를 보존하고 stale generation, unregister/re-registration 같은 수명 회귀를 확인한다.
4. 최초 소비자 한 곳만 A/B 적용하고 엔진 빌드·실제 씬·정확성·전후 시간·메모리를 검증한다. 리플렉션/직렬화/SDK 경계는 별도 계약으로 유지한다.

현재는 독립 구현·검증 단계다. 후속 보고서의 Windows warm throughput 수용과 실제 소비자 수용을 구분하며, 엔진 통합은 아직 시작하지 않았다. 초기 결과는 [측정 보고서](../analysis/ContainerLibraryBenchmark20261025.md)에 보존한다.

## 후속 순서

일반 컨테이너 확장과 고정 용량 MPSC를 구현했다. 성능은 [MPSC·배열·편차 보고서](../analysis/MPSCVarianceAcceptance20261026.md)의 조건별 결과로 판정한다. 독립 성능 수용 → 실제 소비자 한 곳의 분포/메모리/수명/프레임 수용 → OS/컴파일러 행렬 확대 순서로 진행한다. 엔진 통합은 아직 완료하지 않았다. 기존 FAA MPMC 큐의 성능 수용은 [최적화 보고서](../analysis/LockFreeQueueOptimization20261025.md)가 정본이다.

## 고정 용량 MPSC 및 확장 최적화 계약

`bounded_mpsc_queue`는 예약한 생산자가 멈춰도 소비자가 빈 예약을 취소해 다음 epoch로 진행한다. 생산자는 값을 유지한 채 재공개한다. 풀 탐색은 고정 크기 안에서 종료하며 공개 재시도 실패는 소비자의 진행을 뜻한다. 정확히 한 소비자만 허용하고 생성/소멸에는 동시 접근이 없어야 한다. 코어 진행 보장에 사용자 생성자/이동 대입/소멸자와 예외 런타임의 진행 보장을 포함하지 않는다. 값 이동 대입/소멸은 noexcept를 요구한다. `false/unavailable`은 저장소 예약 또는 경합 실패도 포함하므로 공개 FIFO의 strict full 판정이 아니다. false 시 인수는 소비하지 않는다. ticket 한도 소진은 예외이며 재생성이 필요하다.

배열의 일반 heap 재할당은 trivial/noexcept-move 값과 구체 allocator의 명시적 `byte_relocation_allocator` opt-in에만 사용한다. 상속 allocator가 우연히 CRT realloc을 물려받아 소유권을 깨지 않게 한다. 기본 CRT 정렬 한도는 MSVC x64 16B, 그 밖에는 alignof(max_align_t)이며 new-alignment 컴파일 옵션과 분리한다. 오버얼라인은 정렬 new/delete 경로다. 일반 heap은 전역 operator new hook을 경유하지 않으므로 엔진 계측에는 명시적 resource를 연결한다.

해시맵은 상위 7비트 제어 태그와 8바이트 그룹 조회를 사용하고 작은 테이블에는 scalar 경로를 유지한다. 최대 적재율 0.5와 tombstone 계약은 유지한다. 제어 저장소에 wrap 조회용 8바이트 복제본을 추가하며 재해시의 주소 무효화 계약은 동일하다.
## PPL 대체 후보의 진행 보장

PPL의 try_pop 빈 큐 반환 계약을 내부 모든 경로의 lock-free 보장으로 해석하지 않는다. 로컬 설치 헤더에는 내부 마이크로 큐의 동기화 필드가 있다. 자체 concurrent_queue는 하나의 스핀락으로 직렬화하는 독립 대체 후보이며 PPL과 구조·공정성·최대 지연이 같다는 뜻은 아니다. 컴파일러 원자 연산을 사용하더라도 락 프리가 자동으로 성립하지 않는다. SPSC는 고정 역할·고정 용량 계약이며 두 생산자 이상의 경로에는 사용하지 않는다.

## 락 프리 MPMC 현재 구현

Windows 독립 성능 게이트를 통과했다. 기본 push/try_pop 6개 조건의 5회 프로세스 중앙값과 큐 소멸 포함 중앙값이 모두 PPL의 1.10배 이내다. 이 판정은 실제 엔진 프레임 수용과 구분한다.

선택 타입 `ce::lock_free_queue<T>`는 FAA 배열 세그먼트·hazard·값 슬랩을 사용한다. 이전 노드별 MS 구현은 비교 기준으로 보존했고, 스핀락 타입 및 엔진 PPL 별칭은 자동 치환하지 않는다. 기본 push/try_pop을 이전 구현과 PPL에 대조한 원시 자료, 분리 계측 및 수용 판정은 [최적화 보고서](../analysis/LockFreeQueueOptimization20261025.md)가 정본이다.

값은 이동 대입/소멸이 noexcept여야 한다. 32B 이하 값 노드의 슬랩은 256개, 나머지는 64개 슬롯을 묶어 할당하고 clear/소멸까지 유지한다. 원소 처리는 FIFO이며 잔존 값의 소멸 순서가 필요하면 명시적으로 drain한다. prepare는 예비 세그먼트도 만들며 등록 완료 후 publish는 경계에서도 할당·해제 없이 공개한다. 기본 push의 캐시 미스, 초기 TLS 등록, CRT 및 사용자 T 연산까지 무조건 락 프리라고 표현하지 않는다.

`MaxParticipants=64`는 T/한도/Hook 특수화별로 등록되어 살아 있는 스레드 수다. 초기 구현의 활성 호출 한도와 구분해야 한다. 종료 시 등록이 해제되고 초과 시 명시적 예외를 던진다. TLS에는 큐 주소가 없어 큐 수명이 먼저 끝나도 그 큐를 만지지 않는다. clear/소멸은 모든 접근이 끝났음을 동기화한 뒤에만 실행한다.

생산자가 예약 직후·공개 직후 멈춘 상황, 보호 중인 세그먼트의 지연 회수, 참여자 한도, 짧은 큐 수명, 256슬롯 경계, 예외·이동 전용 소유권을 검사했다. MSVC/ClangCL 외 GNU 원자 백엔드도 WSL Ubuntu Clang 18에서 Release와 ThreadSanitizer를 실행했다. 실행 범위는 보고서와 로그로 구분하며 실제 엔진 프레임 성능은 아직 입증하지 않았다.

## 2026-10-26 모듈 구현

독립 라이브러리의 구현 22개를 .ixx 인터페이스 파티션으로 이전하고 Creator.Containers 기본 모듈로 재수출한다. .ixx가 정본이며 기존 헤더는 생성하는 호환 경로다. MSVC Release/ASan 각각 9개, Linux Clang 7개 실행 검사가 통과했다. 모듈 사용법, 실제 검사 범위와 성능 재수용 경계는 [모듈 구현 보고서](../analysis/ContainerModules20261026.md)를 참조한다.

## 2026-10-26 모듈 성능 수용

모듈 경로의 문자열·32비트 원자 래퍼·기본 빈 훅에 명시적 inline을 지정하고 전체 수용을 재실행했다. 일반 82/82, MPSC 주 수용·개별 전송 각 4/4, FAA 6/6, SPSC·스핀 MPMC 처리량 8/8로 PASS다. 원자 순서·메모리 배치·상태 전이는 동일하다. MSVC Release 19/19, 모듈 ASan 9/9, Linux 모듈 TSan 7/7을 통과했다. [원인 분리](../analysis/ModuleOptimizationDiagnostics20261026.md)와 [최종 전체 표](../analysis/ModulePerformanceOptimization20261026.md)를 참조한다. 엔진 통합·실제 프레임 수용과 OS 간 성능 동등성은 별도 게이트다.
