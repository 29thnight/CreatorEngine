# 정렬 set/multiset 119건 전환과 검증 — 2026-10-28

정렬 계열 355개 참조 중 set 118건과 multiset 1건을 55개 소비자 파일에서 전환했다. 정렬 map 235건·multimap 1건은 남아 있다. 전체 정렬 컨테이너 전환 완료가 아니라 정렬 집합 배치 완료다. 기존 합격 정본 66개 파일과 문자열 타입은 유지했다.

## 구현과 모듈 경계

`Engine/Containers/ordered/Creator.Containers.Ordered.ixx`를 새 정렬 집합 정본으로 추가했다. AVL 트리의 노드·회전·탐색·삭제·수명 처리를 자체 구현했다. 기존 합격 파일은 수정하지 않았다. standalone 헤더는 `sync-ordered.ps1`로 모듈 본문에서 생성하고 동기화를 검사한다.

EngineContracts에 named module을 연결하고 리플렉션 도구에도 BMI 참조를 전달한다. `EngineOrderedSet.h`는 엔진 키 비교 정책 어댑터다. 비교 정책·iterator 태그·컴파일 타임 특성·C++ 런타임은 표준 경계를 사용한다. 표준 컨테이너를 내부 저장소로 사용하지 않으며 완전한 표준 라이브러리 독립을 주장하지 않는다.

개별 노드는 전역 new/delete로 할당·해제하며 over-aligned 키는 정렬된 new/delete를 짝지어 사용한다. 풀에 삭제 노드를 남기지 않는다. 회전은 링크만 바꾸고, 두 자식 노드 삭제는 후계자 노드를 물리적으로 이식한다. 생존 키를 복사·이동·대입하지 않아 생존 노드 주소와 반복기를 유지한다. 이동·swap 후 요소 반복기는 새 소유 컨테이너의 sentinel로 연결된다. end 반복자 보존을 보장한다는 뜻은 아니다.

키는 불변이고 복사 삽입은 키를 소유한다. set은 동등 키를 억제하며 multiset은 upper-bound 위치에 삽입해 기존 동등 키의 상대 순서를 유지한다. 정·역순 순회와 lower_bound/upper_bound/equal_range를 제공한다. 두 컨테이너 모두 기존과 같이 외부 동기화가 필요한 저장소다.

## 실제 성능 계측과 원인 분리

소비자 전환 전에 named-module Release 바이너리를 MSVC STL과 비교했다. 최종 입력·소스·바이너리를 고정한 새 3프로세스 캠페인은 10조건 모두 통과했다. 각 조건·각 측면 1초 워밍업, 15표본·표본당 100ms 누적 계측을 사용했다. 두 번째 프로세스는 CE/STL 측정 순서를 뒤집었다. 원시 450쌍을 모두 보존했다.

매 회차 중앙값 CE/STL ≤1.10, 양측 정렬 15표본 x[13]/x[1] ≤1.30을 적용했다. 표본 삭제·교체·기준 완화는 없다. 3회 중 최대 비율은 1.0979, 최대 CE 편차 1.0531, 최대 STL 편차 1.0666이다.

| 조건, 원소 수 | 3회 최대 CE/STL | 판정 |
|---|---:|---|
| hit, 128 | 1.0213 | PASS |
| hit, 4096 | 0.9584 | PASS |
| insert_clear, 128 | 1.0979 | PASS |
| insert_clear, 4096 | 1.0343 | PASS |
| interleaved_miss, 128 | 0.8503 | PASS |
| interleaved_miss, 4096 | 0.8894 | PASS |
| miss, 128 | 0.1758 | PASS |
| miss, 4096 | 0.1080 | PASS |
| multi_equal_range, 4096 | 1.0664 | PASS |
| multi_insert_clear, 4096 | 0.9543 | PASS |

insert_clear는 고정 키 순열의 삽입과 전체 파괴를 함께 측정한다. hit는 삽입 키 조회, miss는 모든 키보다 큰 범위 조회, interleaved_miss는 키 범위 내부의 빈 키 조회다. 범위 밖 miss 결과를 일반적인 모든 miss의 속도라고 해석하지 않는다. multiset은 4,096개 값을 32개 동등 키 그룹에 넣어 삽입·파괴와 equal_range 순회를 측정했다.

개발 계측 1차는 8조건 중 4조건 미달이었다. MSVC 생성 코드에서 조회의 조건부 이동·포인터 의존 경로와 노드 할당의 외부 호출을 확인했다. 자식 부재 시 명시적 반환과 직접 노드 new/delete로 바꾼 2차에서 small hit와 삽입은 통과했지만 범위 밖 miss 두 조건이 남았다. 비교 함수가 필요한 조회 경로에는 원래 할당이 없으므로 조회 개선을 할당 제거 효과로 해석하지 않는다.

기존 최대 키 캐시로 범위 밖 조회를 조기에 반환하게 한 3차에서는 10조건 중 small hit만 비율 약 1.11로 미달했다. 최대 키 비교를 루트가 오른쪽으로 안내할 때만 수행하도록 줄여 4차가 통과했고, 이후 별도 고정 3회 캠페인으로 재검증했다. 중간 실패 CSV·소스 스냅샷과 disassembly를 보존했다. PMU stall·분기 예측률을 계측한 것은 아니며 생성 코드와 실행 결과를 연결한 해석이다.

현재 Windows 11 / i7-8700K / MSVC 19.51 계측이다. 자체 프로세스는 High 우선순위·CPU4, 기존 bounded CPU5 pause helper를 사용했다. 다른 앱의 우선순위·친화도·OS 설정은 변경하지 않았다. 계측 중 엔진 빌드와 소스 편집을 하지 않았고 각 프로세스 사이에 결과를 확인했다. 정수 키 10조건의 수용이며 문자열·모든 키 타입·모든 OS의 속도 수용은 아니다. 실제 프레임 성능 향상을 주장하지 않는다.

