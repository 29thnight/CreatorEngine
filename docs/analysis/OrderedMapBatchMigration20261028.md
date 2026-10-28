# 정렬 map / multimap 일괄 전환 — 2026-10-28

90개 소비 파일의 236개 타입 참조를 전환했다. Engine·Editor·Lattice·Player·HeadlessHost의 추적·미추적 소스를 함께 조사한 범위에서 남은 std::map / std::multimap 참조는 0이다. 문자열 타입은 유지했고, 문자열 참조 8,388건도 동일하다. 나머지 저장 컨테이너 참조는 287건에서 51건으로 줄었다. 전체 STL 제거 완료를 뜻하지 않는다.

## 구현과 경계

구현 정본은 Engine/Containers/ordered-map/Creator.Containers.OrderedMap.ixx이다. 자체 red-black tree와 개별 노드 할당을 사용한다. 헤더는 sync-ordered-map.ps1로 생성하며 Foundation/EngineOrderedMap.h가 엔진 별칭을 제공한다. Contracts와 RuntimeLauncher의 모듈 빌드 및 생성 어댑터 연결을 갱신했다. 외부 CreatorContainers/ordered-map/OrderedMapValidation.proj도 엔진 정본을 직접 참조한다.

이전 합격 정본 66개 파일과 ordered set 확장 4개 파일은 해시가 같다. 이번 새 구현은 allocator_traits, iterator 보조 기능, 노드 핸들의 allocator 상태용 std::optional을 사용한다. arena 경계의 std::pair도 중첩 컨테이너 allocator 전파를 위해 유지했다. STL 트리 저장 구현은 사용하지 않지만 표준 라이브러리 의존 전체 제거를 주장하지 않는다.

## 계약 검증

정렬, 중복 키의 동등 그룹 삽입 순서, 삭제되지 않은 노드의 주소 안정성을 검사했다. 삭제는 값을 이동시키지 않고 successor 노드를 연결한다. 조회와 동일 allocator의 노드 추출·재삽입·중복 거절 후 롤백은 무할당이다. 일반 heap 노드는 erase/clear 시 해제하며 arena 메모리는 기존 reset 정책을 유지한다.

SOCCC, POCCA, POCMA 및 allocator construct/destroy 호출을 검증했다. 서로 다른 비전파 allocator로 이동할 때 대상 도메인에서 재구성한다. allocator가 다른 노드 삽입은 노드를 보존하며 거절한다. 소유권 이동, 중복 삽입 시 rvalue 미소비, 마지막 소유자 파괴와 잠금 해제 이후 파괴를 검사했다. arena 원본 버퍼를 reset 후 덮어써 독립 복사의 수명도 검사했다.

node_type.key()는 const 참조다. 가변 rekey, 서로 다른 Compare 사이의 노드 핸들 호환, fancy pointer allocator까지 STL 전체 API 호환을 주장하지 않는다. 이번 소비처는 가변 rekey를 요구하지 않는다. 남은 unordered 노드 추출 계약 7건은 별도 전환 대상으로 유지했다.

## 실제 성능 수용

Windows / Core i7-8700K / MSVC 19.51에서 12조건 × 3회, 6개 프로세스로 측정했다. 각 측정은 양측 1초 예열 후 15표본, 표본당 100ms 누적이다. CPU 4의 High 우선순위 측정과 CPU 5의 통제 부하를 사용했고 두 번째 반복은 STL/CE 순서를 뒤집었다. 총 540쌍이며 각 반복의 중앙값 CE/STL ≤ 1.10, 양측 정렬 표본 x[13]/x[1] ≤ 1.30 기준을 모두 통과했다. 반올림 전 수치로 판정했다.

최대 비율 1.09486339508984, CE 최대 편차 1.03506476610391, STL 최대 편차 1.0290765769736이다. 아래 비율은 조건별 세 반복 중 최댓값이다.

