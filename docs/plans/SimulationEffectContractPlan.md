# PrSM 시뮬레이션 효과 계약의 미구현 요구

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 상태와 목표

이 계획의 효과 실행기·CreatorEngine용 PrSM integration은 현재 제품 실행 경로에 도입된 기능이 아니다. 현재 C#은 Component.OnSimulate의 Task, SimulationScope, GameThreadSynchronizationContext를 사용한다. 그 안정화와 새로운 언어/실행기 도입을 같은 완료 항목으로 묶지 않는다.

목표는 PrSM 원문에서 정적 의미를 검사하고 생성 C#을 기존 registration/serialization/Editor/build/reload까지 연결하는 것이다. C# API 이름만 정리하거나 독립 prototype을 실행한 것으로 완료하지 않는다.

## 유지할 설계 의도

즉시 효과는 폐쇄 카탈로그의 동기 메서드로 값을 반환한다. 중단은 Wait/WaitUntil/Race 같은 실행기 원시로 분리한다. 효과를 무조건 금지하기보다 live/record/replay/test handler로 라우팅한다. `Var<T>`는 미래 transaction cell의 이름으로 유지한다. 임의 사용자 효과의 개방형 확장이나 기존 외부 STM 라이브러리 채택이 확정된 것은 아니다.

cold routine, Race 분기 전체의 시작·취소, cleanup ordering, cancellation 이후 재개 여부, 실패 결과·진단을 먼저 명세해야 한다. 현재 .NET 협조 취소와 다른 의미를 도입한다면 콘텐츠 이전 규칙을 작성한다. `finally`가 언제나 engine teardown 전에 실행될 것이라는 가정에 수명 안전을 맡기지 않는다.

## 현행 코드의 실제 경계

SimulationScope.Delay는 엔진 dt로 진행하며 등록한 프레임에는 완료하지 않는다. native state 접근은 game-thread guard가 적용되고 정상 await continuation은 설치된 context의 다음 drain에서 재개된다. ConfigureAwait(false)나 직접 Task.Run은 이 marshal 계약을 포기할 수 있다. 현재 코드에는 효과 카탈로그가 임의 난수·시간·I/O·native mutation을 모두 통제하는 기능이 없다.

BehaviorTree의 random 선택 등도 future 결정성 범위에 포함해야 한다. 물리 ContactStream은 OnBeginSimulation 등록·PostPhysics 소비·Scope cleanup을 제공하지만 effect recording, rollback, PrSM lowering의 구현은 아니다. 미래 언어는 이미 있는 [ContactStream 수명 계약](../design/PhysicsContactExecutionContract.md)을 소비해야 한다.

## 도입 단계

- E0/E1: engine-time awaiter와 suspension primitive, 폐쇄 카탈로그의 반환/실패 의미
- E2/E7: 실제 native/managed 호출을 읽기·쓰기·외부 효과로 분류하고 정적 검사와 우회 정책 수립
- E3/E4: 효과 기록/재생과 mismatch/fault 진단. 동일 입력뿐 아니라 초기 상태·물리·BT random도 함께 다룸
- E5/E6: routine 상태·취소·대기 위치 표시와 allocation/runtime 예산 계측
- E8: transaction cell의 읽기·쓰기·commit/abort 및 되돌릴 수 없는 효과 유예
- PrSM0~2: 대상 프로필 분리, 기존 Component/SerializeField/생성 등록 연결, 의미 분석
- PrSM3~4: routine/cleanup/transaction lowering, build/Editor/진단/reload 연결
- PrSM5: 실제 콘텐츠를 옮겨 기존 동작·실패·성능과 비교

## 재개 전 결정과 완료 조건

현행 lifecycle failure/cancel/reentrancy를 먼저 안정화하고 최신 ScriptCore·clock·asset 경계를 다시 조사한다. 원문→생성 코드→Editor 저작→실행→reload가 이어지는 작은 수직 slice부터 진행한다. 과거 외부 reference prototype의 31/31·할당 수치 같은 기록은 현재 엔진 결과가 아니며 이 개정에서 재실행하지 않았다.

완료에는 금지 await의 정적 거부, live/record/replay 대조와 실패 canary, cleanup/cancel 의미, 실제 gameplay 이관, 진단과 성능 예산, reload 뒤 낡은 assembly 참조 해제가 필요하다. 현재 사용자가 OnSimulate 반환형을 바꿔야 한다는 안내는 하지 않는다. [현행 스크립트 계약](ScriptSurfacePlan.md)을 계속 사용한다.

## 구현 근거

- [ScriptCore/Component.cs](../../ScriptCore/Component.cs)
- [ScriptCore/SimulationScope.cs](../../ScriptCore/SimulationScope.cs)
- [ScriptCore/GameThreadSynchronizationContext.cs](../../ScriptCore/GameThreadSynchronizationContext.cs)
- [ScriptCore/ScriptRegistry.cs](../../ScriptCore/ScriptRegistry.cs)
- [ScriptCore/BehaviorTree.cs](../../ScriptCore/BehaviorTree.cs)
- [ScriptCore/ContactStream.cs](../../ScriptCore/ContactStream.cs)
- [ScriptCore.Generators/ScriptGenerator.cs](../../ScriptCore.Generators/ScriptGenerator.cs)
