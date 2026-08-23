# 네트워크 준비형 런타임 · Replication 기반 계획 (PHASE 20)

작성: 2026-08-22 · 범위: 실시간 멀티플레이를 붙일 수 있는 런타임 경계, 고정
Simulation Tick, 네트워크 정체성, Replication, Transport adapter.

관련 문서: [SerializationPlan](SerializationPlan.md)(저작 YAML·쿠킹 바이너리·Archive
경계), [SceneGraphRedesignPlan](SceneGraphRedesignPlan.md)(EntityHandle·Scene 수명주기),
[EngineLayerSeparationPlan](EngineLayerSeparationPlan.md)(Runtime/Core 물리 경계),
[PhysicsRedesignPlan](PhysicsRedesignPlan.md)(고정 스텝 소비자·스레딩),
[ReflectionRedesignPlan](ReflectionRedesignPlan.md)(macro-free canonical schema).

이 문서는 **네트워크 라이브러리를 당장 링크하는 계획이 아니다.** 먼저 엔진 상태와
시뮬레이션을 Transport로부터 분리하고, 소켓 없이 Loopback에서 계약을 증명한 다음
실제 Transport를 붙인다. 기본 모델은 **서버 권한형 실시간 게임**이다. P2P, rollback,
전용 서버는 같은 경계를 소비하는 후속 단계로 둔다.

---

## 0. 결정 요약

1. **ryml/YAML은 Editor authoring 전용이다.** 네트워크 hot path와 Player/Server
   replication에는 YAML/JSON DOM을 넣지 않는다.
2. **스키마 순회만 공유한다.** `meta::schema` 위에 `AuthoringArchive`,
   `CookedArchive`, `NetworkArchive`를 서로 다른 정책으로 둔다.
3. **`EntityHandle`은 wire에 쓰지 않는다.** 서버가 발급하는 `NetworkObjectId`와
   프로세스 로컬 `EntityHandle` 사이에 명시적 레지스트리를 둔다.
4. **현재 타입명 기반 `typeID`도 wire에 쓰지 않는다.** 고정 폭의 명시적
   `NetComponentTypeId`, `NetMessageTypeId`, `NetFieldId`를 별도로 관리한다.
5. **현재의 프레임당 1회 가변 `FixedUpdate`를 실제 고정 Simulation Tick으로
   바꾼다.** Render frame과 Simulation tick을 분리한다.
6. **Network Thread는 live Scene을 만지지 않는다.** 검증된 값 명령과 불변
   snapshot만 bounded queue로 교환한다.
7. **상태와 사건을 분리한다.** 상태 snapshot은 unreliable/latest-wins,
   spawn/despawn·확정 사건은 reliable ordered가 기본이다.
8. **Transport는 마지막에 고른다.** 기능 우선 기본 후보는
   GameNetworkingSockets(GNS), 최소 의존 후보는 ENet이며, 엔진에는 좁은
   message-oriented adapter만 노출한다.
9. **쿠킹 포맷과 wire 포맷은 같지 않다.** scalar codec 일부는 공유할 수 있지만,
   network는 tick·baseline·quantization·권한·상한 검증을 별도 계약으로 가진다.
10. **리플렉션 메서드는 자동 RPC가 아니다.** RPC와 spawn 가능 타입은 명시적
    allowlist로 등록한다.

---

## 1. 현재 소스 기준선 (2026-08-22)

### 1.1 네트워크 계층은 아직 없다

- `vcpkg.json`에는 GNS, ENet, Asio 등의 게임 Transport가 없다.
- 연결·peer·packet·replication·RPC·interest management를 소유하는 Runtime
  모듈도 없다.
- 따라서 지금은 기존 API와 라이브러리를 감싸는 단계가 아니라 **정체성·틱·스레드
  경계를 먼저 세울 수 있는 시점**이다.

### 1.2 포맷 타입이 Runtime 객체까지 새어 있다

2026-08-22 PowerShell 전수 검색 기준:

| 항목 | 현재 값 |
|---|---:|
| `YAML::`/`MetaYml::`/yaml-cpp 직접 소비 | 43개 소스 파일 · 268 matches |
| `nlohmann::json` 직접 소비 | 16개 소스 파일 · 44 matches |
| 장기 보관 Node | 4곳 |

