# 임시 GPU 버퍼 풀 STL 전환과 소유권 검증

성능 수용을 통과한 `Engine/Containers` 구현 62개 파일은 수정하지 않고, `MaterialGraphTransientBufferPool.h`의 소유 ID 맵과 배치 상태 비트셋을 전환했다. 기존 성능 합격 자료는 원시 결과와 입력 해시를 재검증했으며 성능 표본을 새로 수집한 것은 아니다. 이번 작업은 버퍼 소유권·할당·해제 계약과 실제 DX12/Editor 실행을 검증한다.

## 변경과 보존 조건

| 사용처 | 변경 | 보존한 조건 |
|---|---|---|
| `AcquireBatch`의 `missing`, `capacityMiss` | `std::bitset<Count>` → `ce::fixed_bitset<Count>` | 초기 상태 0, 고정 크기·무할당 저장소, 요청별 생성/실패 표시 |
| `owned` | `std::unordered_map<uint32_t, Key>` → `ce::node_hash_map` | 정수 ID와 Key의 값 소유, 조회 시 임시 키 할당 없음, 노드 삭제 시 즉시 저장소 해제 |

맵에는 `PoolNodes=false`를 명시했다. 버퍼가 캐시에 반환됐을 때는 추적 노드를 유지하고, 예산 초과나 종료 후 반환으로 실제 풀을 떠날 때 노드를 해제하는 기존 정책을 유지한다. 이 맵은 GPU 객체 자체를 소유하는 스마트 포인터가 아니라 ID와 재사용 조건을 기록한다. 네이티브 객체와 GPU 완료 후 은퇴는 기존 장치·호출자 소유 흐름이 담당한다. 잠금 범위, 장치 생성·해제 호출 위치, 64MiB 캐시 예산, 닫힌 풀 처리와 예외 롤백 코드는 바꾸지 않았다. 노드·인덱스의 전체 바이트 수가 STL과 같다는 주장은 하지 않는다.

## 실제 DX12 회귀

기존 `MaterialRenderBindingsProbe`를 현재 컨테이너·소유자 API에 맞춰 정리했다. 진단 출력 배열과 배치 인수를 엔진 API 타입으로 맞췄고, 서명·리소스 소유자는 `own` 타입과 양쪽을 유지하는 객체 주소 비교를 사용했다. 엔진 API를 이전 테스트에 맞춰 바꾸지는 않았다. 하네스의 실행 의존 경로도 현재 공용 런타임과 중첩 패키지 경로에 맞췄으며, 새 증거 디렉토리를 지정할 수 있게 했다.

최종 결과는 **Debug 8,778건·Release 8,775건 통과**이다. 두 구성 모두 DebugLayer/GPUValidation을 켠 실제 DX12 장치에서 GPU 결과 64성분과 4프레임을 확인했다. 추가 풀 검사는 다음과 같다.

- 65개 요청으로 비트 64를 통과해 모든 핸들이 유효하고 서로 다른지 확인했다.
- 65개를 캐시에서 반복 재사용한 뒤에도 생성 수·소유 수가 유지된다. Debug CRT 후크에서 100회 배치 Acquire/Return의 호출 스레드 추가 할당이 0회였다. 다른 스레드나 프로세스 전체의 무할당을 뜻하지 않는다.
- 65개가 외부에 대여된 상태에서 `Close`해도 소유 추적은 유지된다. 이후 반환한 65개 버퍼가 각각 한 번씩 해제된다.
- 65번째 생성 요청을 의도적으로 거부하면 외부 반환 핸들은 전부 무효이며, 먼저 생성된 64개는 캐시로 반환된다. `Close`하면 이 64개가 해제된다.
- Debug에서는 닫힌 풀의 마지막 반환을 실제 장치 해제 호출 경계에서 관측했다. 장치의 네이티브 해제 함수에 들어가기 전에 추적 노드가 정확히 1회 해제됐다. 드라이버 내부 할당·해제는 이 CRT 측정에서 제외했다. 이는 GPU 실메모리의 물리적 반환 시간을 측정한 결과는 아니다.
- 기존의 96개 작은 버퍼 보존, best-fit, 사용 플래그 분리, 예산 초과 반납 및 실패 배치 롤백 검사도 통과했다.

Debug와 Release의 검사 수 차이는 Debug CRT 계측 세 검사 때문이다. 최종 증거는 `Build/Verification/ContainerMigration/SceneBufferPoolNative20261027verified/result.json`과 구성별 빌드·실행 로그이며, 요약은 `Build/container-scene-pool-native-verified.log`이다. 하네스의 앞선 API/의존 경로 오류 로그도 별도로 보존했다.

## 실제 엔진 빌드와 실행

변경한 풀을 포함해 Debug Editor를 재빌드했고, 기존 17명령 통합 검증을 실행했다. 두 번의 8,192건 전송, 실제 렌더 준비, Play/Stop·장면 복원 및 기존 물리·힙·장부 계약 검사가 통과했다. 제품 증거는 `Build/container-scene-pool-editor.log`와 `Build/Verification/ContainerMigration/SceneBufferPoolDebug1/acceptance.json`이다.

## 성능 정본과 남은 사용처

정본 62개 파일의 저장된 해시는 모두 일치한다. 기존 `20261027-tailhint-full1`은 입력·바이너리와 원시 258개 조건/프로세스 결과를 다시 검증해 일반 82/82, MPSC 단일·주 기준 각각 4/4, FAA/PPL 6/6, 기타 큐 8/8 PASS를 유지했다. 재검증 기록은 외부 `CreatorContainers/results/20261027-scene-pool-baseline-reverify.log`이다. 새 사용처의 프레임 성능 향상이나 전체 OS 성능을 측정한 것은 아니다.

명시적 STL 참조는 **9,402 → 9,400건**이다. `unordered_map`은 206 → 205건, `bitset`은 3 → 2건이며 후보 파일은 971개다. 외부 코드·생성물·정본 구현과 매크로 확장은 집계에서 제외했다. 전체 전환은 진행 중이며 감사 결과는 `Build/Verification/ContainerMigration/inventory-20261027-scene-pool.json`에 있다.

다음 단계: [DX12 루트 시그니처·샘플러 캐시 전환](NativeCacheMigration20261027.html).
