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

## 1. 현재 소스 기준선 (2026-09-04 재실측)

최초 작성은 2026-08-22다. 그 뒤 PHASE 17 D3-a/D3-b/D4가 완료되면서 1.2와 1.4의
전제가 바뀌어 2026-09-04에 전면 재실측했다. **옛 값은 표에 남긴다** — 무엇이
닫혔는지가 남은 슬라이스의 크기를 정하기 때문이다.

### 1.1 네트워크 계층은 아직 없다 (변동 없음)

- `vcpkg.json`에는 GNS, ENet, Asio 등의 게임 Transport가 없다(2026-09-04 재확인,
  이름 검색 0건).
- 연결·peer·packet·replication·RPC·interest management를 소유하는 Runtime
  모듈도 없다.
- 따라서 지금은 기존 API와 라이브러리를 감싸는 단계가 아니라 **정체성·틱·스레드
  경계를 먼저 세울 수 있는 시점**이다.

### 1.2 포맷 타입 누출은 닫혔다 — N1의 판정 조건이 이미 참이다

| 항목 | 2026-08-22 | 2026-09-04 | 비고 |
|---|---:|---:|---|
| `YAML::`/`MetaYml::`/yaml-cpp 매치 | 43파일 · 268건 | **17파일 · 31건 (전부 주석)** | 실코드 소비 0 |
| `#include`의 yaml-cpp | 있음(`Entity.h` 포함) | **0건** | D3-b-4가 은퇴 |
| `nlohmann::json` 소비 | 16파일 · 44건 | **0** | D4가 은퇴 |
| ryml `#include` | — | **4파일** (Utility_Framework 3 · EngineEntry 1) | authoring backend |
| ryml `#include` — `Engine/SceneRuntime` | — | **0** | ▶ N1 판정 조건 |
| `ComponentFactory::LoadComponent` 인자 | `MetaYml::detail::iterator_value` | **`Authoring::NodeView`** | D3-a-4/5a |

N1의 판정("`SceneRuntime`/`NetCore` 후보 헤더의 yaml-cpp/nlohmann include 0")은
**이미 충족됐다.** N1은 새로 발주할 슬라이스가 아니라 잔여 확인 게이트다.

남은 항목은 **누출이 아니라 보관**이다. 장기 보관 지점 4곳은 여전히 4곳이지만
전부 소유 타입으로 바뀌었다.

| 보관 지점 | 2026-08-22 | 2026-09-04 | 층 |
|---|---|---|---|
| `Prefab::m_prefabData` | 원시 Node | `Authoring::Document` | SceneRuntime |
| `Entity::m_prefabOriginal` | 원시 Node | `Authoring::Document` | SceneRuntime |
| `SceneManager::m_editorSceneBackup` | 원시 Node | `Authoring::Document` | SceneRuntime |
| `GameObjectCommand::m_serializedNode` | 원시 Node | **`m_serializedDocument`**(`Authoring::WriteDocument`) | Editor |

`Authoring::Document`는 `Impl`을 숨기므로 헤더가 ryml을 새게 하지 않는다(위 표의
include 0이 그 결과다). 다만 **런타임 모듈이 authoring document를 여전히 보관한다**는
사실은 남는다. 이것이 headless Server의 실제 링크 의존으로 이어지는지는 헤더 검색이
아니라 PE import로 판정하며, 그 판정은 N9의 몫이다.

### 1.3 정체성이 네 종류인데 계약이 분리되지 않았다

| 정체성 | 현재 의미 | wire 판정 |
|---|---|---|
| `EntityHandle {sceneId,index,generation}` | 한 프로세스의 Scene 슬롯 | **금지** |
| `FileGuid` | 저장 자산의 영속 정체성 | spawn descriptor에서 사용 가능 |
| `Meta::Type::typeID`/`HashedGuid` | 타입명 기반 FNV 조회 키 | **wire ID로 금지** |
| `ComponentTypeUUID::kTable` (2026-09-04 추가) | 컴포넌트 타입의 **리네임 불변 영속 UUID** 32종 | 정본 후보 · 폭은 재단 필요 |

