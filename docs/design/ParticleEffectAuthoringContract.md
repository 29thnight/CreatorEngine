# 파티클 저작 정의 계약 v1

> 2026-10-24 · Phase 10-1b · C++ 정의·검증 구현, 실행·직렬화는 후속 단계

[Phase 10 계획](../plans/ParticleEffectRedesignPlan.md)

소스: 정의 API `Engine/Effect/EffectDefinition.h`, 검증기·카탈로그 `Engine/Effect/EffectDefinition.cpp`, 검증에 사용한 폭발 예제 `Tools/regression/fixtures/EffectExplosionDefinition.h`. 새 소스는 현재 작업 트리의 파일이며 아직 게시된 커밋의 링크로 제공하지 않는다.

## 소유 구조와 신원

`EffectDefinition → EmitterDefinition → ModuleStack → ModuleInstance → InputBinding`이 저작 정본이다. Effect가 공통 파라미터·seed·고정 시계를 소유하고, 이미터가 capacity·seed·시작 시각·공간·머티리얼 참조를 소유한다. 스택의 모듈 배열은 저작 순서 그대로 보존한다. 모듈 인스턴스는 `ModuleDefinition`의 ID와 정확한 버전을 참조한다. 사용자 정의 그래프도 이후 이 정의의 단계·입출력·속성 계약을 만족해야 한다.

- schemaVersion은 1이다. 알 수 없는 버전은 자동 추정 없이 거절한다. 모듈 버전도 정확히 일치해야 한다. 마이그레이션은 별도 명시적 작업이다.
- 안정 ID는 길이 1~128의 ASCII 영문·숫자·`._:-`로 구성한다. 생성 시 부여하고 저장한다. 표시 이름, 배열 인덱스, 화면 배치로부터 매번 계산하지 않는다. 복제는 새 ID를 발급하고 이동·재정렬·이름 변경은 ID를 보존한다.
- 카탈로그 모듈 ID는 카탈로그 전체, 파라미터 ID와 이미터 ID는 Effect 내부, 모듈 인스턴스 ID는 해당 이미터의 모든 단계에서 유일해야 한다. 입력 포트 ID는 모듈 정의 내부에서 유일하다. 같은 표시 이름은 허용한다.
- 머티리얼 필드는 안정 에셋 참조다. 현 단계는 ID 형식만 검사하며 실제 에셋 존재·타입·generation 해석은 cook/런타임 소비자가 검증한다. 예제의 참조는 실제 머티리얼이 준비됐다는 증거가 아니다.
- C++ 구조체 메모리와 `std::variant`의 내부 배치를 파일로 덤프하지 않는다. v1은 논리 스키마·타입 계약이다. 파일 codec·저장/로드 왕복은 10-4에서 구현한다.

## 단계·속성·시계

각 이미터는 Spawn, Initialize, Update, Render 스택을 하나씩 갖는다. 스택 배열 자체의 배치와 무관하게 단계 순서는 고정이고, 각 스택 내부는 배열 순서다. 비활성 모듈도 ID·버전·입력·단계가 유효해야 하지만 속성을 발행하거나 방출/렌더 동작 수에 기여하지 않는다.

| 단계 | 계약 |
|---|---|
| Spawn | 이미터 도메인. 입자 속성을 읽거나 쓰지 않는다. v1 Burst는 이미터 활성화 시 한 번 방출하고 시작 시각 이전에는 방출하지 않는다. 반복 방출·이벤트는 후속 확장이다. |
| Initialize | 새 입자에 한 번 실행한다. 엔진이 Age=0을 제공한다. Lifetime·Position·Velocity·Size·Color는 활성 초기화 모듈이 모두 제공해야 한다. |
| Update | tick 시작에 Age를 dt만큼 증가시킨다. Age가 Lifetime 이상이면 퇴역시키고 이후 갱신·적분·draw에서 제외한다. 살아 있는 입자에 갱신 모듈을 순서대로 실행한다. |
| Render | 갱신 뒤 엔진이 Position += Velocity × dt를 한 번 수행하고 렌더 입력을 읽는다. 렌더 모듈은 속성을 쓰지 않는다. v1은 정확히 하나의 renderer를 요구한다. |

새 입자는 활성화 tick의 Initialize 뒤 같은 tick의 Age 증가·갱신·적분에 참여한다. Age는 엔진 소유이므로 사용자 모듈의 writer가 될 수 없다. 읽기 전에 선행 초기화가 있어야 하며, 여러 모듈이 같은 속성을 쓰면 앞 모듈의 결과를 다음 모듈이 읽는다. 뒤의 쓰기가 최종 값이다. 병렬 실행·IR 최적화가 이 순서를 바꾸면 안 된다. 현재 검증기는 순서와 읽기/쓰기 계약을 검사하며 적분·수명 계산 자체는 실행하지 않는다.

시계는 fixedStepSeconds > 0, timeScale ≥ 0, maxSubsteps > 0이며 모든 실수는 유한해야 한다. timeScale은 들어오는 실시간에 적용하고 시뮬레이션 dt는 고정한다. Pause는 tick을 만들지 않고 Step은 pause·배율과 무관하게 고정 tick 하나를 요청한다. maxSubsteps를 넘은 누적 시간은 backlog로 보존한다. 렌더 뷰가 추가돼도 tick을 추가하지 않는다. 실제 스케줄러·pause/step·다중 뷰 연결은 10-1c 이후의 실행 수용이다.

capacity는 양수이고 시작 시각은 유한한 0 이상 값이다. v1 용량 초과 정책은 DropNewest로 고정한다. 먼저 수용한 입자를 유지하고 초과 방출을 버리며 다음 tick으로 재시도하지 않는다. 공간은 Local 또는 World다. 실제 버퍼·좌표 변환·용량 처리는 아직 구현하지 않았다.

