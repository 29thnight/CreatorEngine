# 데디케이티드 서버 작업자 인계 설계

작성: 2026-10-21. 상태: 인터페이스 설계안. 아래 API 이름은 신규 제안이다.
정본 계획: [NetworkFrameworkPlan](../plans/NetworkFrameworkPlan.md) §6.
이 문서는 해당 계획의 ND-a / N3-b / ND-b와 N9를 담당자에게 넘길 구체 계약으로 풀어 쓴다.

## 1. 현재 코드와 인계 목표

현재 `Engine/SceneRuntime/RuntimeFrame.cpp`의 `Runtime::TickSimulationFrame`은
Initialization → InputEvents → 관리 PrePhysics → Physics → GameLogic → 관리 PostPhysics를
전달받은 delta로 프레임당 한 번 실행한다. pause이면 Pausing 뒤 반환한다.
고정 tick 누산, tick 시작 입력 적용, tick 완료 observer 호출은 없다.

`Engine/RuntimeHost/EngineBootstrap.h`의 InitializeRuntime은 렌더 스레드 훅을 등록하고,
Run은 WindowsCoreWindow를 생성한다. `EngineLaunchConfig.h`도 WindowsWindow와
RuntimeSettings를 포함한다. 현재 공통 bootstrap을 그대로 서버 진입점으로 쓰면
윈도우와 렌더 관련 코드가 서버에 들어간다.

`Component.cpp::OnPropertyChanged`는 이름으로 ProxyDirty를 분류해 렌더 dirty를 발행한다.
이 함수는 범용 복제 변경 스트림을 제공하지 않는다. `SceneRuntime.vcxproj`는 렌더 include
경로를 가진 static library다. 프로젝트 참조 목록만으로 최종 서버의 렌더 링크 제거를
판정하지 않고 실제 참조 심볼과 최종 링크 산출물로 닫는다.

인계 목표는 담당자가 NetCore / NetReplication / Transport / ServerHost를 소유하고,
엔진 담당자가 제공한 simulation·scene·content 경계만 사용해 서버를 완성하는 것이다.
현재 파일에는 이 설계의 구현을 추가하지 않는다.

## 2. 작업 소유권과 순서

| 순서 | 엔진 담당 선행 작업 | 완료 산출물 | 이후 서버 담당 작업 |
|---|---|---|---|
| 1 | ND-a: ID·transport·codec·telemetry·registry 계약 | 독립 include 가능한 계약, wire 경계 게이트와 금지 타입 canary | N0 계측, N4 Loopback |
| 2 | W8 → N3-b: 고정 simulation driver, 관리 OnSimulationTick | Editor/Player 공통 driver, 시작/완료 확장점, pause/catch-up 정책 | N5 spawn/full snapshot |
| 3 | W4 → ND-b: 수명·필드 쓰기 통지 | scene 이동·파괴·비렌더 필드 변경의 호출 경로 | N2 registry/schema, N6 delta/interest |
| 4 | runtime bootstrap에서 window/presentation 분리, cooked content 경계 | 렌더 없는 최소 host 링크·기동 증거 | N9 DedicatedServer host와 운영 루프 |
| 5 | N0 결과로 snapshot·queue budget 계약 확정 | 명시적 용량·초과 정책·schema manifest | N7 실제 transport, N10 통합 검증 |

기존 계획의 ND-a 2일, N3-b 4일, ND-b 2.5일은 기존 추정치다. W8/W4와 bootstrap·
headless 링크 분리를 포함한 전체 서버 준비 공수로 합산하지 않는다.
이번 인계에서는 기존 계획상 선택 항목이었던 N9를 인계 범위에 포함한다.

## 3. 모듈과 의존 방향

```text
DedicatedServer host
  ├─ Simulation driver / Runtime services / Cooked content loader
  ├─ SceneRuntime / Physics / managed gameplay
  └─ NetReplication → NetCore + Transport interface
                                  ├─ Loopback
                                  └─ 실제 transport adapter

Editor / Player → 같은 Simulation driver + 각 host의 presentation
```