장기 보관 지점은 `Prefab::m_prefabData`, `Entity::m_prefabOriginal`,
`GameObjectCommand::m_serializedNode`, `SceneManager::m_editorSceneBackup`이다.
`Entity.h`가 `yaml-cpp/yaml.h`를 직접 포함하며, `ComponentFactory::LoadComponent`는
`MetaYml::detail::iterator_value`를 받는다. 이 상태에서는 headless Server나
Network-only 테스트도 authoring parser에 컴파일 의존한다.

### 1.3 정체성이 세 종류인데 계약이 분리되지 않았다

| 정체성 | 현재 의미 | wire 판정 |
|---|---|---|
| `EntityHandle {sceneId,index,generation}` | 한 프로세스의 Scene 슬롯 | **금지** |
| `FileGuid` | 저장 자산의 영속 정체성 | spawn descriptor에서 사용 가능 |
| `Meta::Type::typeID`/`HashedGuid` | 타입명 기반 FNV 조회 키 | **wire ID로 금지** |

`sceneId`는 프로세스 안에서 생성되는 일련번호이고, 서로 다른 피어의 슬롯 배치는
일치하지 않는다. 타입 ID도 타입명·컴파일러 표기에 결합하며 `size_t`라 wire 고정
폭이 아니다. 둘을 그대로 보내면 같은 빌드에서도 원격 정체성을 보장하지 못하고,
리네임은 프로토콜 변경이 된다.

### 1.4 `FixedUpdate`는 현재 실제 fixed timestep이 아니다

`PlayerMain::Update`는 매 render frame에서 얻은 가변 `deltaSeconds`를
`SceneManagers->Physics`와 `GameLogic`에 한 번씩 전달한다. `Scene::FixedUpdate`
주석도 고정 timestep 누산기로 걸러지지 않고 프레임당 정확히 한 번임을 명시한다.
SceneGraph 트랙 L5는 `PrePhysics/PostPhysics` 훅과 `OnSimulate` 수명 계약은
완료했지만, **fixed-rate simulation clock**은 세우지 않았다.

### 1.5 사용할 수 있는 좋은 선례가 있다

- `ProxyCommandQueueController`는 생산 스레드의 staging을 값 `Batch`로 밀봉하고
  소비 스레드가 frame/epoch 경계에서 적용한다.
- `EnhancedLiveDisplaySnapshot`은 포인터·RHI 객체 없이 불변 값만 다른 스레드에
  공개한다.
- `MetaSchema.h`는 std-only canonical schema이며 `field.with(attr...)` 확장점을
  이미 제공한다.

PHASE 20은 새 전역 자료구조를 먼저 만들기보다 이 세 계약을 network 입출력에
재사용한다.

---

## 2. 범위와 비범위

### 2.1 이번 계획이 해결하는 것

- Server-authoritative simulation의 고정 tick 경계
- 원격 객체 정체성과 로컬 Entity 수명 매핑
- macro-free replication schema와 안정적인 wire ID
- bounded inbound/outbound queue 및 스레드 소유권
- full snapshot, delta, quantization, interest management의 이행 순서
- reliable/unreliable message 분류와 Transport adapter
- 프로토콜 handshake, schema hash, hostile-input 검증
- Loopback·packet loss/latency simulation·headless smoke 게이트

### 2.2 이번 계획이 의도적으로 하지 않는 것

- matchmaking, 계정, 로비, 상점, 데이터베이스, 운영 backend
- 음성 채팅과 대형 자산 전송
- 모든 게임을 위한 범용 RPC 언어
- 자동으로 모든 `[[Property]]`를 복제하는 기능
- 처음부터 rollback/lockstep 결정론을 완성하는 일
- 네트워크를 이유로 SceneGraph나 물리 backend를 다시 선택하는 일
- 실제 요구량 측정 전 패킷 압축 라이브러리를 추가하는 일
- `EntityHandle` 또는 raw pointer의 wire 직렬화

---

## 3. 목표 프로젝트와 의존 방향

```text
CreatorEditor
  └─ SerializationAuthoring (ryml/YAML)
          │
          ▼
Foundation / MetaSchema  ◄───────────────┐
          │                              │
          ├─ SerializationCooked         │
          ├─ SceneRuntime / PhysicsCore  │
          └─ NetCore ──► NetReplication ─┘
                           │
                           ▼
                    INetworkTransport
                      ├─ Loopback
                      ├─ GNS adapter
                      └─ ENet adapter (선택)

PlayerRuntime ─────────► SceneRuntime / NetReplication / Transport
ServerRuntime ─────────► SceneRuntime / NetReplication / Transport
                         (Editor/ImGui/RenderCore/ryml 금지)
```

