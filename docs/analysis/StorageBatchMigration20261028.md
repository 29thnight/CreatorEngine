# 저장 컨테이너 잔여 전환 — 2026-10-28

자체 구현 ce::segmented_deque와 queue·stack·priority_queue, 노드 추출 가능한 해시 맵을 추가하고 **49건·32파일**을 전환했다. Effect의 erase_if 연결 1파일도 함께 변경했다. 기존 합격 정본 **74파일과 문자열은 유지**했다. 실제 Editor Debug 빌드·실행과 새 모듈 성능 수용은 통과했다.

조사 범위는 Engine·Editor·Lattice·Player·HeadlessHost의 추적·미추적 소스다. 저장 컨테이너 참조는 51건에서 **2건**으로 줄었다. 남은 2건은 ReflgenAuthoring.h의 외부 SDK 가상 함수 read_bytes 반환형과 그 결과 버퍼다. 반환 타입을 변경하면 override/ABI가 깨지므로 원본을 유지했다. 문자열 8,388건, callable 264건은 대기 범위이며 전체 STL 제거 완료를 의미하지 않는다.

## 구현 정본과 표준 라이브러리 경계

- 정본: [Creator.Containers.Storage.ixx](../../Engine/Containers/storage/Creator.Containers.Storage.ixx)
- 생성 헤더: [storage.h](../../Engine/Containers/storage/storage.h), sync-storage.ps1로 동기화
- 엔진 별칭: [EngineStorage.h](../../Engine/Utility_Framework/Foundation/EngineStorage.h)
- 독립 테스트 입구: 외부 CreatorContainers/storage/StorageValidation.proj. 엔진 정본과 테스트 프로젝트를 직접 참조하며 구현 복사본은 없다.

segmented_deque는 직접 할당하는 요소 블록과 순환 포인터 디렉토리를 사용한다. 디렉토리가 확장돼도 살아 있는 요소를 이동하지 않는다. 블록 크기는 요소 크기에 따라 32 / 8 / 1개다. queue와 stack도 이 자체 저장소를 사용하며 priority_queue는 기본적으로 기존 합격 dynamic_array에 heap 알고리즘을 적용한다.

해시 확장은 기존 정본을 수정하지 않고 별도 구현으로 추가했다. 독립 노드, 제어 바이트 인덱스와 이동 전용 node_type을 사용한다. 저장된 키는 읽기 전용으로 노출하고, 추출한 노드에서만 키를 바꿀 수 있다. 복사한 value_type의 키 참조도 복사본 자신의 키에 연결된다.

std::deque/queue/stack/priority_queue/unordered_map를 저장소로 감싼 구현이 아니다. allocator_traits, optional allocator 상태, iterator·tuple 프로토콜 및 heap·이동 알고리즘 등 표준 라이브러리 보조 기능은 사용한다. 표준 라이브러리 의존 전체 제거나 모든 STL API 호환을 주장하지 않는다. allocator는 raw pointer 범위를 지원하며, 해시 확장은 기존 ce 바이트 allocator 정책을 따른다. typed STL allocator 전파 규칙은 deque에서 검사했다.

## 계약과 실제 소비처 연결

| 구분 | 전환 참조 | 계약 |
|---|---:|---|
| deque | 34 | 양끝 확장 시 요소 주소 유지, 조회 무할당, pop/erase 시 값과 소유 자원 즉시 파괴 |
| queue / stack / priority_queue | 8 | FIFO/LIFO/비교 순서, unique 소유권, 기존 render arena 컨테이너 유지 |
| 추출 가능한 hash map | 7 | 생존 노드 주소, 추출 후 키 변경, 중복 거절 노드 보존, 롤백, 잠금 해제 뒤 마지막 소유자 파괴 |
| 외부 SDK ABI | 2 유지 | SDK 지정 반환형과 소유 버퍼 타입 보존 |