계약은 네트워크 wire 계약과 프로세스 내부 engine adapter 계약으로 구분한다.
wire에는 EntityHandle·포인터·size_t·STL 객체 메모리 표현을 쓰지 않는다.
engine adapter는 simulation thread 안에서 로컬 EntityHandle을 사용할 수 있다.
span과 callback context pointer도 프로세스 내부 호출에만 사용하고 wire로 복사하지 않는다.
wire gate는 wire schema와 codec을 검사하며 로컬 adapter까지 일괄 금지하지 않는다.

## 4. 동결할 인터페이스

아래 표의 연산은 계약 표기이며 C++ 선언 확정 전이다. 실제 헤더는
NetworkFrameworkPlan §6.4의 11종을 기준으로 배치한다. 추가 engine adapter는
SceneRuntime/RuntimeHost가 소유한다.

| 계약 | 주요 연산·데이터 | 소유권·실패 규칙 |
|---|---|---|
| NetWireIds | NetworkObjectId u64, PeerId u32, component/message/field ID 고정폭 | 0 invalid, 서버 세션 내 object ID 재사용 금지. 폭·예약 범위는 manifest에 고정 |
| SimulationTick | 별도 강타입 u64 tick, session epoch, fixedDelta | frame 번호와 혼용 금지. tick은 세션 내 단조 증가 |
| INetworkTransport | Start, Poll, TrySend, RequestDisconnect, Stop | network thread 단일 소유. Poll 수신 view는 다음 Poll까지 유효. TrySend는 반환 전 복사하거나 실패 |
| NetProtocolHeader / codec | version, message ID, compatibility ID, epoch, tick, baseline, payload bit count | little endian 명시적 encode/decode. struct memcpy 금지. 길이·정수 overflow 검사 후 payload 접근 |
| INetChannelPolicy | message 의미 → reliability, ordering lane, priority | spawn/despawn/확정 사건 reliable, 상태 latest-wins. lane 간 도착 순서는 별도로 보장하지 않음 |
| INetTelemetrySink | 값 counters와 queue high-water snapshot | hot path 문자열 구성·동기 로깅 금지. overflow 원인별 계수 |
| INetworkEntityRegistry | Bind, Rebind, Unbind, TryResolve | simulation thread 전용. 중복 Bind 실패, 미등록 resolve 실패. transport 호출 금지 |
| INetAuthorityPolicy | Evaluate(peer, object ID, command ID, tick) | allow/deny와 사유 값 반환. spawn/RPC 명시 allowlist |
| INetCommandSink | TryEnqueue(command envelope, payload lease) | 성공하면 소유권 이전, 실패하면 호출자 유지. Full/TooLarge/Closed 구분 |
| ReplicationSnapshot | epoch, tick, schema ID, object records, immutable payload lease | 소비 완료까지 소유 buffer 유지. latest-wins 대상은 상태만 |
| NetReplicationPolicy | 조건·quantizer·validation·보간 정책 | 값 입력/출력. reflgen opt-in, bounds 없는 가변 필드 거절 |

PayloadBitCount와 실제 수신 byte 수를 교차 검사하고, 알 수 없는 message ID·예약 비트·
NaN/Inf·미래 tick·상한 초과 container는 world 변경 전에 거절한다.
TrySend 실패 시 reliable 메시지는 bounded pending에 유지하고 재시도한다.
pending 상한 초과는 해당 peer를 종료한다. unreliable 상태는 폐기하고 계수한다.
transport 자체의 ACK·분할 기능은 adapter 뒤에서 사용한다.

## 5. 엔진이 제공할 호출 경계

### 5.1 tick 시작과 완료

`OnTickCommitted`만으로는 inbound 명령을 물리 전에 적용할 수 없다.
N3-b에 다음 두 경계를 함께 제공한다.

1. `OnTickStarting(TickContext)`: session/scene 구조 변경 반영 뒤, 해당 tick의 gameplay·
   physics 실행 전 호출한다. bounded 입력을 drain하고 NetId 해석·권한 검사 후 적용한다.
2. `OnTickCommitted(TickContext)`: 물리 작업 join, 해당 tick의 권한 gameplay와 구조 변경
   commit을 마친 뒤 호출한다. 이 경계에서 불변 snapshot을 만든다.