초기 단계에서 새 Visual Studio 프로젝트를 빈 껍데기로 만들지 않는다. 먼저 기존
프로젝트 안에서 include/API 경계를 세우고, `EngineLayerSeparationPlan` E6에서 소스
편입과 링크 경계를 증명할 수 있을 때 물리 프로젝트로 승격한다.

### 3.1 모듈 책임

| 모듈 | 책임 | 금지 의존 |
|---|---|---|
| `MetaSchema` | 타입·필드의 canonical descriptor | YAML, packet, socket, Editor |
| `SerializationAuthoring` | ryml 문서 소유·YAML load/save·Editor node edit | Transport |
| `SerializationCooked` | 재생성 가능한 runtime asset binary | socket, peer 상태 |
| `NetCore` | 고정 폭 ID, bounded reader/writer, protocol header, queue 값 타입 | Scene pointer, YAML/JSON |
| `NetReplication` | NetId 매핑, spawn/despawn, snapshot, baseline, interest | socket 구체 타입 |
| `NetTransport*` | 연결, message send/receive, lane, 통계 | Entity/Component/Prefab |
| `ServerRuntime` | fixed tick host, 권한 판정, replication publish | Editor, ImGui, authoring parser, RenderCore |

---

## 4. 핵심 계약

### 4.1 Archive는 공통 스키마의 서로 다른 소비자다

```cpp
template<class Archive, class T, class Policy>
void VisitFields(Archive& archive, T& value, Policy policy);
```

| 소비자 | 필드 선택 | 표현 |
|---|---|---|
| `AuthoringArchive` | 저장 대상 필드 | 이름 있는 map/sequence/scalar |
| `CookedArchive` | runtime에 필요한 저장 필드 | layout hash + 키 없는 binary |
| `NetworkArchive` | `replicated_attr` opt-in 필드만 | stable field ID + bitstream |

`AuthoringDocument`의 generic mutable tree는 Prefab 편집·Undo·구조 diff를 위한
Editor 자료구조다. Network serializer가 그 Node 계층을 통과하지 않는다.

### 4.2 로컬 핸들과 원격 정체성

첫 구현은 정확성을 우선해 서버 세션 동안 재사용하지 않는 64-bit ID를 쓴다.

```cpp
struct NetworkObjectId
{
    uint64_t value{ 0 }; // 0 = invalid
};

class NetworkEntityRegistry
{
public:
    bool Bind(NetworkObjectId id, EntityHandle local);
    void Rebind(NetworkObjectId id, EntityHandle moved);
    void Unbind(NetworkObjectId id);
    EntityHandle Resolve(NetworkObjectId id) const;
};
```

- 서버만 runtime `NetworkObjectId`를 발급한다.
- Scene 이동/DDOL로 `EntityHandle`이 바뀌면 registry만 `Rebind`한다.
- spawn message는 `NetworkObjectId + Prefab FileGuid + initial state`를 갖는다.
- despawn 적용과 Entity 파괴 완료 시점의 순서를 수명주기 게이트로 고정한다.
- 대역폭이 실제 병목일 때만 connection-local short ID/varint dictionary를 추가한다.

### 4.3 안정적인 network schema

`MetaSchema`의 기존 `.with(...)`에 macro 없이 속성을 추가한다.

```cpp
meta::field<&Self::m_health>.with(
    meta::replicated(
        1, // stable NetFieldId; 삭제 후 재사용 금지
        meta::quantize_u16(0.0f, 100.0f),
        meta::replication_condition::everyone))
```

필수 속성:

- `NetFieldId`: 타입 안에서 안정적인 명시 ID
- quantizer 또는 exact codec
- replication condition: everyone/owner/relevant/initial-only
- interpolation policy: none/linear/slerp
- validation bounds: 문자열/배열 최대 길이, 수치 범위

컴포넌트와 메시지도 이름 hash가 아니라 고정 폭의 명시 ID를 가진다. schema build가
중복 ID, 재사용된 tombstone, 지원하지 않는 타입, 상한 없는 container를 compile/test
단계에서 실패시킨다. persistence serializer는 network 속성을 무시한다.

### 4.4 실제 고정 Simulation Tick