## 입력 표현

기본 타입은 Boolean, UInt32, Float32, Float3, Color4다. Color4는 선형 RGBA 값이며 이 계층은 색공간 변환이나 범위 clipping을 수행하지 않는다. 암묵적인 숫자 타입 변환은 허용하지 않는다. 명시한 입력이 없으면 모듈 포트의 상수 기본값을 사용한다. 중복·알 수 없는 포트와 존재하지 않는 파라미터 바인딩은 거절한다. 파라미터는 ID로 바인딩하며 현재는 저작 기본값의 타입·값을 검사한다. 실행 중 override는 이후 소비 경계에서 같은 계약을 검사해야 한다.

| 표현 | 허용 타입과 평가 기준 |
|---|---|
| 상수 | 모든 기본 타입. 포트 타입과 정확히 일치 |
| 난수 범위 | UInt32·Float32·Float3. 성분별 min ≤ max. Boolean·Color는 직접 범위 표현을 지원하지 않음 |
| 커브 | Float32, Update의 정규화 입자 수명 기준 |
| 그라데이션 | Color4, Update의 정규화 입자 수명 기준 |
| 파라미터 | 모든 기본 타입. 바인딩한 파라미터와 포트 타입이 정확히 일치 |

커브·그라데이션은 최소 두 key, 첫 time=0, 마지막 time=1, 유한한 오름차순 time을 요구한다. 값은 선형 보간하며 수명 입력을 [0,1]로 clamp한다. lifetime은 양수여야 한다. 커브의 lifetime 읽기도 속성 의존성에 포함한다. 포트는 Finite·Nonnegative·Positive 값 도메인을 지정할 수 있다. 상수·난수 양끝·파라미터 기본값·모든 key에 같은 도메인을 적용한다. Size는 0을 허용하고 Lifetime은 0을 거절한다.

## 난수 신원

`EffectRandomKey`는 최종 난수값이 아니라 **결정적 sample key**만 만든다. AtSpawn은 simulation tick을 무시하고 EachTick은 simulation tick을 포함한다. Initialize는 AtSpawn, Spawn은 EachTick, Update는 두 방식 모두 허용하며 Render 난수 입력은 금지한다. Update의 AtSpawn은 생성 시 결정한 입자별 표본을 재사용한다. Spawn의 EachTick은 실제 방출 평가 tick에서 이미터 도메인의 표본을 고른다.

v1 key는 FNV-1a 64비트다. 순서대로 schema version, effect seed, emitter seed, particle ID를 unsigned 64비트 little-endian으로 넣고, emitter/instance/port ID를 길이(64비트 little-endian)+ASCII 바이트로 넣는다. sampling 종류(64비트), tick(EachTick만 실제 tick, 그 외 0)을 이어 넣는다. 모듈 타입 ID와 인스턴스 ID를 혼동하지 않는다. 같은 모듈을 두 번 쌓아도 인스턴스 ID가 다르면 표본을 분리한다.

simulation tick은 Effect 활성화 세션의 고정 tick 번호다. 재시작은 tick·입자 ID를 초기화하고 동일 seed로 같은 표본 신원을 재현한다. Pause는 tick을 증가시키지 않는다. 렌더 frame/view 번호와 스택 인덱스는 key에 들어가지 않는다. 실제 소비자는 입자 ID를 재활용 슬롯 인덱스와 구분하고, Spawn의 입자 ID는 0, 실제 입자 ID는 활성화 세션 안에서 유일한 0 이외 값으로 제공해야 한다. key의 난수 분포 변환·GPU 일치·충돌/통계 품질은 아직 검증하지 않았다.

## 기본 카탈로그와 검증

현재 카탈로그는 `ce.burst`, `ce.lifetime`, `ce.position.sphere`, `ce.velocity.cone`, `ce.size`, `ce.color`, `ce.gravity`, `ce.drag`, `ce.color.life`, `ce.size.life`, `ce.billboard`의 **11개 정의 기술자**다. 실행 모듈 11개가 구현됐다는 뜻은 아니다. 10-2a에서 [공통 IR과 제한된 CPU 모듈 계산](ParticleEffectModuleIRContract.md)을 구현했다. 10-2b-1에서 난수·커브·그라데이션 CPU 입력과 최소 고정 GPU 체인을 추가했다. 일반 IR의 GPU 변환과 제품 소비는 10-2b 잔여다. 현재 sphere/cone는 초기 계약에 필요한 radius/speed 포트를 제공하며 추가 shape 제어는 실행 모듈 도입 시 버전 정책에 따라 확장한다.

`ValidateEffectDefinition`은 code·ID 경로·메시지의 진단 목록을 반환한다. 오류가 하나라도 있으면 실행 계획 생성에 넘기지 않는다. 그래프가 생기기 전 단계라 노드/포트 화면 위치는 없고 저작 인스턴스·입력 경로까지 식별한다.

```powershell
pwsh -NoProfile -File Tools/regression/verify-effect-definition.ps1 `
  -OutputDirectory artifacts/phase10-definition-new
```

검증기는 실제 Effect 정적 라이브러리를 소비하는 네이티브 테스트를 Debug·Release로 빌드·실행한다. 폭발 fixture, 다중 이미터, 재정렬·이름 변경과 안정 ID, 단계·타입·버전·기본값·속성 의존성, 커브·그라데이션·파라미터·난수 key를 확인한다. 파일 직렬화·Editor UI·GPU·실제 파티클 화면 수용과 구분한다.
