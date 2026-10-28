# DX12 PSO 캐시·비동기 대기 맵 전환

성능 수용을 통과한 `Engine/Containers` 구현 62개 파일은 유지하고, `DX12PSOManager`의 완성 캐시와 비동기 컴파일 대기 맵 두 곳을 `ce::node_hash_map`으로 교체했다. 두 맵에 `ce::heap_allocator, false`를 명시해 노드를 삭제할 때 즉시 해제한다. 기존 성능 합격 자료의 원시 표본과 해시를 재검증했으며 새 성능 표본은 수집하지 않았다.

## 소유권·수명과 변경 범위

| 사용처 | 값 | 보존한 계약 |
|---|---|---|
| `m_cache` | 네이티브 PSO COM 소유자·비소유 핸들 | 정수 키 조회, mutex, 동일 핸들 반환, 무효화 시 핸들 해제 후 PSO를 완료 대기 큐로 이동 |
| `PendingCompiles`, `m_pending` | `own::shared_owner<PendingCompile>` | 입력 바이트 복사, 동일 요청 중복 제거, 작업자와 공동 소유, 완료 후 게시, 종료·전체 무효화 시 대기 맵 교환 후 작업 완료 대기 |

생산 코드 수정은 `DX12PSOManager.h`의 두 타입 선언과 설명 주석에 한정했다. 잠금·스케줄러·네이티브 생성·은퇴 큐 코드는 변경하지 않았다. 대기 맵 순회 순서에는 완료 순서 계약이 없고 각 작업의 완료를 기다리는 기존 흐름을 유지한다.

노드 삭제와 PSO 객체 삭제는 별개다. 무효화 시 맵 노드는 즉시 해제하지만 PSO는 기존 `RHICompletionRetireQueue`에 남아 완료값에 따라 회수된다. 완료값 0은 격리 상태이며 일반 `Collect`에서는 회수하지 않고 종료 시 `Drain`에서 정리한다. 작업자도 복사한 공동 소유자를 보유하므로 대기 맵에서 항목을 제거해도 실행 중인 작업의 입력·결과 수명을 끊지 않는다. 인덱스 용량은 삭제 후 유지할 수 있으며 STL과 동일한 총 할당 바이트 수를 주장하지 않는다.

## 실제 네이티브 PSO 검증

기존 `EditorMeshPsoProbe`를 확장하고 `verify-pso-container-migration.ps1`에서 Debug·Release를 빌드·실행했다. 실제 DX12 장치와 드라이버 PSO 생성 경로이며 DebugLayer·GPUValidation을 켰다.

- 작업자 하나를 게이트로 멈춘 상태에서 동일 요청을 4,096회 반복했다. 모든 요청이 Pending이고 제출은 한 번뿐이다. Debug CRT 후크로 호출 스레드의 추가 할당 0회를 확인했다.
- 요청 제출 후 호출자가 제공한 셰이더 버퍼를 0으로 덮어도 작업자 컴파일과 핸들 게시가 성공했다. 대기 항목이 입력을 복사해 소유하는 기존 계약을 확인했다.
- 완성된 캐시를 4,096회 조회해 같은 핸들과 컴파일 수를 유지했다. Debug CRT 후크에서 호출 스레드 할당은 0회였다.
- 단일 무효화 구간에서 추적 노드 해제가 정확히 1회 관측됐다. 기존 핸들은 무효이고 PSO는 완료 대기 큐에 남았다. 완료값 6에서는 회수 0건, 7에서는 1건이었다. 이 PSO는 GPU에 제출하지 않았으며 합성 완료값으로 정책을 검증한 것이다.
- 재생성 핸들의 세대가 달라졌다. 추가 컴파일이 대기 중인 전체 무효화는 작업을 배출한 뒤 재요청을 허용했다. 완료값 0으로 은퇴한 PSO는 최대 완료값으로 일반 수집해도 남았다.
- 잘못된 입력은 핸들을 게시하지 않고 실패했다. 마지막 대기 요청을 남긴 채 종료해 작업 완료 대기와 격리 큐 정리가 정상 종료까지 이어졌다.

결과 표식은 두 구성 모두 `pendingHits=4096 readyHits=4096 ownership=1 quarantine=1`이다. Debug에는 `allocationChecks=2 nodeFreeChecks=1`, Release에는 해당 CRT 검사 0건이 기록된다. 무할당 주장은 측정한 호출 스레드의 반복 조회 구간에 한정한다.

최종 증거는 `Build/Verification/ContainerMigration/PsoMaps20261027final/result.json`과 구성별 빌드·실행 로그이며 요약은 `Build/container-pso-maps-final.log`다. 실행 파일 및 입력 소스 해시도 기록했다.

## 실제 엔진 빌드·실행

Debug Editor를 최종 소스로 빌드하고 17명령 제품 검증을 통과했다. 새 GPU 프레임 완료, 렌더 프록시 적용, 두 번의 8,192건 전송, Play/Stop·장면 복원 및 정상 종료를 확인했다. 빌드 로그는 `Build/container-pso-maps-editor.log`, 실행 증거는 `Build/Verification/ContainerMigration/PsoMapsEditor20261027/acceptance.json`이다. 네이티브 테스트의 합성 완료값 검사와 실제 제품 GPU 프레임 검증은 별도 증거다.

## 성능 정본과 남은 전환

정본 62개 파일 해시, 26개 모듈 헤더 동기화, 31개 소스 도구 어댑터 검사가 통과했다. 기존 `20261027-tailhint-full1`의 원시 258개 조건/프로세스 결과(각 15표본)와 입력·바이너리 해시를 재검증해 일반 82/82, MPSC 단일·주 기준 각 4/4, FAA/PPL 6/6, 기타 큐 8/8 PASS를 유지했다. 기록은 외부 `CreatorContainers/results/20261027-pso-maps-baseline-reverify.log`다. 이번 사용처 변경에 따른 프레임 성능 향상이나 전체 OS 성능을 새로 측정한 것은 아니다.

동일 감사 기준의 명시적 STL 참조는 **9,398 → 9,396건**, 후보 파일은 971개다. 추적·미추적 소스를 포함하며 외부 코드·생성물·정본·매크로 확장은 제외한다. 전체 전환은 진행 중이다. 감사 결과는 `Build/Verification/ContainerMigration/inventory-20261027-pso-maps.json`이다.

이전 단계: [루트 시그니처·샘플러 캐시 전환](NativeCacheMigration20261027.html).

후속 분류: [문자열 제외 컨테이너 잔여 경계](NonStringContainerBoundaries20261027.html).