둘 모두 simulation thread에서 동기 호출한다. callback은 I/O 대기나 scene 재진입을
하지 않는다. observer 등록·해제는 tick 밖에서만 허용하며 해제 반환 뒤 callback은 없다.
네트워크 상태는 완료 tick 값만 공개한다.

프레임 축 Initialization/InputEvents와 기존 관리 PrePhysics/PostPhysics/GameLogic을
tick마다 반복하지 않는다. 권한 gameplay·root motion·AI·충돌 사건을 고정 축으로
옮길 소비자 목록을 N3-b에서 확정한다. GameLogic은 표시 데이터 갱신을 포함하므로
전체 함수를 누산 루프에 넣지 않는다. OnSimulationTick은 생명주기 enum에 추가하지
않고 ClrHost 전용 진입점으로 배선한다.

tick rate·최대 catch-up 횟수·frame delta 상한은 세션 시작 설정으로 고정한다.
서버는 실행하지 않은 tick을 완료 처리하지 않는다. 누적 지연은 backlog와 overrun으로
계수하고 tick budget 소진 시 다음 host 반복에서 계속 실행한다.
pause는 simulation tick을 멈추지만 transport poll·disconnect·종료 처리는 계속 실행한다.
새 세션은 epoch를 바꾸고 이전 명령·snapshot·baseline을 무효화한다.

### 5.2 엔티티 수명

`Scene::AllocateSlot`/`ReleaseSlot`은 슬롯 경계다. snapshot 공개 가능한 spawn 완료
경계와 같다고 취급하지 않는다. 다음 의미를 엔진이 보장해야 한다.

| 통지 | 시점 | 데이터 |
|---|---|---|
| Bound | 초기 component/state 적용과 spawn commit 뒤 | local handle, local lifetime token, prefab asset identity |
| Rebound | DDOL/scene transfer의 새 handle 확정 뒤 | lifetime token, old/new handle |
| Unbound | 최종 파괴 시 resolve가 닫히는 commit | lifetime token, old handle, destroy 사유 |

이송 중 슬롯 해제를 최종 despawn으로 전달하지 않는다. detach와 attach 사이에는
resolve 실패 상태를 유지하고 동일 lifetime token으로 Rebind한다. 기존 NetId는 유지한다.
실패한 이송의 최종 파괴는 Unbound 한 번으로 끝낸다. raw Entity*를 identity로 보관하지 않는다.
local lifetime token 발급과 transfer 상관관계는 엔진 소유, NetId 발급은 서버 담당 소유다.

### 5.3 쓰기 변경

`OnComponentFieldWritten(local handle, component identity, field key, source, revision)`을
게임 스레드 commit 경계에서 발행한다. engine field key는 wire NetFieldId와 분리하고
복제 schema adapter가 변환한다. 렌더 ProxyDirty를 네트워크 dirty로 해석하지 않는다.

Inspector/Undo·직렬화 load·native setter·managed setter·physics writeback·animation
writeback 경로별로 호출 지점을 확인한다. virtual OnPropertyChanged override의 base 호출
유무에 관측이 좌우되지 않도록 공통 writer가 통지를 소유한다. 직접 필드 쓰기는
writer 경계로 옮기거나 명시적 component revision 갱신을 붙인다.
같은 tick의 반복 쓰기는 component/field 단위로 합친다. 초기 spawn state는 full snapshot에
포함한다. 클라이언트 수신 적용 source는 echo 재발행을 막되 서버 권한 상태 변경은 기록한다.

## 6. 버퍼와 스레드 계약

network thread와 simulation thread 사이에는 각각 하나의 명령/상태 소비자를 둔다.
producer가 하나면 bounded SPSC, 여러 producer가 필요한 경로만 bounded MPSC를 쓴다.
큐 메모리와 payload pool은 시작 시 예약하고 tick마다 임시 vector를 만들지 않는다.
pool slot은 index와 generation을 가진 lease로 전달하고 소비 종료 시 반환한다.

| 통로 | 포화 처리 | 반드시 보존할 정보 |
|---|---|---|
| 입력 상태 | peer/input sequence별 최신값과 중복 window 정책 | 마지막 적용 sequence, stale/duplicate/drop 수 |
| reliable 명령·spawn/despawn·사건 | bounded pending → backpressure → peer 종료 | 적용 전까지 payload 소유권과 사건 순서 |
| 상태 snapshot | 미소비 최신 상태로 교체 | superseded 수, tick/epoch, 독립 reliable 사건 stream |