TextureFramePins의 이미지 추출은 잠금 안에서 수행하고 마지막 소유자 파괴는 잠금 밖에서 유지한다. ScenePhysicsSimulation은 기존 준비·예약 뒤 핸들 교체 경로를 유지했다. 물리 맵의 keys/values 뷰는 참조를 반환하는 transform 뷰로 연결했고, 구조적 바인딩도 지원한다. Effect의 삭제 조건은 그대로 두고 자체 deque용 ce::erase_if로 연결했다.

일반 heap 해시 노드는 erase/clear 시 개별 해제한다. deque는 요소 소멸자를 즉시 호출하고 요소가 모두 빠진 블록을 해제한다. 살아 있는 다른 요소와 공유하는 블록을 요소마다 별도 해제하지는 않는다. allocator가 다른 노드의 삽입은 노드를 보존하며 거절한다. 예약된 실제 교체·롤백 경로의 노드 전송은 무할당을 검사했다. 일반 삽입의 용량 확장과 tombstone 정리는 할당할 수 있다.

## MSVC STL 대비 최종 성능 수용

Windows / Intel Core i7-8700K(논리 12스레드) / MSVC 19.51, Release **모듈 바이너리**를 측정했다. CPU 4 High 우선순위, CPU 5의 별도 pause 통제 부하를 사용했다. 다른 프로세스나 OS 설정은 바꾸지 않았다.

각 측정은 양측 1초 예열, 15표본, 표본당 100ms 누적이다. 15조건을 같은 바이너리로 3회 반복했고 두 번째는 CE/STL 순서를 뒤집었다. **675쌍 / 1,350측 표본**이다. 매 반복·매 조건의 중앙값 CE/STL ≤ 1.10, 양측 정렬 표본 x[13]/x[1] ≤ 1.30 기준을 반올림 전 값으로 적용해 **45/45 통과**했다. 표본 제거·캠페인 이어 붙이기는 하지 않았다.

최대 시간 비율은 **1.0968561485594**, CE 최대 편차 **1.11273300940224**, STL 최대 편차 **1.10033332983913**이다. 다음은 조건별 세 반복 중 최대 시간 비율이다.

| 조건 / 요소 수 | CE/STL 최대 | 판정 |
|---|---:|---|
| deque_fifo, 128 | 0.5332 | PASS |
| deque_fifo, 4096 | 0.5296 | PASS |
| deque_fill, 128 | 0.4896 | PASS |
| deque_fill, 4096 | 0.5079 | PASS |
| deque_index, 128 | 1.0770 | PASS |
| deque_index, 4096 | 1.0640 | PASS |
| hash_aligned64_hit, 4096 | 0.4073 | PASS |
| hash_fill, 4096 | 1.0522 | PASS |
| hash_hit, 4096 | 0.6689 | PASS |
| hash_random64_hit, 4096 | 0.4327 | PASS |
| hash_random64_miss, 4096 | 0.5613 | PASS |
| hash_rekey, 4096 | 0.8854 | PASS |
| heap, 4096 | 1.0969 | PASS |
| queue, 4096 | 0.4970 | PASS |
| stack, 4096 | 0.6316 | PASS |

정수 deque의 크기 128/4096, 어댑터 4096, 해시 연속 정수·임의 64비트·128바이트 간격 주소 형태 키를 검사했다. 모든 키 타입·대형 값·다른 OS의 성능이나 엔진 프레임 시간 향상에 대한 측정은 아니다.

## 해시 진단과 메모리 비용

초기 진단에서 해시 조회 약 1.50배, 노드 키 왕복 약 1.64배로 미달했다. 내부 조회의 키 참조 간접 접근, iterator 재검사, 정수 hash 정책, 노드 핸들의 비어 있는 heap allocator 상태를 분리해 개선했다. 모듈 바이너리에서는 임의 키 miss와 노드 왕복이 다시 미달해 최종 모듈 기준으로 다시 수용했다. 디스어셈블과 실패 원시 CSV도 보존했다. PMU 하드웨어 카운터를 사용하지 않아 캐시 stall 등을 단일 확정 원인으로 주장하지 않는다.