## 소유권·수명·할당 검사

| 검사 | 결과 |
|---|---|
| named-module Debug | 583,092 checks |
| named-module Release | 583,089 checks |
| standalone ASAN | 583,089 checks, 종료 코드 0 |
| 리플렉션 실제 값 왕복 | 37 axes, failures=0 |

8,192개 삽입·삭제 연산을 표준 set 모델과 비교하며 정·역순과 경계 조회를 대조했다. 1,024개 노드의 회전·삭제 후 생존 주소, 이동·swap 후 반복기, 4,096개 동등 키 payload의 삽입 순서와 그룹 삭제를 검사했다. 키 복사 실패 후 기존 소유·노드·크기 보존, 별도 복사의 소유 독립, 공동 소유자 생존과 최종 clear 파괴, 64바이트 정렬 키도 검사했다.

긴 문자열 hit/miss·count·종료 문자가 없는 부분 뷰를 8,192회 조회했고 Debug CRT 호출 스레드 할당은 0회였다. 내부 NUL과 원본 버퍼 변경 후 소유 키 보존도 확인했다. Debug에서 정수 키 erase는 노드 해제 1회·할당 0회, 32개 clear는 노드 해제 32회였다. Release·ASAN을 CRT 무할당 증거로 사용하지 않는다. 기존 호출자가 생성하는 임시 문자열까지 전부 무할당이라고 주장하지 않는다.

## 실제 엔진 빌드·실행

Debug Editor를 Effect 패키지와 static-CRT launcher까지 실제 빌드했다. 첫 빌드에서 Lattice의 `contains({node.id, section})` 형상만 템플릿 추론에 실패해 기존과 같은 `ce::pair<Id, std::string>` 키를 명시했다. 키 소유·문자열 생성 동작은 유지했으며 성능 수용한 모듈은 수정하지 않았다. 실패 로그와 최종 빌드 로그를 남겼다.

실제 Editor는 17명령 모두 통과했다. 새 orderedSetStorage 검사를 두 번 실행하고 경계·역순·주소·중복 키 순서를 확인했다. 기존 2×8,192 전송, UTF-16, 물리·힙·draw seal 검사, 새 GPU 프레임 완료, 렌더 프록시 게시, Play/Stop·장면 복원·정상 종료도 유지했다.

실제 DX12 장치에서 GPUValidation을 켜고 Debug 8,786건, Release 8,781건을 통과했다. 64성분·4프레임과 Debug warmed pool/native cache의 조회 무할당·삭제 전 해제 회귀를 확인했다. 모든 플랫폼·장면·Audio/Vulkan 실행 경로 수용으로 확대하지 않는다.

## 원본·합격 정본 보존과 잔여량

55개 소비자는 집합 타입·include와 위 명시적 키 1곳만 바뀌었음을 수정 전 사본과 대조했다. 문자열 리터럴·인코딩과 미추적 Effect 소스의 관련 없는 로직을 유지했다. 허용 목록 밖 1,441개 소스(미추적 15개 포함)는 해시가 동일하다. 기존 합격 정본 66개 및 기존 전체 성능 캠페인의 원시 266개·입력 131개 해시도 일치한다. 원래 82조건 전체 캠페인은 이번에 다시 실행하지 않았다.

| 남은 저장 컨테이너 | 참조 |
|---|---:|
| 정렬 map | 235 |
| 정렬 multimap | 1 |
| deque | 34 |
| stack/queue/priority_queue | 8 |
| 별도 노드 추출 맵 | 7 |
| reflgen SDK vector 경계 | 2 |
| 합계 | **287** |

저장 참조는 406→287, 전체 감사 참조는 9,055→8,939다. 문자열은 8,385→8,388로, 새 비교 어댑터의 string/wstring 정책 선언 두 곳과 명시적 pair 조회 키 한 곳이 추가된 것이다. 기존 문자열 타입 교체는 없다. 함수 래퍼 264건과 값 타입 allocator 경계 1건도 남아 있다. map/multimap의 정렬·노드 추출·allocator·롤백 계약은 이번에 변환하지 않았다.

## 재현과 증거

- [새 모듈 성능 수용](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/performance-acceptance.json), [30개 회차별 판정](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/performance-summary-3.json)
- [네이티브 검사](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/native-accepted2/result.json), [실제 Editor 실행](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/editor-final/acceptance.json), [실제 DX12](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/material-final/result.json)
- [소비자 원본 대조](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/consumer-equivalence.json), [다른 작업 보존](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/unrelated-preservation.json), [기존 성능 합격 보존](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/baseline-performance-retained.json)
- [잔여 분류](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/remaining-final/classification.json), [최종 manifest](../../Build/Verification/ContainerMigration/OrderedSetBatch20261028/migration-batch.json)
- 외부 독립 검증 진입점 `../CreatorContainers/ordered-set/OrderedSetValidation.proj`도 실제 빌드했다. 구현을 복제하지 않고 엔진 정본의 별도 프로브 프로젝트를 참조한다. 증거는 배치의 `external-build.log`다.
- 재현 스크립트: `verify-ordered-set-migration.ps1`, `verify-ordered-set-performance.ps1`, `verify-container-integration.ps1`, `verify-material-render-bindings.ps1`.
- 중간 `native1`, `native-accepted`, `perf1~4`, 최종 `native-accepted2`, `asan-final.log`, `reflection-roundtrip.log`, `Build/ordered-set-editor*.log`를 보존했다. over-aligned 검사 키의 의도한 padding 경고는 해당 프로브 프로젝트에서만 억제했다.
