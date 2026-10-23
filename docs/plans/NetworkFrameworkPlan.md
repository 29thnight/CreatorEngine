# 네트워크 준비형 런타임 · Replication 기반 계획 (PHASE 20)

<!-- plan-summary: 2026-10-23: F# 일반 멤버·정적 콜백 등록, API45 OnTickSimulate(dt) 고정 틱을 유지한다. ND-a/ND-b/N1/N3의 사내 선행 수용과 서버 담당자의 wire schema·replication·transport·headless 제품 작업을 구분한다. -->

> 2026-10-23 최신 결정: 게임 저작과 공통 관리 실행기는 F#이다. 컴포넌트 생명주기는 **override 없는 일반 멤버 → 생성기의 정적 콜백 등록**, 고정 틱은 **OnTickSimulate(dt), Script API45**다. ScriptCore의 C# 코드는 네이티브 ABI·값 형식·바인딩 진입점으로 유지한다. 수명·세대 핸들·실패 reload 보존은 유지한다. [현재 계약](../design/FSharpScriptingContract.md)과 [API45 수용 기록](../analysis/FSharpManagedRuntimeAcceptance20261023.md)을 따른다. 아래 API43/44·C# 기본 저작 기록은 이전 단계이며 OnSimulate·Scope.Delay·simulation CE는 복원하지 않는다.


> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

> 2026-10-22 사용자 결정 반영: C# 기본 저작 유지·Verse식 계약 PrSM 이관. 아래 고정 master 근거와 구분하여 최신 계획으로 적용한다.

기존 기준선은 위 master이며, 2026-10-22~23 구현·실행 결과는 연결된 수용 기록과 현재 작업 트리를 기준으로 구분한다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/NetworkFrameworkPlan.md)

## 남은 작업과 완료 판정

2026-10-23 master 통합 재점검: `a985ee7e1`의 상류 변경과 기존 작업을 통합한 실행·호출자 수정은 [동기화 수용 기록](../analysis/MasterSyncRecheck20261023.md)을 따른다. 이전 실행 기록의 지문은 보존하고 이번 통합 작업본의 제품 검증과 구분한다.

2026-10-23 master 동기화: 상류의 [2026-10-21 서버 인계 설계](../design/DedicatedServerHandoff.md)는 당시 제안을 보존한 문서다. 현재 F#/API45·완료된 ND-a/ND-b/N1/N3와 남은 headless 제품 경계는 [최종 인계 점검](../analysis/NetworkWorkerHandoff20261023.md)을 우선한다. 상류의 렌더 구조 변경을 받아도 OnSimulate·Scope.Delay를 복원하지 않는다.

- ND/N0~N10의 활성 범위: 공통 고정 simulation tick·C++ 계약 OnTickSimulate(dt)·F# 단독 저작·명시적 tick 명령 적용, wire identity/schema·replication·bounded command/snapshot 교환을 loopback으로 검증한 뒤 transport를 붙인다. OnSimulationTick 저작 훅은 추가하지 않는다.
- 이미 구현된 물리 fixed step·로컬 EntityHandle·ContactStream을 네트워크 simulation/replication 완료로 확대하지 않는다. EntityHandle/typeID를 wire ID로 사용하지 않고 reflgen 순회만 Archive 간 공유한다.

## 적용할 구조와 경계

