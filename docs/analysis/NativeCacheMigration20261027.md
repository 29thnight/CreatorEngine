# DX12 루트 시그니처·샘플러 캐시 STL 전환

성능 수용을 통과한 `Engine/Containers` 구현 62개 파일은 변경하지 않고, DX12 루트 시그니처 캐시와 단일 샘플러 캐시의 맵 두 곳을 `ce::node_hash_map`으로 전환했다. 기존 성능 합격 표본과 해시를 다시 검증했으며 성능을 새로 측정한 작업은 아니다.

## 변경과 소유권

| 사용처 | 저장 값 | 보존 조건 |
|---|---|---|
| `DX12RootSignatureCache::m_cache` | 루트 시그니처 COM 소유자와 비소유 레이아웃 핸들 | 기존 mutex, 내용 해시, 이중 조회, 동일 핸들 반환, 캐시 종료 시 COM 참조 해제 |
| `DX12SamplerHeap::m_cache` | GPU 디스크립터 핸들 값 | 기존 mutex, 동일 설정 중복 제거, 용량 제한, 힙 수명과 핸들 유효기간 |

두 맵 모두 `ce::heap_allocator, false`를 명시해 노드 풀을 사용하지 않는다. `clear`와 삭제는 값의 소멸자를 실행하고 노드를 즉시 해제한다. 해시 인덱스 용량은 기존 STL처럼 `clear` 후 유지할 수 있으며 전체 바이트 수가 STL과 동일하다는 뜻은 아니다. 조회 키는 `uint64_t` 값이며 임시 문자열을 만들지 않는다.

루트 시그니처의 리소스 테이블 항목은 비소유 포인터다. 캐시 종료가 기존 핸들의 수명을 연장하지 않으며, 종료 이후 핸들을 다시 사용하지 않는다. 샘플러 핸들도 힙을 소유하지 않는다. 네이티브 생성·잠금·실패 경로와 종료 순서는 변경하지 않았다. 범위 샘플러의 `m_rangeCache`는 이번 범위에서 제외했으며 여전히 임시 배열 키를 생성하는 별도 경로다.

## 실제 DX12 검증

`MaterialRenderBindingsProbe`의 실제 DX12 장치에서 Debug **8,786건**, Release **8,781건**을 통과했다. DebugLayer와 GPUValidation을 켜고 기존 GPU 64성분·4프레임, 풀 수명·실패 롤백 검증도 유지했다.

- 독립 캐시마다 한 항목을 생성한 후 4,096회씩 조회했다. 반환 핸들·생성 수·캐시 크기가 유지됐다.
- Debug CRT 후크에서 이 반복 조회 구간의 호출 스레드 할당은 **0회**였다. 프로세스 전체 또는 범위 샘플러 조회의 무할당을 뜻하지 않는다.
- 네이티브 루트 시그니처와 샘플러 힙을 별도 COM 소유자로 유지한 상태에서 각각 `Shutdown`했다. 각 캐시에서 노드 해제가 정확히 **1회** 관측됐으며 캐시 크기는 0, 초기화 상태는 false가 됐다. 네이티브 객체의 최종 해제로 인한 드라이버 동작을 이 관측과 분리했다.
- 별도 COM 소유자는 종료 후에도 유효했고, 네이티브 장치 조회·힙 설명 조회가 성공했다. 종료된 캐시의 비소유 핸들은 이후 사용하지 않았다.

최종 결과와 소스 해시는 `Build/Verification/ContainerMigration/NativeCache20261027final/result.json`, 빌드·실행 요약은 `Build/container-native-cache-final.log`에 있다. 최종 정리 전 검증 기록도 별도로 보존했다.

## 실제 엔진 빌드·실행

최종 소스로 Debug Editor를 재빌드하고 기존 17명령 제품 검증을 통과했다. 실제 GPU 프레임 완료, 두 번의 8,192건 컨테이너 전송, 렌더 프록시 적용, Play/Stop과 원본 장면 복원, 정상 종료를 확인했다. 빌드 로그는 `Build/container-native-cache-editor-final.log`, 실행 증거는 `Build/Verification/ContainerMigration/NativeCacheEditor20261027final/acceptance.json`이다.

## 성능 정본과 남은 사용처

저장된 정본 62개 파일의 해시가 모두 일치하고, 26개 모듈 헤더 동기화 및 31개 소스 도구 어댑터 검사가 통과했다. 기존 `20261027-tailhint-full1`의 원시 258개 조건/프로세스 결과(각 15표본)와 입력·바이너리 해시 재검증 결과는 일반 82/82, MPSC 단일·주 기준 각 4/4, FAA/PPL 6/6, 기타 큐 8/8 PASS다. 기록은 외부 `CreatorContainers/results/20261027-native-cache-baseline-reverify.log`에 있다. 새 캐시 사용처의 프레임 성능이나 GPU 메모리의 물리적 반환 시간을 계측한 결과는 아니다.

추적·미추적 파일을 포함한 동일 감사 기준에서 명시적 STL 참조는 **9,400 → 9,398건**, 후보 파일은 971개다. 외부 코드·생성물·정본과 매크로 확장은 집계에서 제외한다. 전체 전환은 계속 진행 중이다. 감사 증거는 `Build/Verification/ContainerMigration/inventory-20261027-native-cache.json`이다.

이전 단계: [임시 GPU 버퍼 풀 전환](SceneBufferPoolMigration20261027.html).

다음 단계: [PSO 캐시·비동기 대기 맵 전환](PsoMapMigration20261027.html).
