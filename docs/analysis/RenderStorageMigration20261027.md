# 렌더 저장소 STL 사용처 전환과 제품 검증

> 다음 사용처 전환: [임시 GPU 버퍼 풀 STL 전환과 소유권 검증](SceneBufferPoolMigration20261027.html). 성능 정본을 유지한 최신 전환과 명시 참조 9,400건의 집계를 확인할 수 있다.

성능 수용을 통과한 `Engine/Containers` 구현은 수정하지 않고 렌더 사용처 두 곳을 전환했다. 기존 일반 82/82, MPSC 단일·주 배치 각각 4/4, FAA/PPL 6/6, 기타 동시성 큐 8/8 및 별도 문자열 57/57의 정본을 유지한다. 이번 작업은 사용처의 동작과 할당 계약을 검증한 것이며 새로운 프레임 성능 향상을 측정한 결과는 아니다.

## 변경 범위

| 사용처 | 변경 | 보존한 계약 |
|---|---|---|
| `EnhancedGpuMeasurementHistory::DomainSamples` | `std::unordered_map` → `ce::node_hash_map` | 재해시 후 샘플 주소, 서명 공유 소유권, 복사한 샘플의 독립 수명, Normal/Capture 분리, 제한된 뷰 수와 실패 워터마크 |
| `EnhancedDrawSealLedger::bindings` | `std::unordered_map` → `ce::node_hash_map` | 프레임 초기화, 기존 바인딩의 주소, pipeline·texture 충돌 판정, descriptor 버전을 신원에서 제외하는 규칙 |

두 맵 모두 `PoolNodes=false`를 명시했다. 삭제할 때 노드 저장소를 즉시 해제하고, `clear` 이후에는 인덱스 용량만 재사용하는 기존 정책을 보존하기 위해서다. 값 안의 기존 소유자·배열·문자열 타입은 변경하지 않았다. 이력은 기존 생산자 스레드 규칙을 따르며, 장부의 병렬 drop 계수 경로도 그대로다. 해시 순회 순서는 공개 계약이 아니다. 가장 오래된 두 뷰의 시간이 같을 때는 그중 하나를 제거하며 특정 뷰 신원은 지정하지 않는다. 정확한 버킷 크기나 총 할당 바이트가 STL과 동일하다는 주장은 하지 않는다.

## 수명과 할당 검증

실제 엔진 헤더를 포함하는 격리 검사에서 다음을 확인했다.

- 재해시 전 조회한 샘플과 바인딩 주소가 유지된다.
- 호출자의 원본 소유자를 해제해도 이력이 서명을 소유한다. 이력 초기화 뒤에도 복사한 샘플은 데이터를 소유하며, 그 복사본까지 초기화하면 해당 강한 참조가 해제된다.
- 만료·도메인별 축출과 `Discard`는 소유자를 즉시 해제하면서 실패 워터마크를 유지한다. 늦은 성공 결과가 실패한 이력을 되살리지 않는다.
- 동률을 포함한 최소 시각 축출 규칙과 기존 프레임 충돌 판정이 유지된다.
- Debug CRT 후크로 1,000회 반복의 이력 hit/miss/fresh 조회와 장부의 기존 바인딩 조회에서 추가 할당 0회를 확인했다.
- 장부의 한 노드 삭제 시 해제 1회, 프레임 초기화 시 남은 노드 수만큼 해제, 다음 프레임 첫 삽입 시 기존 인덱스를 재사용하며 할당 1회를 확인했다.

동일 검사를 임시 헤더에서 이전 `std::unordered_map` 저장소로 되돌린 대조군에도 실행했다. 노드 풀 보존을 다시 켜는 결함 주입은 즉시 노드 해제 검사에서 실패해야 한다. 이 검사들은 원래 구현 파일을 변경하지 않고 임시 복사본으로 수행한다. Debug와 Release의 검사 수는 CRT 후크 검사가 Debug에만 있기 때문에 다르다.

최종 결과는 Debug 88건, Release 83건 통과이며, 기존 STL 대조군 Debug 88건 통과와 노드 풀 보존 결함 검출도 확인했다.

최종 격리 증거: `Build/Verification/ContainerMigration/RenderStorage20261027verified/result.json`, `Build/container-render-storage-verified.log`. 생성 헤더 경로 검사이며 실제 엔진의 명명 모듈 경로는 아래 제품 실행으로 별도 확인했다.

## 실제 엔진 빌드와 실행

- Debug Editor를 새 헤더와 함께 실제로 재빌드했다: `Build/container-render-storage-editor.log`.
- 기존 17명령 통합 실행에서 두 번의 8,192건 전송, 렌더 준비, Play/Stop·장면 복원, 물리 저장소와 힙 정책 검사가 통과했다. 새 장부의 재해시 참조 유지·충돌·프레임 초기화 검사도 실제 명명 모듈 소비자에서 통과했다: `Build/Verification/ContainerMigration/RenderStorageDebug1/acceptance.json`.
- 기존 Editor의 RG8 계획 진단 82건이 통과했다. 도메인 분리, 과거 프레임·시각 거부, 제한된 뷰 축출, 서명별 이력과 실패 워터마크 검사를 포함한다: `Build/Verification/ContainerMigration/RenderStorageRg8Debug1/schedule-result.json`. 이 진단의 `nativeQueueExecutionTested=false`는 그대로이며, 네이티브 다중 큐 실행 성능 검증으로 해석하지 않는다.

## 성능 정본과 남은 전환

정본 파일 62개의 저장된 해시가 일치했고, 기존 성능 판정도 원시 258개 조건/프로세스 결과와 입력 해시를 다시 검증해 PASS임을 확인했다. 새로 성능 표본을 수집한 것은 아니다. 재검증 기록은 외부 `CreatorContainers/results/20261027-render-storage-baseline-reverify.log`, 기준 판정은 `20261027-tailhint-full1-verdict.json`이다. 컨테이너 모듈·생성 헤더는 이번 전환에서 바뀌지 않았다.

명시적 STL 참조 감사는 9,404 → **9,402건**, `unordered_map`은 208 → **206건**이다. 후보 파일은 971개이며, 외부 코드·생성물·정본 컨테이너 구현과 매크로 확장은 이 집계에 포함하지 않는다. 전체 전환은 아직 진행 중이다. 감사 결과: `Build/Verification/ContainerMigration/inventory-20261027-render-storage.json`.
