# 배열·비트셋 61건 배치 전환

문자열 타입은 유지하고, 앞선 경계 분류에서 지정한 배열·비트셋 타입 참조 61건을 엔진 소스 8개 파일에서 한 배치로 전환했다. 성능 수용을 통과한 `Engine/Containers` 구현 62개 파일은 변경하지 않았다. 미추적 Effect 파일과 다른 미추적 소스는 삭제·초기화·스테이징하지 않고 작업 전 사본과 해시를 보존했다.

## 변경 범위

| 변경 | 참조 수 | 파일 |
|---|---:|---|
| `std::vector` → `ce::dynamic_array` | 58 | EffectDefinition.h/cpp, EffectModuleIR.h/cpp, EffectState.h, EffectFrame.h |
| `std::array` → `ce::fixed_array` | 1 | EffectFrame.h의 16 float 행렬 |
| `std::bitset` → `ce::fixed_bitset` | 2 | Creator.Core.DynamicBitset.ixx의 64비트 블록, RHISoftwareRecorder.cpp의 8비트 검사 |

엔진 소스의 작업 전·후 차이는 위 타입 토큰 치환뿐인지 확인했다. 문자열·DefinitionId·정렬 맵·집합·deque·소유자 타입과 알고리즘은 유지했다. API 인수·반환과 중첩 배열을 함께 바꾸고, 실제 소비자인 `EffectDefinitionTests.cpp`의 진단 배열 타입을 맞췄다. DynamicBitset 소스 도구 어댑터는 생성 스크립트로 갱신했다.

배열은 값을 소유하며 제거 시 값의 소멸자를 실행한다. `clear` 후 예약된 배열 저장소는 유지한다. 조회에 새 할당을 추가하지 않고, 삭제한 값이 소유하던 문자열 버퍼는 그 시점에 해제한다. 성장 시 용량 수열·총 할당 바이트 수까지 MSVC vector와 같다는 주장은 하지 않는다. 기존 합격 dynamic_array의 성장 정책을 사용한다.

EffectState의 owner thread 제한, 세대 핸들·scene epoch, 불변 프레임 공동 소유, 버스트 승인·보관·과부하 거부, Shutdown 순서와 슬롯 재사용 흐름은 변경하지 않았다. 배열 원소 주소를 성장 이후까지 고정한다는 계약을 새로 만들지 않았다.

## CPU·모듈 회귀

| 검증 | Debug | Release |
|---|---:|---:|
| Effect 정의·진단·샘플링 | 54건 PASS | 54건 PASS |
| Effect IR 컴파일·평가·롤백 | 274건 PASS | 274건 PASS |
| Effect 상태·수명·배열 회귀 | 65건 PASS | 62건 PASS |
| 실제 DynamicBitset 모듈 소비 | 8개 경계 × 2 초기값 PASS | 동일 PASS |

DynamicBitset 검사는 0/1/63/64/65/127/128/129 크기에서 assign·성장·축소·추가·비트 변경·copy/move·padding/count와 범위 초과 거부를 확인했다. 테스트는 named module을 실제로 import한다.

Effect 상태 검사에 다음 검증을 추가했다.

- 프레임 배열을 4,096회 조회하는 동안 Debug CRT 후크에서 호출 스레드 할당 0회.
- 96자 문자열 두 개를 담은 실제 EffectFrame 배열에서 erase와 clear를 수행해, 제거 대상 문자열 버퍼 주소가 각각 정확히 1회 해제됐는지 확인.
- clear 이후 배열의 data·capacity가 유지되고, erase 후 생존 값과 16 float identity 행렬이 올바른지 확인.
- 문자열 타입은 `std::string`, transform 크기는 16 float임을 컴파일 시 확인.
- 인스턴스 해제·생산자 Shutdown 후에도 보유 중인 프레임의 문자열·배열 값이 유효하며, 마지막 프레임 소유자를 놓으면 weak owner가 만료됨을 확인.
- 기존 불변 다중 뷰, stale generation/epoch 거부, 스레드 소유권, 버스트 승인·오래된 승인 격리와 한도 초과 거부 회귀도 유지.

Debug에서 검사 3건이 더 많은 이유는 CRT 할당·문자열 버퍼 해제 검사다. 초기 계측은 전체 CRT free가 1회라는 가정을 사용해 실패했다. 실제 erase/clear의 전체 free는 각각 2회였고, 계측을 수정해 제거 대상 문자열 버퍼의 주소를 직접 추적했다. 최종 결과는 각 버퍼 해제 1회다. 다른 CRT 관리 메모리의 해제를 문자열 버퍼 해제로 세지 않는다. 초기 실패 로그도 `cpu` 디렉토리에 보존했다. 정본 구현을 이 검사에 맞춰 변경하지 않았다.