상태가 spawn보다 먼저 도착하면 아직 알려지지 않은 object 상태를 bounded 보류하거나
폐기한다. reliable spawn 적용과 baseline 확보 뒤 복제를 재개한다. 서로 다른 lane의
전송 순서만으로 spawn-before-state를 보장하지 않는다.
delta baseline은 수신측 적용 확인된 tick만 사용한다. baseline 유실은 full snapshot으로
복구한다. latest-wins 교체로 사라진 snapshot을 baseline으로 삼지 않는다.

## 7. 서버 host와 content 경계

엔진 담당자는 window/presentation 초기화를 Runtime services 초기화에서 분리한다.
`ServerLaunchConfig`는 content root, scene/prefab manifest, tick 설정, log 경로만
소유하고 WindowsWindowDesc나 render backend를 요구하지 않는다.

공통 bootstrap은 diagnostics·jobs·scene·physics·managed runtime의 시작/종료를 제공한다.
서버 담당자는 console host, monotonic clock 기반 deadline 대기, transport poll,
graceful stop 요청, 실행 옵션·종료 코드를 구현한다.

content loader는 cooked scene/prefab 식별자와 결과 코드만 받는다. 서버 패키지는
게임플레이·충돌·script에 필요한 자산을 포함하고 GPU upload·material 생성 없이 읽힌다.
authoring Document를 보관하는 경로를 서버 load가 타지 않도록 분리한다.
managed gameplay assembly의 presentation 의존도 같은 기준으로 제거한다.

종료 순서는 새 입력 차단 → transport poll/send 중단 및 thread join →
남은 명령·snapshot drain/discard 계수와 lease 반환 → observer 해제 → replication/registry
해제 → Scene/managed/physics 해제 → jobs/diagnostics 해제다.
서버 simulation이 멈춘 후에도 살아 있는 network callback을 남기지 않는다.

## 8. 인계 완료 판정과 전달 묶음

| 게이트 | 실행 내용 | 통과 조건 |
|---|---|---|
| 계약 | 헤더 단독 compile, wire 금지 타입 변이 canary | 정상 계약 통과·금지 계약 실패, run-all 배선 |
| 고정 tick | 동일 입력을 서로 다른 render FPS, pause, 긴 frame으로 재생 | 같은 완료 tick/input/physics step 결과, catch-up 정책 일치 |
| 수명 | spawn·deferred/immediate destroy·DDOL·실패한 transfer | Bind/Rebind/Unbind 순서와 stale resolve 거절 |
| 변경 | 비렌더 component 포함 각 writer 경로 | 변경 누락 0, 중복 합침, 수신 echo 차단 |
| 큐 | 모든 queue/pool을 작은 용량으로 포화 | 무한 증가·lease 누수 0, 사유별 계수 일치 |
| 복제 | loss/reorder/duplicate, state-before-spawn, baseline 유실 | 잘못된 object 적용 0, full 복구, reliable 사건 보존 |
| headless | 렌더 소스 membership·링크 map·PE import 및 실제 기동 검사 | Editor/ImGui/RenderEngine/authoring parser 의존 0, 창·GPU device 생성 0 |
| 통합 | 서버와 두 client의 접속→spawn→입력→상태→despawn→종료 | 권한 거절·정상 동기화·자원 원복 |

담당자에게 전달할 묶음은 계약 헤더, schema/ID/tombstone manifest, 엔진 adapter 예제,
경계 회귀 명령, cooked 테스트 scene, 위 게이트 결과, 모듈별 파일 소유권 표다.
계약 변경 요청에는 영향 소비자·wire compatibility·회귀 변경을 함께 적는다.
새 wire ID와 tombstone 변경은 정본 NetworkFrameworkPlan에 기록한다.

현재 완료된 산출물은 이 설계 문서다. 계약 헤더 구현·엔진 확장점 배선·빌드·실행
게이트는 위 순서대로 수행할 후속 작업이다.
