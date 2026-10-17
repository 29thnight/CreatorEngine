# 네트워크 준비 경계와 미구현 복제 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 상태

현재 저장소에 게임 peer/transport/replication/RPC runtime을 제공하는 별도 네트워크 프레임워크는 연결돼 있지 않다. CLI·HTTP 제어 통로는 게임 복제 계층의 구현 증거가 아니다. 이 문서는 향후 서버 권한형 실시간 게임 모델의 요구를 보존한다. P2P·rollback·전용 서버는 별도 후속 범위다.

## 현재 재사용할 기반

공통 `Runtime::TickSimulationFrame`이 Editor Play와 Player의 프레임 순서를 소유한다. EntityHandle은 sceneId/index/generation, script ObjectHandle은 별도 전역 세대 registry다. Scene 구조 변경은 Host의 안전 경계에서 적용한다. worker는 AI capture나 PhysicsSceneChannel처럼 값 입력/결과를 교환하는 선례가 있다.

중요한 정정: 물리에는 이미 `ScenePhysicsSimulation::Advance`의 1/60초 accumulator와 최대 4 catch-up tick이 있다. 그러나 관리 PrePhysics/PostPhysics, GameLogic, Scope.Delay는 프레임 축이다. 모든 simulation 상태가 고정 tick으로 이동했다는 뜻은 아니다. 현재 Component에는 `OnSimulationTick`이 없으며 이를 추가하는 계획은 여전히 미구현이다.

reflgen schema와 Authoring::Document/NodeView 경계는 저장 정책 재사용의 재료다. source 타입 누출이 줄었다고 headless server의 링크 폐포가 검증된 것은 아니다.

## 미래 정체성·wire 계약

프로세스 로컬 EntityHandle, script ObjectHandle, 이름 기반 type hash를 wire identity로 보내지 않는다. 서버 발급 NetworkObjectId와 local entity registry, 명시적 component/message/field ID를 별도로 설계한다. 기존 component UUID는 영속 정본 후보이며 compact wire ID와 handshake 매핑은 별도다. 제거한 ID는 tombstone으로 남겨 재사용 충돌을 막는다.

authoring/cooked/network archive는 schema 순회 일부만 공유한다. network path에는 YAML/JSON DOM을 넣지 않고 bounded binary codec, version, quantization, baseline, 권한 검사를 정의한다. reflection method가 존재해도 자동 RPC로 노출하지 않는다. spawn 가능 타입과 RPC는 allowlist를 요구한다.

## 미래 실행 계약

Network Thread는 live Scene이나 컴포넌트를 직접 만지지 않는다. 검증된 bounded command와 immutable snapshot을 owner thread에 전달한다. 고정 simulation tick에는 입력 채택·명령 commit·고정 gameplay·물리·상태 게시 순서를 명시하고 render frame interpolation과 분리한다.

현재 initialization이 scene activation을 poll하고 GameLogic이 render data를 게시하므로 두 함수를 그대로 fixed loop 안에 넣으면 안 된다. 기존 물리 accumulator와 새 simulation clock 중 어느 쪽이 tick authority인지 먼저 정해야 이중 catch-up을 피할 수 있다. Scope.Delay와 contact tick ID의 시간 의미도 이 결정에 맞춰야 한다.

상태 snapshot은 unreliable/latest-wins, spawn/despawn·확정 사건은 reliable ordered를 기본 후보로 둔다. authoritative validation, interest management, reconnect/full-state resync, stale baseline 거부, packet size/rate 상한, malformed input 실패 정책을 설계한다. rollback은 별도의 state/effect 결정성 계약이 필요하다.

## 착수 순서와 수용 기준

1. local loopback에서 ID·schema·bounded command·snapshot 의미를 고정한다
2. fixed tick과 render 분리를 연결하고 pause/catch-up/dropped time을 명시한다
3. spawn/despawn·변경 state·관심 영역과 baseline 복구를 검증한다
4. latency/loss/reorder/duplicate·disconnect·malformed input을 주입한다
5. 그 다음 message-oriented transport adapter를 선택한다. GNS/ENet은 기존 검토 후보이며 설치·지원된 dependency가 아니다
6. headless server를 별도 target으로 만들 경우 실제 binary dependency와 audio/render 비필수 경계를 검증한다

이 계획은 transport 구현·배포 일정의 확정이 아니다. 현재 API와 향후 이름을 혼동하지 말고 [물리 통합](PhysicsRedesignPlan.md), [효과 계약](SimulationEffectContractPlan.md)과 함께 설계한다. 외부 라이브러리의 최신 상태는 이번 source-only 갱신에서 조사하지 않았다.

## 구현 근거

- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
- [Engine/SceneRuntime/ScenePhysicsSimulation.h](../../Engine/SceneRuntime/ScenePhysicsSimulation.h)
- [Engine/SceneRuntime/ScenePhysicsSimulation.cpp](../../Engine/SceneRuntime/ScenePhysicsSimulation.cpp)
- [Engine/SceneRuntime/EntityHandle.h](../../Engine/SceneRuntime/EntityHandle.h)
- [Engine/SceneRuntime/ScriptObjectRegistry.h](../../Engine/SceneRuntime/ScriptObjectRegistry.h)
- [ScriptCore/Component.cs](../../ScriptCore/Component.cs)
- [ScriptCore/SimulationScope.cs](../../ScriptCore/SimulationScope.cs)
- [vcpkg.json](../../vcpkg.json)