CPU 증거는 `Build/Verification/ContainerMigration/ArrayBitsetBatch20261027/definition`, `ir`, `cpu-verified` 아래의 결과·구성별 빌드·실행 로그다.

## 실제 DX12 GPU 검사

기존 EffectSimulationGpuProbe를 현재 경로 모듈 API로 복구했다. 삭제된 `PathFinder.h`와 InternalPath 호출 때문에 최초 빌드가 실패했으며, 현재 `Creator.Core.Paths`와 동일한 경로 설정을 사용하도록 하네스만 수정했다. 시뮬레이션 커널·수치 기준은 바꾸지 않았다.

실제 DX12 그래프·compute·제출·완료·readback 경로에서 **60조건, 1,082,120성분, barrier 300건, validation 오류 0건**을 통과했다. GPUValidation을 켰으며 CPU 기준값을 의도적으로 바꾸는 음성 검사는 예상대로 실패했다. 이 결과는 기존 고정 커널의 수치·RHI 검증이며 임의 IR 그래프의 GPU lowering 검증은 아니다.

최종 증거는 `Build/Verification/ContainerMigration/ArrayBitsetBatch20261027/gpu-verified/result.json`과 positive/negative 실행 로그다. 최초 하네스 빌드 실패는 `gpu/build.log`에 보존했다.

## 실제 엔진 빌드·실행

Effect 패키지를 포함한 Debug Editor를 최종 소스로 빌드하고 기존 17명령 제품 검증을 통과했다. 실제 새 GPU 프레임, 렌더 프록시, 두 번의 8,192건 전송, Play/Stop·장면 복원과 정상 종료를 확인했다. Effect package의 초기화·종료가 각각 한 번이고 tick 13회·snapshot 18회·HDR 선언/기록 각 35회임을 확인했다. 이는 제품 프레임 전달 관측이며 Editor에서 임의의 Effect 그래프를 실행했다는 뜻은 아니다.

제품 증거는 `Build/container-array-bitset-editor.log`, `Build/Verification/ContainerMigration/ArrayBitsetBatch20261027/editor/acceptance.json` 및 `effect-product-observations.json`이다. GPU 고정 커널 검사와 Editor 프레임 전달 관측은 서로 다른 범위이며 모두 완료 결과를 기록한다.

## 정본 유지와 잔여량

기존 `20261027-tailhint-full1`의 원시 258개 조건/프로세스 결과(각 15표본)와 입력·바이너리 해시를 재검증했다. 일반 82/82, MPSC 단일·주 기준 각 4/4, FAA/PPL 6/6, 기타 큐 8/8 PASS를 유지했다. 성능 표본을 새로 수집하거나 이번 Effect 사용처의 프레임 속도 개선을 주장하지 않는다. 기록은 외부 `CreatorContainers/results/20261027-array-bitset-baseline-reverify.log`다.

같은 감사 기준에서 명시적 STL 참조는 **9,396 → 9,335건**, 후보 파일은 969개다. 문자열 참조 8,379건은 그대로이고 함수 래퍼 264건도 그대로다. 저장 컨테이너는 **753 → 692건**이다. 남은 vector 2건은 reflgen SDK 반환 경계이며 이번 61건에는 포함하지 않았다. 기존 배열·비트셋 전환 후보는 모두 닫혔다.

잔여 분류는 `Build/Verification/ContainerMigration/ArrayBitsetBatch20261027/remaining-boundaries/classification.json`과 CSV에 있다. 정렬·deque의 설계 필요 범위와 외부 ABI는 별도로 유지한다. 전체 컨테이너 전환 완료를 뜻하지 않는다.

작업 전 사본·해시는 `Build/Verification/ContainerMigration/ArrayBitsetBatch20261027/before`와 `before.json`에 있다. 정본 해시·최종 입력·검증 결과는 같은 배치의 최종 기록에 묶었다. 이전 분류: [문자열 제외 컨테이너 경계](NonStringContainerBoundaries20261027.html).

다음 단계: [해시 맵 194건 배치 전환](HashMapBatchMigration20261027.html). 해당 단계 이후 잔여 저장 컨테이너 참조는 498건이다.