최종 해시 확장의 기본 max_load_factor는 0.45다. 인덱스를 더 넓게 잡아 miss의 충돌 탐색을 줄이는 정책이며 비용이 있다. 별도 counting allocator로 uint64 → unsigned 4096개를 실제 검사한 결과는 다음과 같다. CRT 메타데이터·프래그먼테이션은 포함하지 않은 **allocator 요청 바이트**다.

| 항목 | CE | MSVC STL |
|---|---:|---:|
| bucket 수 | 16,384 | 4,096 |
| 생존 요청 바이트 | 245,760 | 196,640 |
| default node handle 크기 | 8 | 16 |

이 조건에서 생존 요청 바이트는 약 **25% 증가**했다. 조회 8,192회 중 추가 할당 0, 노드 한 개 erase 시 해제 1회, 전체 파괴 후 생존 바이트 0도 확인했다. 속도 수용을 메모리 사용량의 우위로 표현하지 않는다.

## 기능·소유권·빌드·실행

- 최종 모듈 Debug / Release: 각각 **48,244검사 통과**. 20,000회 deque 모델 대조, 대형 앞쪽 확장, 256바이트 정렬, 소유자 수명, 복사/이동과 노드 주소를 검사했다.
- allocator: SOCCC와 비전파 allocator 간 이동, 서로 다른 allocator swap 거절, 디렉토리 초기화 도중 예외와 기존 요소가 있는 확장 실패 후 복구를 검사했다. 임의 생성자 예외 후 기존 요소도 보존했다.
- 최종 standalone ASAN: **48,244검사 통과**. 실제 Editor 전체를 ASAN으로 계측한 결과는 아니다.
- 실제 CreatorEditor **Debug 빌드 성공**. 최초 빌드에서 발견한 erase_if·뷰 연결 문제를 수정한 후 다시 빌드했다. 기존 빌드 경고는 남아 있으며 경고 0을 주장하지 않는다.
- 실제 Editor **17명령 통과**. 검사 2회 × 8,192전송, 새 저장 컨테이너 주소·추출·키 변경·중복 롤백, PhysX 검사, GPU 새 프레임, Play/Stop, 장면 객체·변환 복원, 정상 종료를 확인했다.
- 외부 독립 테스트 입구 Release 빌드 성공. 실제 Editor Release와 모든 오디오/Vulkan 런타임 경로 실행은 이번 검증 범위가 아니다.
- 생성 어댑터 31개와 모듈/헤더 동기화 확인.

## 정본·문자열·미추적 작업 보존

기존 합격 정본 74파일은 SHA256이 동일하다. 이전 전체 82항목/PPL 수용의 원시 결과 266파일과 고정 입력 131파일, ordered set/map 확장 결과도 해시가 같다. 이 이전 전체 성능을 이번에 새로 측정한 것은 아니다.

전환 대상 32파일을 원본과 대조했다. 타입·include 및 물리 키/값 참조 뷰 연결을 제외한 코드는 유지했다. EffectState.cpp는 erase_if 연결만 바꿨다. 허용한 소비처·검사 연결을 제외한 기존 소스 1,464개(미추적 16개 포함)는 동일하다. 미추적 Effect 작업과 문자열 타입을 보존했다. 커밋·푸시는 수행하지 않았다.

## 근거와 재현

근거 루트: [StorageBatch20261028](../../Build/Verification/ContainerMigration/StorageBatch20261028)

- performance-final/result.json: 45개 판정, 고정 입력·바이너리·원시 CSV·CPU helper 해시
- native-accepted2/result.json / asan-accepted2/result.json
- editor-final/acceptance.json / editor-build2.log
- memory.log / consumer-equivalence.json / preservation.json / baseline-retained.json
- remaining-accepted/classification.json / migration-batch.json

재현 입구는 Tools/regression/verify-storage-migration.ps1, verify-storage-asan.ps1, verify-storage-performance.ps1이다. 각 스크립트의 OutputDirectory는 새 디렉토리여야 한다. 성능 스크립트는 이 로컬 환경의 CPU4/5 및 외부 테스트 CPU helper를 요구한다.