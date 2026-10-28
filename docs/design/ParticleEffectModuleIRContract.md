# 파티클 모듈 IR 계약 v1

> 2026-10-24 · PHASE 10-2a / 10-2b-1 · 작은 그래프 변환기와 CPU 기준 평가기

[전체 계획](../plans/ParticleEffectRedesignPlan.md) · [저작 정의](ParticleEffectAuthoringContract.md) · [프레임 전달](ParticleEffectTransportContract.md) · [10-2a 수용 기록](../analysis/Phase10ModuleIRAcceptance20261024.json) · [시뮬레이션 첫 계약](ParticleEffectSimulationContract.md)

## 저작에서 실행까지

이미터의 단계별 모듈 스택을 유지한다. 기본 모듈은 내장 그래프를 만들고 사용자 모듈은 작성된 그래프를 전달한다. 두 경로 모두 `CompileEffectModule`을 거쳐 타입이 확정된 순차 IR을 생성하며 `EvaluateEffectModules`가 이를 평가한다. 기본 Gravity를 위한 별도 CPU 계산 우회 경로는 없다.

구현은 `Engine/Effect/EffectModuleIR.h`와 `EffectModuleIR.cpp`에 있다. 이 버전은 메모리 내 컴파일 계약이다. 저장·cook 형식이나 GPU 바이너리 ABI를 정의하지 않는다. 그래프 UI, GPU 코드 생성, 제품 tick 연결은 후속 단계다.

## 그래프와 타입

노드는 안정 ID, 연산, 인수 노드 ID를 갖고 속성 출력은 `(속성, 노드 ID)`로 지정한다. 노드 배열 순서와 화면 배치는 실행 순서가 아니다. 반복 방식의 위상 정렬로 의존 순서를 확정한다. 연결되지 않은 노드도 검증하며, 연결되지 않은 순환 역시 거절한다.

| 연산 | 입력과 결과 |
| --- | --- |
| Constant | Boolean / UInt / Float / Float3 / Color 유한 값 |
| Input | 선언된 포트의 타입 |
| ReadAttribute | Age/Lifetime/Size → Float, Position/Velocity → Float3, Color → Color |
| DeltaSeconds | Update에서 Float |
| Add | 같은 Float/Float3/Color 두 값 → 같은 타입 |
| Multiply | Float/Float3/Color × 오른쪽 Float → 왼쪽 타입 |
| ExpNeg | Float → `exp(-x)` Float |

자동 타입 변환은 없다. Boolean/UInt 산술, 벡터 간 곱, 분기·루프·외부 호출은 이 IR의 지원 범위 밖이다. 속성 읽기는 모듈 기술자의 reads 안에 있어야 하며, writes의 각 속성은 정확히 한 개의 같은 타입 출력을 가져야 한다. Age는 엔진 소유이므로 쓰기를 거절한다. 속성 모듈은 Initialize/Update에서만 허용한다.

컴파일 오류는 실행 가능한 부분 프로그램을 반환하지 않는다. `EffectDiagnostic`의 code·path·message에 모듈 인스턴스와 노드/포트/출력 위치를 남긴다. 중복 ID, 존재하지 않는 연결, 인수 개수, 타입, 순환, 버전, 단계, 속성 접근, 누락·중복 출력 오류를 구분한다.

## 입력과 지원 모듈

스키마와 IR은 동일한 `ValidateEffectInput`을 사용한다. CPU 실행은 **ConstantInput·ParameterInput·RandomInput·CurveInput·GradientInput**을 지원한다. 파라미터 기본값은 컴파일 결과에 복사한다. 실행 시 제공한 override는 안정 ID로 해석하며 타입·유한성·포트의 양수/비음수 영역을 다시 검사한다. 알려지지 않은 ID와 중복 override도 거절한다.

10-2b-1에서 RandomInput·CurveInput·GradientInput의 CPU 평가를 추가했다. 기존 결정적 난수 key 계약을 유지하며 분포 v1과 선형 보간·암묵적 수명 읽기는 [시뮬레이션 첫 계약](ParticleEffectSimulationContract.md)을 따른다. 이 입력들의 GPU lowering은 아직 구현하지 않았다.

| 기본 모듈 | 현재 lowering |
| --- | --- |
| ce.gravity | `Velocity + acceleration * dt` |
| ce.drag | `Velocity * exp(-coefficient * dt)` |
| ce.lifetime / ce.size / ce.color | 타입이 맞는 입력을 속성에 대입 |
| ce.size.life / ce.color.life | 커브/그라데이션 또는 상수/파라미터 입력을 평가해 대입 |
| ce.burst / ce.position.sphere / ce.velocity.cone / ce.billboard | `ir.builtin.unsupported`; 생성·형상 표본·렌더 후속 구현 |

기존 폭발 예제의 모든 입력을 실행할 수 있다는 뜻은 아니다. 모듈 기술자 11개 중 7개의 제한된 입력 경로를 구현한 수치 기준선이다.

## 평가와 상태 보존

평가기 입력은 입자 값과 초기화 마스크, 유한한 비음수 dt, 타입이 있는 파라미터 override다. 그래프에 선언된 reads가 초기화돼 있어야 한다. 비활성 모듈도 컴파일 시 구조를 검증하지만 실행 시 읽기와 쓰기를 생략한다.

속성 읽기는 해당 모듈 진입 상태를 관찰한다. 모든 노드를 계산한 뒤 출력을 함께 반영하므로 한 모듈 안에서 Position과 Velocity를 교환할 수 있다. 모듈 사이는 스택 순서를 지키며, 같은 속성의 마지막 writer가 이긴다. 따라서 Gravity→Drag와 Drag→Gravity는 서로 다른 결과를 낸다.

입력 오류, 초기화되지 않은 읽기, 비유한 값이나 산술 overflow, 잘못된 최종 Lifetime/Size가 발생하면 **전달된 전체 스택의 변경을 취소**한다. Lifetime은 양수, Size는 비음수여야 한다. Color는 유한 HDR 값을 허용한다. 프로그램은 컴파일러가 만든 상태로만 읽을 수 있고 호출자는 명령 목록을 수정할 수 없다.

`EvaluateEffectModules`는 모듈 연산만 수행한다. 10-2b-1의 별도 `StepEffectParticle`이 Age 증가·수명 종료·Update·위치 적분을 수행한다. spawn/capacity scheduler·고정 시계 backlog는 맡지 않는다. 최소 고정 GPU 커널과의 실제 수치 비교 범위는 시뮬레이션 첫 계약에 기록하며, 일반 그래프 GPU 실행이나 성능을 주장하지 않는다.

## 재현

```powershell
pwsh -NoProfile -File Tools/regression/verify-effect-module-ir.ps1 `
  -OutputDirectory artifacts/phase10-ir-new
pwsh -NoProfile -File Tools/regression/verify-effect-transport.ps1 `
  -OutputDirectory artifacts/phase10-regression-new `
  -FixtureProject artifacts/phase10-1a-20261022/repro-fixture/Project `
  -CookedPak artifacts/phase10-1a-20261022/repro-fixture/GameAssets.pak
```

첫 검사는 실제 Effect 정적 라이브러리에 링크한 네이티브 Debug/Release 소비자를 실행한다. Gravity의 여러 초기 속도와 dt를 해석식·사용자 그래프 결과와 비교하고, Drag와 스택 순서·상태 복구·타입 진단을 검사한다. 두 번째는 기존 스키마·전송 회귀와 Debug Editor/Player 포함·제외 제품 4조합, scene reload, resize 캡처를 재검증한다. 출력은 새 디렉터리를 사용한다.