`current scene/physics/managed lifecycle provides local state; replication must add its own wire identity boundary`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `ND-a` · PHASE 20 | 계약 헤더 6종 동결 · 변경 절차 · 경계 게이트 | done |
| `ND-b` · PHASE 20 | 로컬 엔티티 수명·일반 컴포넌트 쓰기 관찰자 | done |
| `N0` · PHASE 20 | 기준선·금지 타입·telemetry 실패 게이트 | todo |
| `N1` · PHASE 20 | Archive·Parser 경계 밀봉 | done |
| `N2` · PHASE 20 | Network ID와 schema 정본 | todo |
| `N3` · PHASE 20 | Fixed Simulation Clock + C++ 계약 OnTickSimulate(fixedDelta), API45/F# 일반 멤버·공통 런타임·30/60/144fps·moving GPU 수용 | done |
| `N4` · PHASE 20 | Loopback Transport와 메시지 프레이밍 | todo |
| `N5` · PHASE 20 | Spawn/Despawn과 Full Snapshot | todo |
| `N6` · PHASE 20 | Dirty Delta·Quantization·Interest | todo |
| `N7` · PHASE 20 | 실제 Transport A/B 판정과 adapter | todo |
| `N9` · PHASE 20 | Dedicated Server 제품 target — 요구 시 별도 수용 | 조건부 · 미수용 |
| `N10` · PHASE 20 | Hardening·관측·soak | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/Scene.h:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.h#L39) — `class Component;`
- [ScriptCore/Component.cs:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Component.cs#L22) — `public abstract class Component`
- [Engine/RuntimeHost/EngineLaunchConfig.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/EngineLaunchConfig.h#L14) — `struct EngineLaunchConfig`

## 2026-10-22 결정 — C# 기본 저작 유지와 언어 독립 고정 틱

C#에 Verse식 효과·중단·실패·트랜잭션 계약을 이식하는 계획은 중단한다. C#의 OnSimulate 전용 코루틴 경로와 Scope.Delay 등 공개 전용 저작 표면은 제거하고, OnSimulate를 C++ 계약의 동기 OnUpdate(dt)로 대체한다. PrSM 계약 도입 전까지 OnUpdate는 고정 틱마다 실행한다. 기본 C# 저작으로 복귀하며 호출자 이주·수명 안전성 검증은 ScriptSurfacePlan이 소유한다. OnSimulationTick 저작 훅도 신설하지 않는다. 직전 검토의 OnSimulate 최초 진입 예약·Scope.Delay 전체의 고정 틱 이관·모든 await의 Scope 제한은 필수 계획에서 철회한다. 현재 작업 트리에 OnUpdate 고정 틱과 C# 전용 구조 제거를 구현했다. [수용 기록](../analysis/CSharpFixedUpdateAcceptance20261022.md)에서 실제 실행과 잔여를 구분한다.

### 현재 코드와 적용 경계

| 경로 | 확인한 현재 동작 | 적용할 정책 |
|---|---|---|
| ScriptRegistry / OnSimulate | 기본 생명주기 + 고정 틱 OnUpdate 배치 | OnSimulate 자동 본문 시작 제거 완료 |
| SimulationScope.Delay | 타입·대기 큐·공개 Scope 제거 | 전용 C# 대기 없이 일반 코드로 저작 |
| GameThreadSynchronizationContext | 프레임 경계에서 게임 스레드로 재개 | 스레드 안전 장치 유지. 일반 Task/await를 허용하되 완료 시각의 결정성은 보장하지 않음 |
| RuntimeFrame / ScenePhysicsSimulation | 프레임 호출 아래 물리 자체 누산기·고정 스텝·CCT·pose 보간 존재 | 엔진 공통 clock과 단일 스텝으로 통합. 누산기 이중화 방지 |
| PrSM | 별도 저장소에서 LLVM 컴파일러·contract fn·중단 실행 구현 중 | 언어 계약과 엔진 tick adapter를 분리. C# 모사 런타임을 선행시키지 않음 |

### N3 계약과 후속 설계

고정 시계는 C# 저작 양식과 독립적인 엔진 기반이다. 클라이언트와 서버는 같은 tick driver를 사용하고 서버는 렌더 없이 실행한다. 입력 샘플·외부 비동기 결과는 검증한 값 명령으로 특정 tick에 귀속한다. 게임 스레드 복귀와 tick 적용은 다른 경계다.

```text
프레임/외부 입력 → 검증된 값 명령 큐
고정 틱: 명령 적용 → C# OnUpdate(fixedDelta) → 기존 시스템·물리 단일 스텝
       → 결과/접촉 게시 → 안전한 생성·파괴 확정
       → 성공 tick 커밋 → OnTickCommitted/snapshot
표현 프레임: 마지막 커밋 상태 읽기 → Animator/UI/Sound/Camera → 보간/렌더
```

- C#은 기본 컴포넌트·생명주기·Pre/PostPhysics·일반 메서드·Task/await로 저작한다. Scope.Delay와 OnSimulate 전용 코루틴 저작은 제거했다. 네트워크 권한 상태나 물리 상태의 변경은 엔진의 명시적 tick 명령 경로로 전달한다. 일반 Task 완료 시각에 tick 실행이나 결정성을 부여하지 않는다.
- C# 갱신 진입점은 C++ 계약 OnUpdate(dt)로 확정하며 PrSM 계약 도입 전까지 dt=fixedDelta로 호출한다. 물리 앞 게임 스레드에서 생존/Enabled/세대를 검사한 관리 배치를 tick당 한 번 실행한다. tick 서수 조회·값 명령 제출·결과 읽기는 API v43의 Simulation.GetStatus/Submit/ReadSnapshot으로 구현했다. 명령 256개/64바이트/미래128틱, 성공 snapshot 8개/4096바이트와 명시적 포화·유실·세대·실패 규칙은 [인계 계약](../design/SimulationTickExchangeContract.md)에 고정한다. OnUpdate 외 별도 OnSimulationTick이나 전용 await 저작 구조를 만들지 않는다.
- 공개 Scope 제거 후에도 엔진 내부 취소·구독 해제·DDOL·리로드·예외 격리 책임은 기본 C# 경로에서 보장한다. 제거될 대기의 Enabled 규칙이나 Cancel 훅을 영구 API 계약으로 남기지 않는다. PrSM 중단/취소 계약을 C#에 소급 적용하지 않는다.
- 물리 없는 씬에서도 clock은 진행한다. pause/time-scale/씬 교체/catch-up 예산은 공통 driver가 소유한다. 물리 실패 시 미완료 tick snapshot을 게시하지 않는다. 현재 fixed_seconds와 CCT·보간 소비 경로는 측정 없이 재작성하지 않는다.
- ContactStream의 현재 프레임 소비와 tick 결과 보존을 대조하여 0틱 프레임 유실·다중틱 중복을 방지한다. 표현에서 게임 상태로 역류하는 쓰기는 값 명령 경계로 옮기고, 입력/물리 쓰기 호출자를 실제 코드로 분류한다.

### 현재 구현과 N3 제품 수용

구현: SimulationTick(기존 물리와 같은 60Hz/최대 catch-up 4), SceneManager 공통 루프, ClrHost/Bootstrap OnTickSimulate, 생산 물리 단일 Step, 공통 보간 alpha, 씬 epoch/재생 전환 시 clock 초기화, pause 보존, C# 전용 Scope 제거와 내부 접촉 수명 정리. 독립 물리 회귀의 Advance는 자체 누산 경로를 유지하지만 생산 루프는 Step만 호출한다. 별도 OnSimulationTick은 없다. 프레임 입력/표현 Pre/PostPhysics와 Animator·렌더 데이터 처리는 프레임 축에 남는다.

추가 구현: SimulationExchange의 bounded copied-value 명령 큐와 성공 커밋 snapshot ring, native shared-owner adapter의 Apply/Capture 연결, API v43의 C# 서수·제출·복사 읽기, 실제 로컬 위치 명령/물리 뒤 값 소비자. 동일 tick FIFO·현재 tick 재진입 거부·포화·지연·snapshot 유실·작은 버퍼 재시도·실패 공개 차단·Play/Stop/씬/리로드 세대 경계를 구현했다. [서버 인계 계약](../design/SimulationTickExchangeContract.md)을 따른다.

2026-10-23 N3 사내 수용 완료: 세션 Play pacing으로 실제 30/60/144fps 목표(실측 ±5%)를 제어하고 같은 tick의 CCT/강체 궤적, 0틱/다중틱, C# OnUpdate·SDK step 대응과 자식·부착 카메라 렌더 보간을 검증했다. 누락됐던 CCT 이전/현재 위치 보간과 0틱 프레임의 자식 proxy 갱신을 수정했으며 텔레포트·disable/enable·제거·동일 위치 warp·종료 이력 회귀를 연결했다. Camera/animation/foliage/gizmo 표현 소비자는 render frustum으로 연결하고 AI의 기존 simulation frustum은 유지했다. 별도 moving readback에서 실제 0틱 GPU draw/픽셀을 sourceEngineFrame으로 연결했다. 새 cooked Player의 primitive CCT 소비도 수용했다. [수용 기록](../analysis/N3PacingAcceptance20261023.md)과 [판정 계약](../design/SimulationPacingAcceptanceContract.md)을 따른다.

 wire identity/schema·권한·transport 연동은 N2/N4, full snapshot은 N5이며 실제 Dedicated Server target/2-client/connection drain과 PrSM 언어 계약도 별도다.

### ND-a 확정 — 2026-10-22

`Engine/Network/Contracts`의 NetWireIds, INetworkTransport, NetProtocolHeader, INetChannelPolicy, INetTelemetrySink, INetworkEntityRegistry 6종을 contract v1/protocol v1로 동결했다. wire 값은 별도 고정 폭 타입, 프로토콜 헤더는 36바이트 little-endian이며 static ID 원장·tombstone·변경 이력과 소스 fingerprint를 검사한다. [계약과 변경 절차](../design/NetworkContractChangePolicy.md)가 인계 정본이다.

`verify-net-wire-boundary.ps1`은 금지 타입·별칭·숨은 include·wire 필드·ID 재사용·버전/원장 변경을 실패시킨다. 격리 canary에서 타입 유입·endian 반전·필수 회귀 등록 제거·gate 종료코드 실패 전파를 검증한다. [ND-a 수용 기록](../analysis/NetworkContractAcceptance20261022.md)에서 독립 계약 소비자와 실제 네트워크 구현을 구분한다.

기존 무범위 run-all은 폐지되어 있었다. 새 `run-all.ps1 -Suite NetworkHandoff -Configuration All`은 등록된 네트워크 인계 회귀만 전부 실행하며 ND-a를 생략할 수 없다. 관련 변경의 PR/push CI도 이 진입점을 사용한다. 실제 transport/registry 구현 및 원격 CI 실행 완료를 뜻하지 않는다. ND-a는 N0/N4 제한 착수를 열며, W4·ND-b·N1·N3 사내 선행 수용도 완료했다. wire ID/schema·권한·replication·transport와 실제 서버 제품의 전량 인계 완료로 확대하지 않는다.

### ND-b 로컬 확장점 — 2026-10-22

W4 authoring transaction 위에 SceneObservation의 ISceneEntityObserver/IComponentWriteObserver를 연결했다. 생성/로드/편입 완료, 세대가 해제된 파괴, root retirement 및 DDOL Detached/Attached에서 copied instance/location 이벤트를 제공한다. 목적 씬의 전체 root remap 뒤 Attached를 발행하고 ScriptObjectHandle 생존 규칙은 보존한다. 일반 쓰기는 비렌더 reflection/Prefab·Inspector/Undo/Redo·native setter·Transform/physics accepted batch에서 발행한다. 기존 렌더 dirty 큐를 일반 dirty로 재사용하지 않는다.

shared-owner 구독과 해제 토큰, owner-thread dispatch, worker copied queue, 예외·포화 metrics 및 전체 재구성 규칙은 [SceneRuntime 관찰 계약](../design/SceneObservationContract.md)에 정한다. N2/N5 어댑터는 이 확장점을 소비하며 Scene.cpp에 자체 hooks를 추가할 필요가 없다. 로컬 instance/location을 wire로 전송하지 않는다. 실제 NetEntityRegistry/권한/schema·replication 및 transport는 후속 작업이다.

[W4·ND-b 구현·검증 기록](../analysis/SceneObservationAcceptance20261022.md). NetworkHandoff는 ND-a 다음에 W4/ND-b native gate를 필수 실행하며 Product 선택 시 실제 Editor authoring/lifetime/DDOL gate도 실행한다. 원격 CI와 Dedicated Server 제품 수용은 별도로 남는다.

### N1 Archive·Parser 경계 — 2026-10-22

현재 소스에서 발견한 TemporalMotionFixture·EnhancedPbrCapture의 직접 ryml 의존 2곳을 opaque Diagnostics::Document writer로 이주하고 EnhancedSceneRenderer 호출자를 함께 수정했다. 기존 Authoring::NodeView와 로컬 archive facade 호출은 유지했다. Network에는 저작·진단 archive를 전달하지 않는다.

실제 codec Debug/Release 각 226검사, runtime 860소스/역할별 전이 1014항목, 변이 35건과 필수 회귀 등록/실패 전파를 검사한다. 새 cooked Player는 text parser 호출 0·직접 backend 참조 0·패키지 불변, 실제 PBR capture는 manifest와 첨부 7개를 검증했다. [경계 계약](../design/ArchiveParserBoundaryContract.md)과 [수용 기록](../analysis/ArchiveParserAcceptance20261022.md)을 따른다.

Product 회귀에는 명시적 새 cooked Stage가 필요하다. 전체 runtime parser 라이브러리 unlink·Dedicated Server 제품 및 N2 wire schema/transport는 별도다. Motion fixture 준비 실패는 변경 전 SDK에서도 재현되어 잔여로 보존한다.

### 서버 작업자 인계와 제품 수용

2026-10-23 [최종 인계 점검·착수 순서·검증 기준](../analysis/NetworkWorkerHandoff20261023.md): 네트워크 구현 착수는 가능하다. 실제 Dedicated Server 제품 전에는 사내 소유의 렌더 없는 SceneRuntime 링크·구동 수용과 전달 리비전 고정이 남는다. N9 조건부·미수용은 유지한다.

ND-a 계약 6종·변경 절차·금지 타입 실패 canary와 전체 회귀 배선, W4→ND-b 엔티티 수명/일반 쓰기 관찰, N1 Archive/Parser 경계 검증은 유지한다. W8은 기존 프레임 훅 dispatch 검증으로 독립 유지한다. C# Verse식 계약이나 PrSM 전체 언어 완성은 서버 인계 선행 조건이 아니다. C# 전용 표면 제거 작업은 별도 소유하며, 서버와 공유하는 수명/ABI 경계가 바뀌면 계약 동결 및 인수 gate에 반영한다.

1. N3는 사내 소유: 단일 clock, C++↔C# OnUpdate 고정 틱 계약, 명시적 tick 명령 API, 성공 커밋 observer를 먼저 수용한다. ND-a가 닫히면 담당자는 N0/N4를 제한 착수할 수 있고, N5 전체 인계는 tick 계약 확정 뒤다.
2. 30/60/144fps·0틱/다중틱·물리 없는 씬에서 같은 tick 명령의 적용 서수·상태·물리 step 수와 OnUpdate 호출 수/fixedDelta/물리 앞 순서를 비교한다. C++/C# 서명 불일치·중복 dispatch·프레임 dt 전달 변이는 실패해야 한다. 임의 Task 완료 순서나 플랫폼 간 물리 결정성까지 보장하지 않는다.
3. 제거될 Scope.Delay 전용 검사는 폐기 사유를 기록하고 lifecycle/ddol/axis/thread/generation/failure/retention/reentrancy의 수명·스레드 안전성 검사를 기본 C# 경로로 이주한다. tick 명령 순서·포화·stale 세대·pause/씬 교체·실패 커밋 거부는 별도 검증한다. 대기 검사 전체를 tick 검사로 바꾸지 않는다.
4. Editor와 신규 cooked Player에서 CCT·동적 바디·접촉·렌더 보간을 검증한다. 렌더 없는 SceneRuntime 링크/구동은 사내 경계이며 담당자의 개발 하네스와 구분한다.
5. N9 배포용 Dedicated Server는 제품 요구 범위로 추적한다. N3·transport 및 런타임/배포 경계 수용 후 cooked startup, 창/렌더 장치 없는 tick 진행, 2-client 연결, graceful shutdown/connection drain, renderer/Editor/저작 parser 패키지 의존 차단을 실제 서버 산출물로 검증한다. 현재 완료로 표시하지 않는다.

## 검증 경계

2026-10-20 고정 master의 문서 감사와 2026-10-22 현재 작업 트리의 구현·제품 수용은 별도다. C# 저작 이주의 빌드·Editor/신규 cooked Player는 [기존 수용 기록](../analysis/CSharpFixedUpdateAcceptance20261022.md), 후속 API43 명령·snapshot 배선과 최종 코드 신원은 [N3 수용 기록](../analysis/N3ExchangeAcceptance20261022.md)으로 구분한다. 서버/PrSM 전체, 대표 성능·GPU 픽셀·Debug/Shipping·다른 OS 수용으로 확대하지 않는다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-결정-요약"></span>
<span id="1-현재-소스-기준선-2026-09-04-재실측"></span>
<span id="10-외부-후보의-공식-근거"></span>
<span id="11-네트워크-계층은-아직-없다-변동-없음"></span>
<span id="12-포맷-타입-누출은-닫혔다--n1의-판정-조건이-이미-참이다"></span>
<span id="13-정체성이-네-종류인데-계약이-분리되지-않았다"></span>
<span id="14-고정-simulation-tick은-여전히-없다--다만-착지점이-한-곳으로-좁혀졌다"></span>
<span id="15-사용할-수-있는-좋은-선례가-있다-변동-없음-경로-확인"></span>
<span id="2-범위와-비범위"></span>
<span id="21-이번-계획이-해결하는-것"></span>
<span id="22-이번-계획이-의도적으로-하지-않는-것"></span>
<span id="3-목표-프로젝트와-의존-방향"></span>
<span id="31-모듈-책임"></span>
<span id="4-핵심-계약"></span>
<span id="41-archive는-공통-스키마의-서로-다른-소비자다"></span>
<span id="42-로컬-핸들과-원격-정체성"></span>
<span id="43-안정적인-network-schema"></span>
<span id="44-실제-고정-simulation-tick"></span>
<span id="45-스레드와-수명"></span>
<span id="46-상태-입력-사건의-전송-정책"></span>
<span id="47-version과-보안"></span>
<span id="5-실행-페이즈"></span>
<span id="51-순서-요약"></span>
<span id="6-이양-경계--인터페이스-계약과-확장점"></span>
<span id="61-전제--1-기준선은-갱신-완료다"></span>
<span id="610-이-절-고유의-함정"></span>
<span id="62-완전-이양의-조건"></span>
<span id="63-확장점-3종--붙일-자리는-이미-다-있다"></span>
<span id="64-동결-계약-헤더"></span>
<span id="65-이양-경계표"></span>
<span id="66-사내-1회-선행--nd-a와-nd-b"></span>
<span id="67-착수-순서"></span>
<span id="68-접점은-0이-되지-않는다--남는-셋"></span>
<span id="69-이-절이-판정하지-않는-것"></span>
<span id="7-검증-게이트"></span>
<span id="71-source-boundary"></span>
<span id="72-simulation"></span>
<span id="73-replication"></span>
<span id="74-성능운영"></span>
<span id="8-완료-기준"></span>
<span id="9-리스크와-함정"></span>
<span id="n0--기준선과-실패-게이트"></span>
<span id="n1--archiveparser-경계-밀봉-2026-09-04-실측-실질-충족"></span>
<span id="n10--hardening관측soak"></span>
<span id="n2--network-id와-schema-정본"></span>
<span id="n3--fixed-simulation-clock-위임-대상-아님--사전-작업--선행-w8"></span>
<span id="n4--loopback-transport와-메시지-프레이밍"></span>
<span id="n5--spawndespawn과-full-snapshot"></span>
<span id="n6--dirty-deltaquantizationinterest"></span>
<span id="n7--실제-transport-결정과-adapter"></span>
<span id="n8--client-predictionreconciliation-선택"></span>
<span id="n9--headless-server-target"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-현재-소스-기준선-2026-09-04-재실측"></span>
<span id="sec-10-외부-후보의-공식-근거"></span>
<span id="sec-2-범위와-비범위"></span>
<span id="sec-3-목표-프로젝트와-의존-방향"></span>
<span id="sec-4-핵심-계약"></span>
<span id="sec-5-실행-페이즈"></span>
<span id="sec-6-이양-경계--인터페이스-계약과-확장점"></span>
<span id="sec-7-검증-게이트"></span>
<span id="sec-8-완료-기준"></span>
<span id="sec-9-리스크와-함정"></span>
<span id="배선-지점-6곳"></span>
<span id="선행-관리-측-오버라이드-감지--phase-95-w8이-소유한다"></span>
<span id="실측한-배치-2026-09-04"></span>
<span id="아직-정하지-않은-것"></span>
<span id="왜-기존-훅을-옮기지-않고-신설하는가"></span>
<span id="축-간-계약--scope-클럭--postphysics-잔류-규칙--프레임-내-읽기쓰기-방향-2026-09-15"></span>

</details>