```text
Render frame
  Time accumulation
    while accumulator >= fixedDelta:
      Drain validated commands(tick)
      Apply input/authority decisions
      PrePhysics(fixedDelta)
      Physics(fixedDelta)
      PostPhysics/GameLogic(fixedDelta)
      Commit spawn/despawn
      Capture replication snapshot(tick)
      tick++
  Render(interpolationAlpha)
```

- fixed rate 값은 N3 계측에서 정하고 하드코딩하지 않는다.
- pause, scene switch, time scale, long-frame catch-up 상한을 명시한다.
- 네트워크 tick은 render frame number와 다른 타입을 쓴다.
- dedicated server는 같은 tick driver를 render 없이 실행한다.
- client는 snapshot interpolation buffer를 가지며, prediction은 N8 전에는 선택하지
  않는다.

### 4.5 스레드와 수명

```text
Transport/Network Thread
  packet bounds 검증 + message decode
  → InboundCommand(value only)
  → bounded MPSC/SPSC queue

Game/Simulation Thread
  tick 시작에 inbound drain
  → NetId resolve + authority 검증 + state 적용
  → tick 끝에 immutable ReplicationSnapshot publish

Replication/Network Thread
  snapshot consume
  → peer별 interest/baseline/delta encode
  → Transport send
```

규칙:

- queue에는 raw pointer, `EntityHandle`의 원격 의미, `ryml::NodeRef`를 넣지 않는다.
- inbound reliable command queue가 상한을 넘으면 무한 확장 대신 backpressure 또는
  연결 종료 정책을 적용한다.
- snapshot queue는 latest-wins가 가능하며 superseded/drop을 계수한다.
- shutdown은 `Transport stop → queue drain/discard 계수 → replication teardown →
  Scene teardown` 순으로 한다.
- 모든 queue는 enqueued/applied/dropped/pending/invalid/unauthorized를 관측한다.

### 4.6 상태, 입력, 사건의 전송 정책

| 종류 | 기본 정책 | 비고 |
|---|---|---|
| handshake/auth/schema | reliable ordered | 호환 실패는 접속 거절 |
| spawn/despawn | reliable ordered | 상태보다 먼저 도착해야 함 |
| inventory/quest/확정 RPC | reliable ordered | 권한 검사 필수 |
| transform/animation snapshot | unreliable sequenced | latest wins |
| player input | unreliable sequenced + 최근 N개 중복 | server tick에 귀속 |
| chat | reliable, 낮은 lane 우선순위 | simulation과 분리 |
| asset/patch | 별도 서비스/stream | replication lane 금지 |

위 표는 application 의미다. GNS를 쓰면 packet ACK·fragmentation을 다시 구현하지
않고 해당 message/lane 기능에 매핑한다.

### 4.7 version과 보안

application message header의 최소 필드:

```text
ProtocolVersion
MessageTypeId
SchemaHash 또는 CompatibilityId
ServerTick
BaselineTick (snapshot일 때)
PayloadBitCount
```

- 같은 배포판끼리만 허용하는 초기 정책은 schema hash 불일치 시 즉시 접속 거절한다.
- mixed-version 운영이 실제 요구되기 전에는 optional-field migration 엔진을 만들지
  않는다.
- 모든 정수는 고정 폭이고 endian을 명시한다.
- 배열·문자열·component 수·spawn 수·RPC 빈도에 상한을 둔다.
- NaN/Inf, 범위 밖 enum, 존재하지 않는 NetId, 미래 tick input을 거절/계수한다.
- 수신한 type/message ID가 `ComponentFactory`나 `Meta::Method`를 임의 호출하지
  못하도록 별도 allowlist를 사용한다.
- Transport 암호화는 application 권한 검사를 대체하지 않는다.

---

## 5. 실행 페이즈

각 슬라이스는 구현과 함께 build, source boundary gate, loopback/runtime gate,
측정치를 남긴다. 완료 기록은 “코드 존재”가 아니라 아래 판정 기준을 만족할 때만
붙인다.

### N0 — 기준선과 실패 게이트

**목적:** 아직 없는 네트워크 성능을 추정치로 최적화하지 않는다.

- 현재 frame delta/Physics 호출 횟수와 최악 long-frame을 계측한다.
- 대표 Scene의 Entity/Component 수와 매 tick 변경량을 계측한다.
- `EntityHandle`, `HashedGuid`, YAML/JSON Node가 wire type에 들어오면 실패하는
  source gate를 만든다.
