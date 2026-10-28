# 해시 집합 92건 전환과 실제 엔진 검증 — 2026-10-28

기존 합격 컨테이너 정본과 문자열 타입을 유지하면서 `std::unordered_set` 92개 참조를 38개 파일에서 `ce::engine_node_set`으로 전환했다. 현재 감사 범위 Engine·Editor·Lattice·Player·HeadlessHost에서 해당 표준 집합 참조는 0건이다. 전체 저장 컨테이너 전환이 끝난 것은 아니다.

## 구현과 변경 범위

새 `Engine/Utility_Framework/Foundation/EngineNodeSet.h`는 기존 `EngineNodeMap.h`와 같은 엔진 정책 어댑터다. 합격한 `Creator.Containers` 모듈의 `node_hash_map<Key, present, Hash, Equal, heap_allocator, false>`를 보유하고, 반복기에서 불변 키만 노출한다. 탐색·해시 인덱스·노드 수명 알고리즘을 복제하지 않았다. 이 정책 어댑터는 헤더이며, 새로운 독립 `.ixx` 구현은 추가하지 않았다.

키를 값으로 소유하고 표준 키 해시·비교 정책을 유지한다. 기존 std::string/std::wstring 타입도 그대로다. 반복자는 forward iterator이며 수정 가능한 키를 노출하지 않는다. reserve/rehash는 키 주소를 유지하되 반복자를 무효화할 수 있다. 순회 순서 보장은 추가하지 않는다.

비풀링 노드 정책을 명시해 erase는 해당 노드를 즉시 해제하고 clear는 모든 원소를 파괴·해제한다. 인덱스 용량은 남는다. 요소가 포인터이면 대상 소유권은 그대로 호출자에게 있고, 공동 소유 키이면 마지막 소유자가 사라질 때 대상이 파괴된다. 참조·뷰 조회는 호출자 버퍼 수명을 연장하지 않으며 조회 중 키를 저장하지 않는다.

Scene·SceneManager의 이름/순회 방문 집합, 렌더·셰이더 준비 집합, Audio ClipKey 집합, 저작 문서 키 검사, Editor 자산·계층 집합, Lattice 그래프 방문/핀 집합을 함께 전환했다. 추출·재키잉 노드 맵과 정렬·중복 키 컨테이너, deque는 이번 배치 대상이 아니다.

## 소유권·할당·실패 처리 검사

| 검사 | 실제 결과 |
|---|---|
| named-module Debug | 533,532 checks |
| named-module Release | 533,529 checks |
| standalone ASAN | 533,529 checks, 종료 코드 0 |
| 리플렉션 실제 값 왕복 | 37 axes, failures=0 |

충돌 해시를 고정한 8,192개 삽입·삭제 연산을 MSVC STL 모델과 비교했다. 긴 문자열 hit/miss·count·부분 뷰 조회 8,192회에서 Debug CRT 호출 스레드 할당은 0회였다. 종료 문자가 없는 뷰, 내부 NUL, 원본 버퍼 변경 후 키 보존과 rehash 후 키 주소 유지를 확인했다.

복사 노드의 독립 소유, 이동·swap, erase 반복기·범위 삭제, 중복 키 억제, 공동 소유자의 잔존과 최종 clear 파괴를 검사했다. 키 복사 예외 후 기존 노드·크기·키 수명이 보존되고 누출이 없음을 검사했다. Debug에서 정수 키 단일 erase는 노드 해제 1회·할당 0회, 32개 clear는 노드 해제 32회였다. Release·ASAN은 CRT 할당 검사 증거가 아니다.

초기 검사에서 짧은 std::string 키의 해제를 노드 하나로만 기대한 단정이 실패했다. Debug 문자열의 추가 소유 자원 때문에 노드 해제만 분리할 수 없어 정수 키 전용 단정으로 고쳤고, 실패 로그는 `native1`에 남겼다. 구현을 이 단정에 맞춰 바꾸지는 않았다. 기존 호출자가 임시 문자열을 만드는 경로까지 무할당이라고 주장하지 않는다.

## 실제 엔진 빌드·실행

