# 관리 생명주기와 Task 실패 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현행 정본과 독립된 수명 축

공개 표면은 [ScriptSurfacePlan](../ScriptSurfacePlan.md), 전체 frame은 [FrameLifecycle](../../analysis/FrameLifecycle.html)을 따른다. 이 문서는 기존 LC 안정화 링크를 유지하며 실패·취소·재진입의 실제 구현을 요약한다. future PrSM 효과 실행기와 별개다.

native Scene이 여섯 단계를 구동한다. 관리 registry는 initialize/enter/begin의 “호출됨”과 “성공함”을 구분한다. opening hook이 던졌다면 성공한 단계로 승격하지 않으며 closing hook은 실제 열린 단계에 대응한다. fallback teardown은 고아 정리와 assembly reload처럼 native driver가 없는 경우를 보완한다.

## 순서와 예외 격리

OnBeginSimulation 성공 뒤 OnSimulate가 시작된다. scope cancellation은 OnEndSimulation보다 앞선다. 취소 콜백에서 예외가 나도 나머지 cleanup·pending delay 해제·기존 token Dispose·새 token 준비를 계속한다. 정상 완료한 delay는 cancellation registration을 완료 통지보다 먼저 dispose해 Task 참조가 scope 수명 동안 남지 않게 한다.

PrePhysicsTick은 모든 scope를 먼저 진행한 뒤 queued continuation을 drain하고 enabled script의 PrePhysics를 호출한다. PostPhysicsTick은 finally에서 ContactRouter.EndFrame을 호출하므로 사용자 훅 실패가 borrowed contact buffer를 다음 프레임까지 남기지 않게 한다.

## Delay와 외부 await의 의미

Delay는 생성 프레임을 건너뛴다. 0·음수는 최소 한 프레임, NaN은 거부, positive infinity는 취소까지 대기다. enabled와 scope time은 별개다. native pause 경로는 관리 틱을 생략한다.

정상 context capture를 유지하는 await continuation은 game-thread queue로 돌아오지만 ConfigureAwait(false)와 직접 worker 실행은 그렇지 않다. worker의 동기 Send는 NotSupportedException이며 native state API에도 thread guard가 있다. 긴 동기 사용자 코드나 취소를 무시하는 Task를 강제로 종료할 수는 없다.

## 계속 유지할 회귀

initialize/begin 실패, cleanup 하나/여럿 실패, delay 정상 완료 후 회수, delay cancel 중 새 등록, callback에서 자기/다른 component 파괴, 외부 Task 실패, worker API 거부, reload의 pending continuation 제거를 각각 검증한다. 이를 하나의 정상 Play 테스트로 대신하지 않는다.

과거 LC 번호별 PASS·retention 개수는 당시 결과다. 이번에는 소스 순서만 확인했고 실행하지 않았다. 생성기 기반 제한 awaiter·effect tape·cold routine은 현재 LC 코드에 없으며 [미래 효과 계획](../SimulationEffectContractPlan.md)에 남아 있다.

## 구현 근거

- [ScriptCore/ScriptRegistry.cs](../../../ScriptCore/ScriptRegistry.cs)
- [ScriptCore/SimulationScope.cs](../../../ScriptCore/SimulationScope.cs)
- [ScriptCore/GameThreadSynchronizationContext.cs](../../../ScriptCore/GameThreadSynchronizationContext.cs)
- [ScriptCore/Component.cs](../../../ScriptCore/Component.cs)
- [Engine/SceneRuntime/RuntimeFrame.cpp](../../../Engine/SceneRuntime/RuntimeFrame.cpp)