- packet size, encode/decode time, alloc count, queue depth, drop count의 공통
  telemetry 구조를 먼저 정의한다.

**판정:** 측정 명령과 기준선 표가 이 문서에 추가되고, 금지 타입을 넣은 canary가
gate를 실제로 붉게 만든다.

### N1 — Archive·Parser 경계 밀봉

**선행:** PHASE 17 D3-a/D3-b/D4.

- `MetaSchema`를 format-neutral canonical schema로 유지한다.
- `Entity.h`, `ComponentFactory`, Runtime interface에서 YAML/JSON 타입을 제거한다.
- `AuthoringDocument`는 ryml `Tree`를 소유하고 Node view의 수명을 문서 아래로
  제한한다.
- `AuthoringArchive`와 `CookedArchive`가 같은 field visitor를 소비함을 canary로
  증명한다.

**판정:** `SceneRuntime/NetCore` 후보 헤더의 yaml-cpp/nlohmann include 0,
authoring/cooked 왕복 골든 통과.

### N2 — Network ID와 schema 정본

**선행:** EntityHandle/Scene slot lifecycle(현재 충족), N1의 format-neutral schema.

- `NetworkObjectId`, `Net*TypeId`, `NetFieldId`, registry를 추가한다.
- `.with(meta::replicated(...))` 속성과 schema validation을 추가한다.
- component/message/RPC ID tombstone 목록을 버전 관리한다.
- DDOL detach/attach, scene transfer, deferred/immediate destroy에서 Bind/Rebind/Unbind
  순서를 검증한다.

**판정:** 서로 다른 두 Scene에서 같은 local index가 있어도 NetId resolve가 섞이지
않고, stale/despawn ID가 fail-close하며, ID 중복 canary가 실패한다.

### N3 — Fixed Simulation Clock

**선행:** SceneGraph L5 완료(현재 충족). PHASE 19 T0과 hot zone 조정.

- render clock과 simulation clock을 분리한다.
- accumulator, catch-up 상한, pause/scene-switch/time-scale 규칙을 구현한다.
- `PrePhysics → Physics → PostPhysics/GameLogic → replication capture` 순서를
  Editor/Player/Server 공통 driver로 만든다.
- C# `SimulationScope.Delay`와 managed crossing이 frame 횟수가 아니라 simulation
  tick 계약과 일치하는지 골든을 추가한다.

**판정:** 가변 render FPS에서 같은 입력 tick 수와 물리 step 수가 나오며, 한 프레임
stall 뒤 catch-up 상한과 drop/debt 계수가 예상값과 일치한다.

### N4 — Loopback Transport와 메시지 프레이밍

**선행:** N2. N3와 병행 가능하나 snapshot tick 통합은 N3 이후.

- `INetworkTransport`의 최소 message-oriented API를 정의한다.
- 메모리 Loopback adapter, bounded reader/writer, protocol header, handshake를
  구현한다.
- latency/loss/reorder/duplicate/corruption simulator를 테스트 adapter에 넣는다.
- 실제 socket/library는 아직 추가하지 않는다.

**판정:** malformed length/type/version은 crash 없이 거절되고, loss/reorder 조건에서
reliable control과 unreliable latest-wins의 차이가 자동 테스트로 증명된다.

### N5 — Spawn/Despawn과 Full Snapshot

**선행:** N2+N3+N4.

- server-authoritative spawn/despawn lifecycle을 만든다.
- initial full snapshot을 reflection visitor로 작성한다.
- snapshot은 live Entity pointer가 아닌 값 구조로 tick 끝에 밀봉한다.
- client interpolation buffer와 out-of-order/stale snapshot 폐기를 추가한다.

**판정:** 접속 중간 join, Scene 이동, DDOL, prefab spawn, 파괴가 Loopback 2-peer에서
일치하고 stale snapshot이 부활을 만들지 않는다.

### N6 — Dirty Delta·Quantization·Interest

**선행:** N5의 correctness gate.

- 매 tick 전체 리플렉션 비교를 제거하고 component revision/dirty mask를 쓰기
  창구에 연결한다.
- peer별 acknowledged baseline과 delta를 관리한다.
- position/rotation/health 등 실제 분포로 quantizer를 결정한다.
- owner/relevant/initial-only 조건과 최소 spatial interest grid를 추가한다.