Debug Editor를 Effect 패키지와 static-CRT launcher까지 빌드했다. 최종 소스를 다시 확인한 빌드 후 제품을 실행해 17개 명령 모두 통과했다. 두 번의 8,192건 전송, 새 GPU 프레임 완료, 렌더 프록시 게시, Play/Stop, 장면 객체·변환 복원과 정상 종료를 확인했다.

`containers.integration.probe`에 `nodeSetStorage` 검사를 추가하고 실행 검증 스크립트가 그 결과를 필수로 확인하게 했다. 실제 Editor 안에서 긴 소유 키·부분 뷰 조회, rehash 주소, 복사와 삭제를 두 번 검사했다. 기존 UTF-16·물리·힙·draw seal 검사도 유지했다.

실제 DX12 장치 회귀는 Debug 8,786건, Release 8,781건을 통과했다. GPUValidation을 켜고 64성분·4프레임을 확인했으며 Debug의 warmed pool/native cache 조회 할당·삭제 전 해제 단정도 통과했다. 모든 Audio/Vulkan 실행 경로를 실행한 결과로 확대하지 않는다.

## 합격 정본과 다른 작업 보존

합격 정본 66개 파일은 해시까지 동일하다. 기존 `20261027-variance-window-full1` 성능 수용의 원시 파일 266개와 입력 131개도 해시가 일치한다. 이번에는 새 속도 계측이나 전체 성능 재실행을 하지 않았다. 새 집합 어댑터의 사용처별 속도·메모리 사용량이나 프레임 성능 향상을 주장하지 않는다.

38개 소비자 파일은 수정 전 사본과 대조해 집합 타입과 include만 바뀌었음을 확인했다. 문자열 리터럴과 기존 소스 인코딩을 보존했다. 별도 변경은 실제 실행 검사와 리플렉션 검사 연결이다. 변경 허용 목록 밖 1,457개 소스(미추적 16개 포함)가 해시까지 유지됐다. 미추적 Effect 작업도 보존했다. 변경 전 사본 기준 42개 파일의 새 공백 오류는 0건이다.

## 남은 전환

| 분류 | 타입 참조 |
|---|---:|
| 정렬 map/set/multi | 355 |
| deque | 34 |
| stack/queue/priority_queue | 8 |
| 별도 노드 추출 맵 | 7 |
| reflgen SDK vector 경계 | 2 |
| 저장 컨테이너 합계 | **406** |

저장 컨테이너는 498→406, 전체 감사 참조는 9,145→9,055다. 문자열 8,385건은 기존 8,383건에 실행 검사 선언 2개가 더해진 수치이며 문자열 타입 교체는 없다. 함수 래퍼 264건과 값 타입 allocator 경계 1건도 남아 있다. 이는 추적·미추적 파일을 합친 어휘 참조 수이고 객체 수가 아니다.

## 증거

- [네이티브 검사](../../Build/Verification/ContainerMigration/HashSetBatch20261028/native-accepted/result.json), [실제 Editor 실행](../../Build/Verification/ContainerMigration/HashSetBatch20261028/editor-final/acceptance.json), [실제 DX12](../../Build/Verification/ContainerMigration/HashSetBatch20261028/material-final/result.json)
- [소비자 원본 대조](../../Build/Verification/ContainerMigration/HashSetBatch20261028/consumer-equivalence.json), [다른 작업 보존](../../Build/Verification/ContainerMigration/HashSetBatch20261028/unrelated-preservation.json), [성능 합격 증거 보존](../../Build/Verification/ContainerMigration/HashSetBatch20261028/performance-retained.json)
- [잔여 분류](../../Build/Verification/ContainerMigration/HashSetBatch20261028/remaining-accepted/classification.json), [최종 manifest](../../Build/Verification/ContainerMigration/HashSetBatch20261028/migration-batch.json)
- 빌드: `Build/hash-set-editor-build1.log`, `Build/hash-set-editor-final-build.log`. ASAN·리플렉션: 배치 디렉토리의 `asan-final.log`, `reflection-roundtrip.log`.
- 재현: `Tools/regression/verify-hash-set-migration.ps1`, `Tools/regression/verify-container-integration.ps1`, `Tools/regression/verify-material-render-bindings.ps1`.