| 조건 / 크기 | CE/STL 최대 | 판정 |
|---|---:|---|
| hit, 128 | 0.9838 | PASS |
| hit, 4096 | 0.9078 | PASS |
| insert_clear, 128 | 1.0949 | PASS |
| insert_clear, 4096 | 0.9168 | PASS |
| interleaved_miss, 128 | 0.8582 | PASS |
| interleaved_miss, 4096 | 0.8813 | PASS |
| miss, 128 | 0.2655 | PASS |
| miss, 4096 | 0.1647 | PASS |
| multi_equal_range, 4096 | 1.0637 | PASS |
| multi_insert_clear, 4096 | 0.9816 | PASS |
| node_transfer_interior, 4096 | 1.0229 | PASS |
| node_transfer_roundtrip, 4096 | 0.4843 | PASS |

정수 키의 삽입·삭제, hit, 상한 밖 miss, 내부 빈 키 miss, 중복 범위 및 노드 왕복 전송을 측정했다. 문자열·UUID의 모든 분포, 다른 OS, 프레임 시간 향상에 대한 측정은 아니다.

## 원인 분리와 최적화

초기 AVL 기반 노드 왕복은 약 1.45배였다. 노드 핸들 16바이트, 삽입 결과 32바이트로 STL과 크기가 같아 크기 차이는 원인이 아니었다. 디스어셈블과 실제 계측을 비교하며 새 map 확장만 red-black tree로 변경하고 전송 시 extrema 위치를 재사용했다. 진단 단계 최소 노드 전송은 약 0.53배, 내부 노드는 약 1.07배였다. 최초 고정 캠페인의 소형 삽입 약 1.11배 미달은 노드 생성의 명시적 예외 처리 경계를 RAII pending-node guard로 변경한 뒤 진단에서 약 1.06배로 개선했다.

실패 캠페인, 원본 바이너리·입력 해시, 디스어셈블과 원시 CSV를 보존했다. 최종 판정은 변경 후 독립된 세 반복 전체이며 실패 표본을 걸러내거나 캠페인을 이어 붙이지 않았다. 하드웨어 PMU 계측은 수행하지 않아 캐시/분기 stall을 단일 확정 원인으로 주장하지 않는다.

기존 전체 82항목 및 PPL 비교는 이번에 재측정하지 않았다. 기존 원시 결과 266파일과 고정 입력 131파일, 이전 ordered set 성능 자료를 해시 대조해 보존을 확인했다. 이번 신규 수용 범위는 새 ordered map 확장이다.

## 빌드와 실행 검증

- 네이티브 Debug 1,095,556건, Release 1,095,552건 통과. 8,192개 모델 연산과 생존 노드 주소, 문자열 view 조회, allocator 도메인, 추출·롤백·소유권을 검사했다.
- ASAN 1,095,552건 통과. mimalloc C 구현 자체는 ASAN 계측하지 않았다. arena 하위 할당 경계는 별도 버퍼 덮어쓰기·도메인 검사로 검증했다.
- 리플렉션 컨테이너 값 왕복 37축, 실패 0.
- 실제 CreatorEditor Debug 빌드 성공. Editor 17명령, 컨테이너 검사 2회 × 8,192전송, 새 GPU 프레임, Play/Stop과 장면 복원 및 정상 종료 통과.
- 실제 DX12 Debug 8,786건, Release 8,781건 통과. GPU validation, 64개 컴포넌트와 4프레임 검사. 할당/해제 계수 검사는 Debug에서 수행했다.
- 외부 독립 테스트 솔루션 Release 빌드 성공. 전체 Vulkan/Audio 경로 실행을 뜻하지 않는다.

## 보존과 근거

90개 소비 파일을 전환 전 원본과 대조해 타입·import 연결 변경임을 확인했다. 별도 검사 연결 2파일을 제외한 원본 소스 1,407개(미추적 16개 포함)는 동일하다. 미추적 Effect 작업도 보존했다.

근거 디렉토리: Build/Verification/ContainerMigration/OrderedMapBatch20261028

- performance-acceptance.json, accepted-inputs.json, accepted-*-core.csv, accepted-*-nodes.csv
- native-accepted/result.json, asan-final.log, reflection-roundtrip.log
- editor-final/acceptance.json, material-final/result.json
- consumer-equivalence.json, unrelated-preservation.json, baseline-performance-retained.json
- remaining-final/classification.json, migration-batch.json

남은 저장 참조 51건은 deque 34, adaptor 8, unordered map 계약 7, 외부 ABI 2다. 문자열과 callable은 대기 범위를 유지한다.