**판정:** full snapshot 대비 bytes/tick, encode time, allocation이 수치 개선되고,
누락 dirty canary와 baseline loss 복구가 통과한다. 목표치는 N0/N5 실측 뒤 이 문서에
기입한다.

### N7 — 실제 Transport 결정과 adapter

**선행:** N4~N6. Transport 없이 replication correctness가 먼저 서 있어야 한다.

동일한 N6 workload와 loss simulator로 후보를 비교한다.

| 후보 | 기본 판정 |
|---|---|
| GameNetworkingSockets | 기능 우선 기본 후보. reliable/unreliable message, fragmentation, encryption, lane, NAT/P2P가 필요할 때 채택 |
| ENet | dedicated-server 직접 UDP와 작은 의존성이 우선이고 encryption/NAT를 별도 해결할 때 채택 |
| raw UDP/Asio | transport protocol 자체가 제품 목표가 아니므로 기본 기각 |

측정 항목: RTT/packet loss별 latency, CPU, alloc, bytes, queue/backpressure, DLL 및
transitive dependency, Windows Debug/Release 패키징.

**판정:** 결정 기록과 A/B 결과를 남기고 선택한 adapter 외 구체 API가 NetCore에
노출되지 않는다.

### N8 — Client Prediction·Reconciliation (선택)

**선행:** N3+N5, gameplay 요구 확정.

- local input history와 authoritative state history를 tick-keyed ring buffer로 둔다.
- locally-owned movement만 예측하고 authority state로 rewind/replay한다.
- Physics backend가 목표 gameplay에서 재현 가능한 범위를 측정한다.
- 완전 결정론이 필요하지 않다면 remote entity는 interpolation을 유지한다.

**판정:** 목표 RTT/loss에서 correction magnitude와 visible snap 빈도가 수치 기준을
만족한다. 요구가 없으면 만들지 않는다.

### N9 — Headless Server target

**선행:** EngineLayerSeparation E6, PHASE 17 D6, N3, N7.

- Editor/ImGui/RenderCore/ryml 없이 SceneRuntime·PhysicsCore·ScriptRuntime·NetCore를
  링크하는 Server target을 만든다.
- startup scene은 cooked manifest/binary에서만 연다.
- graceful shutdown과 connection drain 순서를 smoke로 검증한다.

**판정:** Server PE import 및 패키지에 Editor/ImGui/renderer/ryml/yaml-cpp/nlohmann
의존 0, render window 없이 일정 tick rate로 2-client smoke 통과.

### N10 — Hardening·관측·Soak

**선행:** N7, 필요 시 N8/N9.

- fuzz/invalid packet corpus, rate limit, queue saturation, reconnect, timeout을 자동화한다.
- packet/tick/peer별 capture와 schema-aware decode 도구를 DeveloperTools에 둔다.
- 장시간 soak에서 NetId/Entity/queue/packet buffer 누수와 generation ABA를 검사한다.

**판정:** 정한 peer 수와 지속시간의 soak 통과, queue 불변식과 resource counter 원복,
corrupt corpus crash 0.

### 5.1 순서 요약

```text
PHASE 17 D3-a/D3-b/D4 ─► N1 ─► N2 ─► N4(loopback) ─┐
                                    └───────────────┼─► N5 ─► N6 ─► N7 ─► N9/N10
SceneGraph L5 ─────────────────► N3 ────────────────┘

N8(prediction)는 N3+N5 뒤 gameplay 요구가 있을 때만
```

N0는 즉시 가능하다. N2와 N3도 담당 파일이 겹치지 않으면 병행할 수 있다. N7에서
실제 외부 네트워크 라이브러리를 추가하기 전까지는 dependency manifest를 늘리지 않는다.

---

## 6. 검증 게이트

### 6.1 Source boundary

- `NetCore/NetReplication/ServerRuntime`에서 yaml-cpp, nlohmann, ryml include 0
- wire 구조체에서 `size_t`, pointer, `EntityHandle`, STL ABI container 직렬화 0
- Transport 구체 타입이 adapter 밖에 노출되는 include 0
- replicated field ID/component ID/message ID 중복 0
- 상한 없는 수신 문자열/배열/container 0

### 6.2 Simulation

- 동일 input stream을 서로 다른 render FPS로 재생해 simulation tick/physics step
  수 일치