`sceneId`는 프로세스 안에서 생성되는 일련번호이고, 서로 다른 피어의 슬롯 배치는
일치하지 않는다. 타입 ID도 타입명·컴파일러 표기에 결합하며 `size_t`라 wire 고정
폭이 아니다. 둘을 그대로 보내면 같은 빌드에서도 원격 정체성을 보장하지 못하고,
리네임은 프로토콜 변경이 된다.

네 번째 행은 §1이 처음 쓰일 때 빠져 있던 재료다. `ComponentTypeUUID.h`(SceneGraph
K1-b)는 컴포넌트 타입 32종의 손으로 박은 UUID 표를 갖고 있고, 파일 상단이
"값은 처음 박을 때만 정하고 그 뒤로는 절대 바꾸지 않는다"는 규율을 이미 명시한다 —
N2가 요구하는 **tombstone 규율과 같은 성질**이다. 다만 128비트는 매 snapshot에
싣기에 무겁다. N2는 이 표를 새로 만들지 말고, 이것을 정본으로 두되 wire에는
handshake에서 확정한 고정폭 short id를 쓰는 안을 먼저 검토한다.

### 1.4 고정 Simulation Tick은 여전히 없다 — 다만 착지점이 한 곳으로 좁혀졌다

옛 서술("`PlayerMain::Update`가 가변 delta를 `Physics`와 `GameLogic`에 한 번씩
전달한다")은 더 이상 정확하지 않다. E3-7이 시뮬레이션 순서를 단일 소유자로 옮겼다.

- `Runtime::TickSimulationFrame(delta)`(`Engine/SceneRuntime/RuntimeFrame.cpp`, 92줄)가
  `Initialization → InputEvents → pre-physics 관리 틱 → Physics → GameLogic →
  post-physics 관리 틱` 순서를 소유한다.
- `PlayerMain::Update`와 `EditorMain`의 재생 분기가 **같은 함수**를 부른다.
  `Runtime::ResolveFrameDelta()`가 "delta 0은 일시정지에서만"을 지키는 유일한 자리다.
- 그러나 누산기는 없다. 저장소 전체에서 fixed timestep accumulator 검색 결과
  시뮬레이션 축에는 0건이다(`Scene.cpp`의 `TransformUpdateAccumulator`는 이름만 같고
  트랜스폼 순회 계수기다).

즉 **N3가 고쳐야 할 대상은 여전히 100% 남아 있고, 고칠 자리만 두 앱에서 한 파일로
줄었다.** 이것을 "작아졌다"로 읽지 않는다 — 파급은 물리 스텝 수·C#
`SimulationScope.Delay`·AI·회귀 세트 전체다(§9).

### 1.5 사용할 수 있는 좋은 선례가 있다 (변동 없음, 경로 확인)

- `ProxyCommandQueueController`(`Engine/RenderEngine/ProxyCommandQueue.h`)는 생산
  스레드의 staging을 값 `Batch`로 밀봉하고 소비 스레드가 frame/epoch 경계에서 적용한다.
- `EnhancedLiveDisplaySnapshot`(`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h`)은
  포인터·RHI 객체 없이 불변 값만 다른 스레드에 공개한다.
- `MetaSchema.h`(`Engine/Utility_Framework/`, 633줄)는 std-only canonical schema이며
  `field.with(attr...)`가 consteval 가변 인자라 속성 추가가 **순수 추가**다.
- (2026-09-04 추가) `RHIAssetEvictionPolicy.h`는 값 구조체와 순수 판정 함수만으로
  정책을 소유하고 device를 모른다. §6이 위임 가능한 슬라이스의 형태로 지목하는 선례다.

PHASE 20은 새 전역 자료구조를 먼저 만들기보다 이 계약들을 network 입출력에
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

### N1 — Archive·Parser 경계 밀봉 (2026-09-04 실측: 실질 충족)

**선행:** PHASE 17 D3-a/D3-b/D4 — **완료.**

**상태:** 아래 네 항목은 PHASE 17이 닫으면서 이미 참이다(§1.2 재실측 표).
남은 것은 판정을 게이트로 고정하는 일뿐이며, 새로 발주할 구현 슬라이스가 아니다.
`MetaSchema`는 format-neutral을 유지했고, `Entity.h`·`ComponentFactory`의 YAML/JSON
타입은 사라졌으며(include 0), `Authoring::Document`가 ryml `Tree`를 `Impl`로 숨긴다.

- `MetaSchema`를 format-neutral canonical schema로 유지한다.
- `Entity.h`, `ComponentFactory`, Runtime interface에서 YAML/JSON 타입을 제거한다.
- `AuthoringDocument`는 ryml `Tree`를 소유하고 Node view의 수명을 문서 아래로
  제한한다.
- `AuthoringArchive`와 `CookedArchive`가 같은 field visitor를 소비함을 canary로
  증명한다.

**판정:** `SceneRuntime/NetCore` 후보 헤더의 yaml-cpp/nlohmann include 0(2026-09-04
**충족** — SceneRuntime의 ryml include도 0), authoring/cooked 왕복 골든 통과.
잔여 작업은 이 판정을 회귀 세트에 배선해 되돌아가지 않게 만드는 것이다 — 세트에
없는 게이트는 없는 것과 같다.

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

## 6. 인터페이스 계약과 소유권 분할

작성: 2026-09-04 · 목적: 이 계획의 일부를 **저장소 주 개발자가 아닌 다른 개발자가
담당**할 수 있도록, 위임 가능한 경계와 위임할 수 없는 경계를 실측으로 가른다.

이 절은 새 슬라이스를 추가하지 않는다. N0~N10의 소유권만 나눈다.

### 6.1 전제 — §1 기준선은 이미 낡았다

§1은 2026-08-22 실측이고, 그 뒤 PHASE 17 D3-a/D3-b/D4가 완료되면서 이 계획의
선행 조건 상당수가 이미 충족됐다. 위임 범위를 §1로 산정하면 **없는 일을 발주하게
된다.** 2026-09-04 재실측:

| 항목 | §1 (2026-08-22) | 재실측 (2026-09-04) | 영향 |
|---|---|---|---|
| `YAML::`/`MetaYml::`/yaml-cpp 소비 | 43파일 · 268건 | **17파일 · 31건** | N1 대폭 축소 |
| `nlohmann::json` 소비 | 16파일 · 44건 | **0** | PHASE 17 D4가 은퇴 |
| `ComponentFactory::LoadComponent` 인자 | `MetaYml::detail::iterator_value` | **`Authoring::NodeView`** | N1 목표 달성 |
| `Entity.h`의 yaml-cpp include | 있음 | **0건**(주석 언급만) | N1 목표 달성 |
| 시뮬레이션 틱 드라이버 | Player/Editor가 각자 소유 | **`Runtime::TickSimulationFrame` 단일 정본**(`Engine/SceneRuntime/RuntimeFrame.cpp`, 92줄, E3-7) | N3 착지점이 1파일 |
| `EntityHandle` | 슬롯 정체성만 언급 | `{sceneId,index,generation}` + `Scene::Resolve` 존재 | N2 선행 충족 |
| `MetaSchema`의 `.with(...)` | 확장점 존재 | consteval 가변 인자(633줄) | N2 속성은 **순수 추가** |
| EngineLayerSeparation E6 | 미완 | **완료**(2026-08-24) | N9 선행 하나 충족 |
| PHASE 20 진행 | 신설 | 대시보드 N0~N10 전부 `todo` | 미착수 |

**판정:** N1은 실질적으로 완료 상태에 가깝고, 남은 것은 잔여 확인 게이트다.
§1은 2026-09-04에 이 재실측으로 갱신 완료했다. 이후 §1이 다시 낡으면 같은 규칙을
적용한다 — 재실측 전에는 어떤 슬라이스도 발주하지 않는다.

### 6.2 분할 기준

슬라이스 하나를 위임할 수 있는지는 세 질문으로 판정한다. 셋 다 참일 때만 위임한다.

1. **값 경계에서 닫히는가.** 입력과 출력이 값 타입이고 live Scene·RHI·Editor 상태를
   읽지 않는가. 이 저장소에는 이미 그 형태의 선례가 있다 —
   `RHIAssetEvictionPolicy.h`는 값 구조체와 순수 판정 함수만으로 정책을 소유하고
   device를 모른다. 위임 가능한 슬라이스는 전부 이 형태여야 한다.
2. **검증을 담당자가 스스로 돌릴 수 있는가.** 판정이 Editor 전체 빌드에 묶여 있으면
   위임이 아니라 병목 이전이다. 이 질문은 두 시점으로 갈린다 — 담당자의 **개발
   루프**는 자기 하네스로 굴려도 되지만, 사내의 **인수 판정**은 ND-b가 세운
   헤드리스 게이트로만 한다(6.5).
3. **공유 대형 파일을 건드리지 않는가.** 6.6의 접촉 금지 목록에 편집이 필요하면
   위임 대상이 아니다.

이 기준은 "네트워크 지식이 필요한가"가 아니다. 난이도가 아니라 **경계**로 가른다.

### 6.3 슬라이스별 소유 판정

| 슬라이스 | 구간 | 소유 | 사유 |
|---|---|---|---|
| ND-a 계약 헤더 6종 동결·변경 절차 | **B** | 사내 | 발주의 전제. 계약을 위임하면 헤더가 구현을 따라간다 |
| ND-b 헤드리스 호스트·인수 게이트 | **B** | 사내 | 인수의 전제. 발주와 병행하되 완료 기록 전에는 선다 |
| N0 기준선·telemetry 값 타입 | **A** | 위임 가능 | 계측 구조체와 금지 타입 게이트는 값·정적 검사 |
| N1 Archive·Parser 경계 밀봉 | **B** | 사내 | 6.1대로 실질 완료. 잔여는 게이트 확인이라 발주 단위가 아님 |
| N2 wire ID·registry·schema 검증 | **A/C** | 분할 | ID 표기·tombstone·schema 검증기는 위임(A). `Bind/Rebind/Unbind` 호출 지점은 `Scene.cpp` 접촉이라 사내(C) |
| N3 Fixed Simulation Clock | **B** | **사내 필수** | 네트워크 작업이 아니라 게임플레이 변경이다. §9가 이미 "N3는 네트워크보다 먼저 독립 회귀로 닫는다"고 적었다. PhysX 스텝·CLR 크로싱·C# `SimulationScope.Delay` 계약이 전부 걸린다 |
| N4 Loopback·프레이밍·loss simulator | **A** | 위임 가능 | 소켓 없는 메모리 어댑터. 값 in/값 out의 교과서적 형태 |
| N5 spawn/despawn·full snapshot | **A/C** | 분할 | snapshot 값 구조와 인코더는 위임(A). tick 끝의 capture 호출 지점은 사내(C) |
| N6 delta·quantization·interest | **A/C** | 분할 | 알고리즘은 위임(A). dirty를 만드는 **쓰기 창구**는 공유 파일이라 사내(C) |
| N7 Transport 결정·adapter | **A** | 위임 가능 | `INetworkTransport` 뒤의 구현 교체. 단 vcpkg 매니페스트 편집은 사내 |
| N8 prediction (선택) | **A** | 위임 가능 | 요구가 확정된 뒤에만 |
| N9 Headless Server target | **B** | 사내 | `.sln`·`.vcxproj`·링크 경계 수술 |
| N10 hardening·fuzz·soak | **A** | 위임 가능 | corpus와 자동화는 독립 산출물 |

구간 정의 — **A**: 신규 파일만으로 닫히는 위임 가능 슬라이스. **B**: 사내가 먼저
닫아야 하는 슬라이스. **C**: 계약은 사내가 쓰고 알고리즘만 위임하는 공동 슬라이스.

### 6.4 동결 계약 헤더

아래 헤더는 **사내가 작성해 동결**한다(계약 소유는 예외 없이 사내다). 위임 담당자는
이 헤더를 수정하지 않고 구현만 제출한다. 헤더 변경이 필요하면 6.5의 계약 변경
절차를 탄다.

**11종을 한 번에 동결하지 않는다.** 헤더의 비용은 타이핑이 아니라 담기는 결정인데,
quantizer 표현·snapshot 값 배치·backpressure 임계는 N0 계측 전에 정하면 추정을
계약으로 굳히는 것이 된다. "동결 시점" 열이 그 판정이며, **ND-a는 6종만 세운다.**

| # | 헤더 | 소유하는 정책 | 동결 시점 | 구현 |
|---|---|---|---|---|
| 1 | `NetWireIds.h` | `NetworkObjectId`·`NetComponentTypeId`·`NetMessageTypeId`·`NetFieldId` 고정폭 표기와 tombstone 표 | **ND-a** | 사내 |
| 2 | `INetworkTransport.h` | message-oriented send/recv, lane, 연결 상태, 통계 | **ND-a** | **위임**(Loopback·GNS·ENet) |
| 3 | `NetProtocolHeader.h` + bounded reader/writer | §4.7 헤더 필드, 고정폭·endian 명시 | **ND-a** | **위임** |
| 4 | `INetChannelPolicy.h` | §4.6 전송 정책 표의 코드화 — 종류 → reliability/lane 매핑 | **ND-a** | **위임** |
| 5 | `INetTelemetrySink.h` | enqueued/applied/dropped/pending/invalid/unauthorized 계수 | **ND-a** | **위임** |
| 6 | `INetworkEntityRegistry.h` | `Bind`/`Rebind`/`Unbind`/`Resolve` 순서 계약 | **ND-a** | 사내 |
| 7 | `NetReplicationPolicy.h` | replication condition(everyone/owner/relevant/initial-only), quantizer, interpolation, validation bounds. **값 in/값 out 순수 함수** | N0 계측 뒤 | **위임** |
| 8 | `ReplicationSnapshot.h` | 불변 snapshot 값 구조. 포인터·`EntityHandle`·`ryml::NodeRef` 금지 | N0 계측 뒤 | 인코더 **위임** |
| 9 | `INetCommandSink.h` | 검증된 inbound 값 명령과 backpressure 정책 | N0 계측 뒤 | **위임** |
| 10 | `SimulationTick.h` | render frame과 **다른 타입**의 tick, `ISimulationClock` | N3 뒤 | 사내(N3) |
| 11 | `INetAuthorityPolicy.h` | RPC·spawn allowlist와 권한 판정 | 권한 요구 확정 뒤 | **위임** |

앞 6종이 서면 **N4(Loopback)를 즉시 발주할 수 있다** — 2·3·4·5가 N4의 입출력을
전부 덮고, N4는 소켓도 Scene도 만지지 않는다.

`meta::replicated(id, quantizer, condition)` 속성은 `MetaSchema.h`의 기존
`.with(...)`에 **순수 추가**로 붙는다 — 기존 소비자의 재컴파일 외에 의미 변경이
없다. 이 성질이 N2 위임 가능성의 핵심 근거다. 반대로 속성을 매크로로 도입하면
PHASE 18이 없앤 매크로 0종을 되돌리므로 금지한다.

### 6.5 위임 준비 — ND-a와 ND-b

이 절의 초판은 인프라 3종을 모두 "위임 전 필수"로 못박았다. 그것은 과했다.
**인수 게이트가 필요한 시점은 발주 시점이 아니라 인수 시점이다.** 담당자가 코드를
쓰는 동안 사내가 인수 창구를 만들면 되고, 그러면 위임 착수가 나흘 앞당겨진다.
그래서 준비를 둘로 나눈다.

| | 내용 | 공수 | 성격 |
|---|---|---:|---|
| **ND-a** | 지금 동결 가능한 계약 헤더 6종 + 계약 변경 절차 | **1일** | 발주의 전제 |
| **ND-b** | 헤드리스 `NetTests.exe` + `verify-net-*` 게이트 3종 | **3일** | 인수의 전제 · 발주와 **병행** |

공수 근거: 이 저장소의 기존 인터페이스·정책 헤더는 19~429줄이고 중앙값이 약
160줄이며 절반 이상이 주석이다(`EntityHandle.h`는 61줄 중 40줄이 주석). 6종이면
600~1,000줄 규모라 작성 자체는 하루다. 게이트는 1종당 반나절이 이 저장소의
관행이고, 헤드리스 호스트는 `Tools/AssetCooker` 선례 복제에 링크 경계 정리가
붙는다. **이 숫자들은 여전히 추정이며 ND-a 착수 뒤 실측으로 갱신한다.**

#### ND-a — 계약 헤더 동결과 변경 절차 (1일)

6.4의 11종을 한 번에 동결하지 않는다. 헤더의 진짜 비용은 타이핑이 아니라 **결정**인데,
그중 일부는 지금 근거가 없다. 6.4 표의 "동결 시점" 열이 그 판정이다.

- **지금 동결(6종)** — `NetWireIds.h`, `INetworkTransport.h`, `NetProtocolHeader.h`,
  `INetTelemetrySink.h`, `INetChannelPolicy.h`, `INetworkEntityRegistry.h`.
  이 여섯이 서면 **N4(Loopback)를 즉시 발주할 수 있다.** N4는 소켓 없는 순수
  로직이라 담당자가 자기 개발 루프를 스스로 굴릴 수 있고, transport 인터페이스와
  헤더 프레이밍만 있으면 시작된다.
- **나중 동결(5종)** — quantizer 표현·snapshot 값 배치·backpressure 임계는 N0 계측
  전에 정하면 추정을 계약으로 굳히는 것이다. tick 타입은 N3가, allowlist는 권한
  요구가 정한다.

계약 변경 절차도 여기서 문서로 고정한다. wire ID는 리네임 하나가 프로토콜
파손이다(§9).

- 6.4 헤더 수정은 사내 승인 없이 하지 않는다.
- `NetFieldId`·`Net*TypeId`는 삭제 후 재사용하지 않고 tombstone에 남긴다.
- tombstone 표는 이 계획 문서가 소유하며 코드 주석이 정본이 아니다.

#### ND-b — 헤드리스 인수 창구와 게이트 (3일, N4 발주와 병행)

**헤드리스 테스트 호스트는 현재 0이다.**

- vcpkg 매니페스트에 단위 테스트 프레임워크가 없다(gtest·catch2·doctest 0).
- `Editor/RenderTests`는 Editor에 링크되는 `StaticLibrary`라 독립 실행이 아니다.
- `Tools/regression/run-all.ps1`의 verify 호출 84건 중 **50건이 Editor/Player exe를
  인자로 받는다.** 나머지 34건은 정적 소스 검사이거나 AssetCooker 계열이다.

즉 지금 구조에서는 Loopback 테스트 하나를 **사내가** 돌리는 데에도 vcpkg + 그래픽
SDK + CoreCLR + Editor 전체 빌드가 필요하다. 담당자에게 이 진입 비용을 그대로
지우면 첫 주가 빌드 싸움으로 사라진다 — 그래서 병행하되 **인수 전에는 반드시
선다.**

**선례는 있다.** `Tools/AssetCooker`가 `ConfigurationType=Application` ·
`SubSystem=Console`이고 RenderEngine과 Utility_Framework만 참조한다. 또한
`Engine/SceneRuntime`이 RenderEngine·DirectX를 참조하는 파일은 헤더 5개(구현 포함
15개)뿐이라 분리가 현실적이다.

→ `NetTests.exe`(Console Application, SceneRuntime 링크, 렌더 장치 없음).

**인수 게이트 최소 3종.** 이 저장소의 반복된 실패 양식은 "게이트 없이 받은 완료"가
눈먼 초록이었다는 것이다.

- `verify-net-wire-boundary` — wire 구조체에 `size_t`·포인터·`EntityHandle`·
  STL ABI 컨테이너·yaml/json 타입이 들어오면 붉어진다. 금지 타입 canary를 함께 둔다.
- `verify-net-loopback-correctness` — loss/reorder/duplicate 조건에서 reliable
  control과 unreliable latest-wins의 차이를 판정한다.
- `verify-net-id-lifecycle` — 서로 다른 두 Scene의 같은 local index, stale/despawn
  ID fail-close, ID 중복 canary.

각 게이트는 **변이로 이빨을 증명한 뒤** 도는 세트(`run-all.ps1`)에 배선한다. 세트에
없는 게이트는 없는 것과 같다.

**판정:** ND-b가 서지 않은 상태에서 위임 산출물을 "완료"로 기록하지 않는다.
담당자가 자기 하네스로 낸 초록은 개발 신호이지 인수 판정이 아니다.

### 6.6 접촉 금지 파일과 우회 경로

아래는 규모가 크고 다른 트랙이 동시에 편집하는 파일이다. 위임 담당자는 편집하지
않는다.

| 파일 | 규모 | 필요해지는 슬라이스 | 우회 |
|---|---|---:|---|
| `Engine/SceneRuntime/Scene.cpp` | 5,054줄 | N2 Bind/Rebind, N5 | 사내가 호출 지점만 심고, 담당자는 6.4의 인터페이스 뒤에서 구현 |
| `Engine/SceneRuntime/SceneManager.cpp` | 2,039줄 | N3, N5 | 사내 |
| `Engine/SceneRuntime/RuntimeFrame.cpp` | 92줄 | N3 | 사내(작지만 게임플레이 계약의 단일 소유자) |
| `Editor/EngineEntry/ConsoleCommandSystem.cpp` | 12,109줄 | 관측 커맨드 | `net.*` 커맨드를 **별도 파일**로 분리해 담당자에게 넘긴다 |
| `CreatorEngine.sln` · `*.vcxproj` | 프로젝트 15 | N7 의존 추가, N9 | 사내 |
| `vcpkg.json` | — | N7 | 사내 |

관측 커맨드 분리는 위임과 무관하게 이득이다 — 지금은 관측 표면 전체가 한 파일에
있어 어떤 트랙이든 같은 지점에서 충돌한다.

### 6.7 위임 착수 순서

```text
사내                                                위임
──────────────────────────────────────────────────────────────────────
§1 기준선 갱신(6.1) ─ 완료(2026-09-04)
  │
  └─ ND-a 계약 헤더 6종 동결 + 변경 절차 (1일)
        ├──────────────────────────────────────────► N4 Loopback  ◄ 발주 지점
        │                                              (담당자 자기 하네스로 개발)
        ├─ ND-b NetTests.exe + verify-net-* (3일) ─── 병행 · 인수 전 완료
        │
        ├─ N0 telemetry (사내 또는 위임)
        │     └─ 헤더 3종 추가 동결(7·8·9) ─────────► N5 인코더 / N6
        │
        └─ N3 Fixed Clock (독립 회귀로 먼저 닫음)
              └─ N2 Bind 호출 지점 ─────────────────► N2 검증기 / N7 / N10
```

- **발주 지점은 ND-a 뒤다.** 초판은 ND 전체(4일)를 발주 전 필수로 뒀는데 그것은
  과했다 — 인수 게이트가 필요한 시점은 발주가 아니라 인수다. 이 분할로 위임 착수가
  나흘에서 하루로 당겨진다.
- **ND-b는 인수 전에 반드시 선다.** 담당자가 자기 하네스로 낸 초록은 개발 신호이지
  인수 판정이 아니다.
- N3와 위임 슬라이스는 **병행 가능하다.** 담당자는 Loopback과 프레이밍을
  simulation clock 없이 시작할 수 있다. snapshot의 tick 통합만 N3 뒤다(§5.1과 동일).
- 해당 헤더가 동결되기 전에 그 구현을 시작하면 헤더가 구현을 따라가게 되고, 그러면
  위임이 아니라 사후 승인이 된다. 나중 동결 5종에 걸린 슬라이스를 미리 발주하지 않는
  이유가 이것이다.

### 6.8 이 절이 판정하지 않는 것

- **인원·일정·계약 조건.** 6.3의 구간 판정은 기술 경계이지 공수 배분이 아니다.
  대시보드 공수는 여전히 N0 실측 뒤 갱신 대상이다.
- **담당자의 저장소 접근 형태.** 별도 클론인지 워크트리인지, 브랜치 정책이 무엇인지는
  이 문서 밖이다. 다만 6.6의 파일 목록은 어느 형태든 그대로 적용된다.
- **협업 규약.** 이 저장소에는 `.editorconfig`·`.clang-format`·`CONTRIBUTING.md`가
  없고, 최근 200커밋 중 191이 단일 저자다. 다중 저자 규약은 휴면 상태이며 위임과
  함께 되살려야 하지만, 그 내용은 이 계획의 범위가 아니다.

### 6.9 이 절 고유의 함정

- **"인터페이스가 있으니 위임 가능하다"는 오독.** 인터페이스 존재는 필요조건이고,
  판정은 6.2의 세 질문 전부다. 6.5의 인프라 없이 헤더만 넘기면 담당자는 자기 코드가
  맞는지 확인할 방법이 없다.
- **N3를 "틱 루프 한 파일"로 오해.** 착지점은 92줄이지만 파급은 물리 스텝 수·C#
  delay·AI·회귀 세트 전체다. 파일 크기로 위임 가능성을 판정하지 않는다.
- **관측 커맨드를 통해 계약을 우회.** `net.*` 커맨드가 live Scene을 직접 읽으면
  §4.5의 스레드 경계가 커맨드 표면으로 새어 나간다. 커맨드도 값 snapshot만 읽는다.
- **위임 산출물의 초록을 그대로 수용.** 새 게이트가 첫 실행부터 전부 통과하면 변이로
  이빨을 먼저 증명한다. 이것은 담당자에 대한 불신이 아니라 이 저장소가 자기 게이트에
  적용해 온 규칙과 같다.

---

## 7. 검증 게이트

### 7.1 Source boundary

- `NetCore/NetReplication/ServerRuntime`에서 yaml-cpp, nlohmann, ryml include 0
- wire 구조체에서 `size_t`, pointer, `EntityHandle`, STL ABI container 직렬화 0
- Transport 구체 타입이 adapter 밖에 노출되는 include 0
- replicated field ID/component ID/message ID 중복 0
- 상한 없는 수신 문자열/배열/container 0

### 7.2 Simulation

- 동일 input stream을 서로 다른 render FPS로 재생해 simulation tick/physics step
  수 일치
- PrePhysics/Physics/PostPhysics/commit/snapshot 순서 골든
- pause, scene switch, DDOL, long-frame catch-up 골든
- Server와 Player host가 같은 tick driver를 소비

### 7.3 Replication

- spawn-before-state, despawn-after-last-state 순서
- stale NetId fail-close와 ID 미재사용
- full snapshot/delta 동일성
- packet loss/reorder/duplicate에서 baseline 복구
- owner/relevant 조건과 interest enter/leave
- invalid/unauthorized input 적용 0

### 7.4 성능·운영

- bytes/peer/tick, encode/decode µs, alloc/tick, queue depth/drop, RTT/jitter/loss
- 목표 peer 수에서 simulation budget 초과 0
- reliable queue 무한 증가 0
- latest snapshot superseded가 정상 계수됨
- shutdown 뒤 NetId/queue/packet buffer/connection resource 원복

---

## 8. 완료 기준

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

## 9. 리스크와 함정

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

## 10. 외부 후보의 공식 근거

- rapidyaml: <https://github.com/biojppm/rapidyaml>
- GameNetworkingSockets: <https://github.com/ValveSoftware/GameNetworkingSockets>
- ENet: <https://github.com/lsalzman/enet>
- FlatBuffers schema evolution(backend/control-plane 요구가 생길 때만 재평가):
  <https://flatbuffers.dev/evolution/>

라이브러리 벤치마크 수치는 후보 선정 근거일 뿐 완료 증거가 아니다. 최종 결정은 N0/N7의
CreatorEngine Release A/B와 패키징 결과로 내린다.