- PrePhysics/Physics/PostPhysics/commit/snapshot 순서 골든
- pause, scene switch, DDOL, long-frame catch-up 골든
- Server와 Player host가 같은 tick driver를 소비

### 6.3 Replication

- spawn-before-state, despawn-after-last-state 순서
- stale NetId fail-close와 ID 미재사용
- full snapshot/delta 동일성
- packet loss/reorder/duplicate에서 baseline 복구
- owner/relevant 조건과 interest enter/leave
- invalid/unauthorized input 적용 0

### 6.4 성능·운영

- bytes/peer/tick, encode/decode µs, alloc/tick, queue depth/drop, RTT/jitter/loss
- 목표 peer 수에서 simulation budget 초과 0
- reliable queue 무한 증가 0
- latest snapshot superseded가 정상 계수됨
- shutdown 뒤 NetId/queue/packet buffer/connection resource 원복

---

## 7. 완료 기준

PHASE 20 전체 완료는 다음이 모두 참일 때다.

1. Authoring YAML/JSON Node가 Runtime network 경계에 나타나지 않는다.
2. `EntityHandle`과 타입명 기반 `typeID`가 wire에 기록되지 않는다.
3. Editor/Player/Server가 같은 고정 Simulation Tick 계약을 소비한다.
4. Network Thread가 live Scene 객체를 직접 접근하지 않는다.
5. Loopback과 실제 Transport에서 동일한 spawn/snapshot/delta/despawn 테스트가 통과한다.
6. malformed/unauthorized packet이 fail-close하며 상한 없는 allocation이 없다.
7. 외부 Transport는 adapter 한 곳에 격리된다.
8. N0 대비 bandwidth/CPU/allocation 수치와 목표 peer smoke/soak 결과가 기록된다.
9. headless Server가 제품 요구라면 Editor/Render/authoring parser 의존 없이 빌드·실행된다.

N8 prediction이나 P2P는 제품 요구가 없으면 전체 완료 조건이 아니다.

---

## 8. 리스크와 함정

- **Generic serializer 재사용 과잉:** persistence의 모든 필드를 자동 복제하면 비밀
  데이터·Editor 상태·대형 container가 wire에 샌다. network는 opt-in만 허용한다.
- **Reflection diff를 hot path로 착각:** full snapshot 성공 뒤에도 매 tick 전 필드를
  비교하면 개체 수에 정직하게 비싸진다. N6은 쓰기 시점 dirty가 본체다.
- **`EntityHandle` 별칭:** 같은 `{index,generation}`이 다른 Scene/피어에서 존재한다.
  테스트는 우연히 같은 값을 의도적으로 만들어야 한다.
- **type/field ID 리네임:** 이름 hash를 쓰면 코드 정리 하나가 protocol break가 된다.
  명시 ID와 tombstone을 version control한다.
- **reliable 남용:** 위치·입력을 reliable ordered로 보내면 낡은 상태가 최신 상태를
  막는다. 사건과 상태를 분리한다.
- **고정 tick 도입의 게임플레이 변화:** 현재 가변 dt에 맞춰진 물리·C# delay·AI가
  달라질 수 있다. N3는 네트워크보다 먼저 독립 회귀로 닫는다.
- **Network Thread의 Scene 접근:** `Resolve()`가 있다고 스레드 안전한 것은 아니다.
  Scene slot과 generation은 게임 스레드가 변경한다. queue 경계를 우회하지 않는다.
- **GNS가 replication까지 해준다는 오해:** GNS는 transport다. entity delta,
  quantization, interest는 엔진 소유다.
- **전용 서버를 `#ifdef`로 위장:** Server target에 Editor/Render 소스가 들어간 채
  실행만 안 하는 것은 경계 완료가 아니다. PE import와 source membership으로 판정한다.

---

## 9. 외부 후보의 공식 근거

- rapidyaml: <https://github.com/biojppm/rapidyaml>
- GameNetworkingSockets: <https://github.com/ValveSoftware/GameNetworkingSockets>
- ENet: <https://github.com/lsalzman/enet>
- FlatBuffers schema evolution(backend/control-plane 요구가 생길 때만 재평가):
  <https://flatbuffers.dev/evolution/>

라이브러리 벤치마크 수치는 후보 선정 근거일 뿐 완료 증거가 아니다. 최종 결정은 N0/N7의
CreatorEngine Release A/B와 패키징 결과로 내린다.
