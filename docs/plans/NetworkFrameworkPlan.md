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
   바꾼다.** Render frame과 Simulation tick을 분리하고, **고정 축 전용 관리 훅
   `OnSimulationTick`을 신설한다**(2026-09-04 결정 · §4.4). 기존 `PrePhysics`/
   `PostPhysics`와 `GameLogic`은 프레임 축에 그대로 둔다 — 실측상 `GameLogic`은
   `UpdateRenderData`로 끝나고 `Initialization`은 씬 활성화를 폴링하므로 tick 루프에
   들어갈 수 없다.
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
11. **이 페이즈는 통째로 이양한다**(2026-09-04 결정 · §6). 사내는 확장점 3종과 계약
    헤더를 한 번 세우고, N0·N2·N4·N5·N6·N7·N10은 담당자가 전량 소유한다. N3 고정
    틱만 네트워크가 아닌 엔진 기반 작업이라 사내가 먼저 닫는다.

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

**결정(2026-09-04, 2판): 고정 축 전용 관리 훅 `OnSimulationTick(float fixedDelta)`을
신설한다. 기존 `PrePhysics`/`PostPhysics`와 `GameLogic`은 프레임 축에 그대로 둔다.**

1판은 "관리 틱을 고정 루프 안에"라고만 적고 `Initialization`과 `GameLogic`까지 루프
안에 넣었다. **생명주기를 읽지 않고 Unity를 유추한 배치였고, 실측으로 틀렸음이
드러났다.** 아래 표가 그 정정이다.

#### 실측한 배치 (2026-09-04)

| 호출 | 실제로 하는 일 | 배치 | 근거 |
|---|---|---|---|
| `SceneManagers->Initialization` | **비동기 씬 로딩 완료 폴링 → `ActivateScene`** + `DrainPendingLifecycle`(Awake/Start 드레인) | **밖** | tick 루프 안에서 활성 씬이 바뀌면 같은 프레임의 남은 tick이 다른 씬을 돈다 |
| `SceneManagers->InputEvents` | 입력 샘플 | **밖** | 프레임 이벤트다. 30fps에서 두 tick이 같은 입력을 소비하는 것은 정상이며 §4.6의 "input은 server tick에 귀속"과 일치 |
| `IsGamePaused`/`Pausing` | — | **밖** | 일시정지는 누산 자체를 멈춘다 |
| `SceneManagers->Physics`<br>= `Scene::FixedUpdate` | AI future 회수 · `AllUpdateWorldMatrix(FixedUpdate)` · `SetInternalPhysicData` · `CharacterControllerSystems->FixedUpdate` · `PhysicsManagers->Update`(PhysX simulate) | **안** | 고정 스텝의 본체. CCT가 PhysX **앞**이어야 한다는 기존 순서 규약은 tick 안에서 그대로 성립한다 |
| **`OnSimulationTick`(신설)** | C# 고정 축 훅 | **안 · 물리 뒤** | 게임 로직이 옮겨갈 자리(아래) |
| `Scope.Tick` | `SimulationScope.Delay` 시간 진행 | **안**(이동) | "결정적 지연"을 표방하는데 프레임 dt로 흐르면 FPS에 따라 재개 tick이 달라져 표방이 거짓이 된다 |
| `SceneManagers->GameLogic`<br>= `Scene::Update` + `LateUpdate` | Animator · Decal · Foliage · UITick · Sound · Camera 시스템, 그리고 **`UpdateRenderData()`** | **밖** | 표현 축이다. 루프 안에 넣으면 **프레임당 렌더 페이로드를 N번 만든다** |
| `TickManagedPrePhysics`/`PostPhysics` | Flush 5종 + C# `PrePhysics`/`PostPhysics` | **밖 · 현 위치 유지** | `FlushAniEvents`가 `GameLogic` 안 `AnimatorSystem->Update`가 쌓은 Enter/Update/Exit에 의존한다. 루프로 옮기면 상태 머신이 돌기 전에 이벤트를 흘린다 |

```text
Render frame
  Initialization()                  ← 밖: 씬 활성화 폴링 포함
  InputEvents(frameDelta)           ← 밖
  if paused: Pausing(); return

  accumulator += frameDelta
  clamp(accumulator, catchUpMax)

  while accumulator >= fixedDelta:
      Scope.Tick(fixedDelta)              ← 결정적 지연은 고정 축
      Drain validated commands(tick)
      Physics(fixedDelta)                 ← Scene::FixedUpdate (PhysX)
      FlushPhysicsEvents                  ← 이 tick의 충돌을 이 tick에서 보게
      OnSimulationTick(fixedDelta)   ★    ← 신설 관리 훅
      Commit spawn/despawn
      OnTickCommitted(tick, fixedDelta)   ← 확장점: replication snapshot capture
      tick++
      accumulator -= fixedDelta

  GameLogic(frameDelta)             ← 밖: Animator·UI·Sound·Camera·UpdateRenderData
  TickManagedPostPhysics(frameDelta) ← 밖: 현 위치 유지
  interpolationAlpha = accumulator / fixedDelta
  Render(interpolationAlpha)
```

#### 왜 기존 훅을 옮기지 않고 신설하는가

2026-09-04 실측(PHASE 9.5 W1 개명 후 재확인): `override void PrePhysics` **0건**,
`override void PostPhysics` **17건**, `Component` 파생 **42개**. 즉 **게임플레이
로직의 본체가 C#의 물리-뒤 축에 있다.** 그것을 프레임 축에 두면 서버가 tick당 물리를 두 번 돌 때 스크립트
판단은 한 번이라 서버와 클라이언트가 갈린다.

그렇다고 `PostPhysics`를 통째로 루프 안으로 옮기면 위 표의 `FlushAniEvents` 의존이
깨진다. **신설이 그 둘을 모두 피한다** — 17개를 한꺼번에 옮길 필요 없이 결정론이
필요한 것부터 이주하고, 기존 순서 계약은 하나도 건드리지 않는다.

| C# 훅 | 축 | 상태 |
|---|---|---|
| `PrePhysics(dt)` | 프레임 · 물리 앞 | 유지 (현재 오버라이드 0건) |
| `PostPhysics(dt)` | 프레임 · 물리 뒤 | 유지 (현재 17건) |
| **`OnSimulationTick(fixedDelta)`** | **고정 tick · 물리 뒤** | **신설.** 결정론이 필요한 로직이 여기로 이주한다 |

**이름은 `FixedUpdate`로 하지 않는다.** `ClrHost.h`가 그 이름을 의도적으로 버렸다 —
*옛 FixedUpdate/Update/LateUpdate 셋을 대체한다. 셋 다 실제로는 물리 뒤였고, **이름이
그 사실을 감추고 있었다.*** 같은 이름을 되살리면 그 교훈이 무너진다.
`OnSimulationTick`은 축을 이름에 드러낸다.

#### 선행: 관리 측 오버라이드 감지 — **PHASE 9.5 W8이 소유한다**

`ScriptRegistry`는 `_active` 전체를 순회하며 `Invoke(b, x => x.PrePhysics(dt), ...)`를
부른다 — 오버라이드하지 않은 스크립트도 빈 가상 호출을 겪고, 틱 훅은 `dt`를 캡처해
할당까지 붙는다(6단계 훅은 이미 `static` 람다다). 지금도 낭비다: `PrePhysics`는
오버라이드 0건인데 `Component` 파생 42개를 매 프레임 돈다. 여기에 **tick당** 도는
훅을 더하면 30fps에서 프레임당 84회가 된다.

**이 작업은 PHASE 20이 아니라 `ScriptSurfacePlan` W8이 소유한다.** 성격상 스크립트
표면 작업이고, `ScriptRegistry.cs`를 그 페이즈가 일관되게 소유해야 동시 편집이
생기지 않는다. PHASE 20은 **N3-b의 선행으로 참조만 한다.**

#### 배선 지점 6곳

기존 `PrePhysics`/`PostPhysics`가 이미 밟은 길이므로 설계가 아니라 복제다.

| # | 파일 | 내용 |
|---|---|---|
| 1 | `ScriptCore/Component.cs` | `public virtual void OnSimulationTick(float fixedDelta) { }` |
| 2 | `ScriptCore/ScriptRegistry.cs` | 디스패치 루프(`PostPhysicsTick` 형태 복제, 마스크로 필터) |
| 3 | `ScriptCore/Bootstrap.cs` | `[UnmanagedCallersOnly]` 진입점 |
| 4 | `Engine/SceneRuntime/ClrHost.cpp` | `bind(L"SimulationTick", ...)` + 호출 래퍼 |
| 5 | `Engine/SceneRuntime/ClrHost.h` | `TickFn m_fnSimulationTick` + 선언 |
| 6 | `Engine/SceneRuntime/RuntimeFrame.cpp` | 고정 루프 안에서 호출 |

#### 아직 정하지 않은 것

- `fixedDelta` 값과 catch-up 상한은 N3 계측에서 정하고 하드코딩하지 않는다.
- **물리 앞 고정 훅**(`OnSimulationPreTick`)은 만들지 않는다. 지금 `PrePhysics`
  오버라이드가 0건이라 수요가 없다. 힘·입력을 tick당 적용하는 스크립트가 실제로
  생기면 그때 같은 패턴으로 추가한다.
- **네이티브 컴포넌트에는 고정 축 훅을 만들지 않는다.** `OnSimulationTick`은 관리
  측 전용이며, 위 배선 6곳에 `Component.h`가 없는 것이 그 사실이다. 트랙 C3가
  네이티브 가상 틱 3종을 걷어낸 뒤 전용 시스템의 조밀 배열이 "무엇을 언제 도는가"를
  전부 말하므로 그 축은 되살리지 않는다. `Lifecycle::PhaseBits`의 옛 틱 자리
  `1u<<3~5`는 비어 있고(2026-09-04 철거), 네이티브에도 고정 축 훅이 실제로 필요해지면
  그때 축을 드러내는 새 이름으로 그 자리를 받는다.
- 관리 크로싱은 tick당 1회 늘어난다. `ClrHost.h`·`RuntimeFrame.cpp`의 규약 "스크립트가
  몇 개든 **프레임당** 통과 횟수는 고정"은 "**축마다** 고정"으로 갱신한다 — 프레임 축
  훅은 프레임당, 고정 축 훅은 tick당이며, 어느 쪽도 스크립트 수에 비례하지 않는다.
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

### N3 — Fixed Simulation Clock (위임 대상 아님 · 사전 작업 · 선행 W8)

**선행:** SceneGraph L5 완료(현재 충족). PHASE 19 T0과 hot zone 조정.

**소유:** 사내. 이것은 네트워크 작업이 아니라 **게임플레이 변경**이며, PHASE 19
물리와 PHASE 14 프로파일링도 같은 clock을 기다린다. PHASE 20 위임보다 **먼저**
닫는다 — 닫히면 `SimulationTick.h`가 확정돼 계약 헤더 11종 중 10종을 동결할 수 있고,
N5의 tick 통합 대기가 사라져 담당자가 N5를 온전히 받는다(§6).

**선행 하나가 이 페이즈 밖에 있다: `ScriptSurfacePlan` W8(디스패치 오버라이드 감지).**
`OnSimulationTick`은 tick당 돌므로 감지 없이 추가하면 30fps에서 프레임당 순회가
42→84회가 된다. 그 작업은 성격상 스크립트 표면이고 `ScriptRegistry.cs`를 PHASE 9.5가
일관되게 소유해야 하므로 그쪽이 가진다(2026-09-04 이관). **W8 없이 착수하지 않는다.**

- render clock과 simulation clock을 분리하고 §4.4의 루프 안/밖 배치를 구현한다.
- accumulator, catch-up 상한, pause/scene-switch/time-scale 규칙을 구현한다.
- **`OnSimulationTick(float fixedDelta)`을 §4.4의 배선 6곳으로 신설한다.** 기존
  `PrePhysics`/`PostPhysics`는 프레임 축에 그대로 둔다 — 통째로 옮기면
  `FlushAniEvents`가 `AnimatorSystem` 결과에 거는 의존이 깨진다.
- `Scope.Tick`을 고정 축으로 옮긴다. `SimulationScope.Delay`가 표방하는 "결정적
  지연"은 고정 dt로 흘러야 성립한다.
- **`ISimulationFrameObserver::OnTickCommitted(tick, fixedDelta)` 확장점을 같이
  심는다.** N5의 snapshot capture가 여기 붙는다. 자리가 같은 파일
  (`RuntimeFrame.cpp`)이라 따로 열지 않는다.
- C# `SimulationScope.Delay`와 managed crossing이 §4.4의 tick 계약과 일치하는지
  골든을 추가한다.

**2026-09-04 실측 — 파급 재산정.** 이 문서의 이전 서술("파급이 회귀 세트 전체")은
과했다. 실제로는 다음과 같다.

| 위험 후보 | 실측 | 판정 |
|---|---|---|
| 회귀 게이트의 프레임 수 단정 | `Tools/regression`에서 프레임 **수**를 단정하는 게이트 **0건**. 유일한 프레임 상수는 Player smoke의 `frameLimit`인데 종료 조건이지 판정이 아니다 | 낮음 |
| C# `SimulationScope.Delay` | 프레임 횟수가 아니라 **초 누적**이다(`Remaining = seconds`를 엔진 dt로 깎는다). 총 경과 시간이 보존된다 | 낮음 |
| **물리 저작값** | PhysX가 지금은 프레임당 1회 가변 dt로 돌고 그 위에서 값이 튜닝돼 있다. 특히 **CCT 이동값이 "스텝당 단위"**라(PhysicsRedesignPlan 함정) 스텝 수가 바뀌면 이동 속도가 그대로 바뀐다 — 60fps에서 1스텝, 30fps에서 2스텝 | **높음. N3 작업량의 본체는 누산기가 아니라 이 재보정이다** |
| **렌더 보간 부재** | §4.4가 적은 `Render(interpolationAlpha)` 배관이 현재 0건 | **높음. 없으면 고정 틱 도입 즉시 화면이 떤다(60Hz tick / 144Hz 렌더)** |
| 관리 크로싱 횟수 | §4.4 2판 결정으로 **tick당 1회 추가**(`OnSimulationTick`). 기존 Pre/PostPhysics는 프레임 축이라 늘지 않는다 | 중간. PHASE 9.5 W8이 순회 비용을 먼저 낮춘다 |
| C# 로직 이주 | `override void PostPhysics` 17건이 결정론 대상 후보다. 한꺼번에 옮기지 않고 필요한 것부터 `OnSimulationTick`으로 이주한다 | 중간. 이주는 N3 완료 조건이 아니다 |

**판정:** 가변 render FPS에서 같은 입력 tick 수와 물리 step 수가 나오며, 한 프레임
stall 뒤 catch-up 상한과 drop/debt 계수가 예상값과 일치한다. 추가로 CCT를 포함한
이동 저작값이 30/60/144fps에서 같은 궤적을 그리고, `interpolationAlpha` 적용 뒤
시각적 떨림이 없다.

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

선행이던 PHASE 17 D3-a/D3-b/D4와 SceneGraph L5는 **모두 완료**다(§1). 남은 순서는
이양 경계(§6)가 정한다.

```text
사내 1회                                    담당자 전량
──────────────────────────────────────────────────────────
ND-a 계약 6종 + 경계 게이트 ─┬───────────► N4(loopback)  ← 첫 발주
                             └───────────► N0(telemetry)
[9.5 W8] 디스패치 감지
  └ N3-b 고정 틱 + OnSimulationTick
        + OnTickCommitted ─ 헤더 7 동결 ──► N5
                       (N0 실측으로 헤더 8·9·10 동결)
[9.5 W4] 저작 dirty 통로
  └ ND-b 확장점 2종 ─────────────────────► N2 · N6 · N7 · N10
                                            ── 이후 접점 없음 ──

N1은 게이트 배선 잔여(사내). N8/N9는 제품 요구가 있을 때만.
```

세 사내 단계는 순차로 볼 필요가 없다 — ND-a가 끝나면 담당자가 N4로 4일을 쓰는 동안
사내가 N3를 진행한다. 담당자가 대기하는 구간이 없다. N7에서 실제 외부 네트워크
라이브러리를 추가하기 전까지는 dependency manifest를 늘리지 않는다.

---

## 6. 이양 경계 — 인터페이스 계약과 확장점

작성: 2026-09-04 · 개정: 2026-09-04(2판) · 목적: PHASE 20을 **다른 개발자에게 통째로
넘기기 위한** 경계를 정한다.

**초판은 목표를 잘못 잡았다.** 슬라이스를 A(위임 가능)/B(사내)/C(계약은 사내,
알고리즘만 위임) 세 구간으로 갈랐는데, C가 있는 한 담당자는 매번 사내 작업을
기다린다. 그것은 이양이 아니라 영구 분업이다. 2판은 구간을 **둘**로 줄인다 —
**사내가 한 번 세우는 것**과 **담당자가 전부 소유하는 것**.

### 6.1 전제 — §1 기준선은 갱신 완료다

§1은 2026-09-04에 재실측으로 갱신했다. 요지: PHASE 17 D3/D4가 닫히면서 N1의 판정
조건이 이미 참이고, `Runtime::TickSimulationFrame`이 시뮬레이션 순서의 단일
소유자이며, `ComponentTypeUUID`가 tombstone 규율을 이미 갖고 있다. 이후 §1이 다시
낡으면 같은 규칙을 적용한다 — 재실측 전에는 어떤 슬라이스도 발주하지 않는다.

### 6.2 완전 이양의 조건

담당자가 **공유 파일을 한 번도 건드리지 않고** PHASE 20의 모든 슬라이스를 끝낼 수
있어야 한다. 그러려면 두 종류의 경계가 필요하며, 둘은 방향이 반대다.

| | 방향 | 이 계획에서 |
|---|---|---|
| **인터페이스** | 담당자 코드가 엔진을 **부른다** | 6.4의 계약 헤더 |
| **확장점** | 엔진이 담당자 코드를 **부른다** | 6.3의 관찰자 3종 |

초판이 놓친 것은 두 번째다. 인터페이스만 주면 담당자는 "언제 무엇이 일어났는지"를
알 수 없어 결국 `Scene.cpp`에 자기 호출을 심어야 한다 — 그것이 C 구간의 정체였다.
**C는 지속적 협업이 아니라 확장점 부재였다.**

### 6.3 확장점 3종 — 붙일 자리는 이미 다 있다

2026-09-04 실측. 셋 다 새로 설계할 것이 아니라 **기존 단일점에 관찰자를 붙이는**
일이다.

| 확장점 | 붙일 자리 | 실측 근거 | 소유 |
|---|---|---|---|
| `ISimulationFrameObserver`<br>`OnTickCommitted(tick, fixedDelta)` | `Runtime::TickSimulationFrame` | 92줄 단일 정본이고 Editor·Player가 같은 함수를 탄다(E3-7) | **N3에 얹는다** — 같은 파일이라 따로 열지 않는다 |
| `ISceneEntityObserver`<br>`OnEntityBound`/`Rebound`/`Unbound` | `Scene::AllocateSlot` · `ReleaseSlot` | 슬롯 수명 단일점. 호출부 13건이 **전부 `Scene.cpp`/`Entity.cpp` 내부**이고 외부 호출 0. DDOL 이송도 `DetachEntityHierarchy`가 "BFS+슬롯 해제 단일점"이라고 주석에 명시 | **ND-b** |
| `IComponentWriteObserver`<br>`OnComponentFieldWritten(...)` | `Component::OnPropertyChanged(name, source)` | "X8 writer boundary"라는 **이름은 서 있으나 저작 축이 비어 있다**(아래) | **ND-b · PHASE 9.5 W4 뒤** |

**세 번째는 2026-09-04 재실측으로 판정이 뒤집혔다.** 초판은 주석을 근거로 "배관이
이미 서 있다"고 적었는데, 실제로는 **`OnPropertyChanged`를 부르는 곳이
`ReflectionTypedYml.h`(직렬화·로드 경로) 하나뿐이고 `ReflectionTypedDraw.h`(인스펙터
편집 경로)에는 0건**이다. 오버라이드 구현체도 4개(`Component`·`Canvas`·
`RectTransformComponent`·`Transform`)뿐이다. 즉 쓰기 경계는 **로드/저장 축에만 있고
저작 축에는 없다.** 주석의 "리플렉션 기반 필드는 이 훅을 상속한다"는 참이지만,
상속과 호출은 다르다.

그 구멍을 닫는 것이 **`ScriptSurfacePlan` W4(저작 dirty 통로)** 다 — 리플렉션 draw가
값 변경을 알릴 훅을 만들고 렌더 프록시 컴포넌트가 거기서 dirty를 발행한다.
**따라서 ND-b는 W4에 의존한다.** W4가 통로를 세우고, ND-b는 그 훅에 네트워크
관찰자를 분기한다. 순서가 뒤집히면 통로 없는 관찰자를 만들게 된다.

### 6.4 동결 계약 헤더

계약 소유는 예외 없이 사내다. 담당자는 이 헤더를 수정하지 않고 구현만 제출한다.
변경이 필요하면 6.6의 계약 변경 절차를 탄다.

**N3가 먼저 닫히면 11종 중 10종을 동결할 수 있다.** 남는 하나는 권한 요구가
정한다.

| # | 헤더 | 소유하는 정책 | 동결 시점 | 구현 |
|---|---|---|---|---|
| 1 | `NetWireIds.h` | `NetworkObjectId`·`NetComponentTypeId`·`NetMessageTypeId`·`NetFieldId` 고정폭 표기와 tombstone 표 | **ND-a** | 사내 |
| 2 | `INetworkTransport.h` | message-oriented send/recv, lane, 연결 상태, 통계 | **ND-a** | **위임** |
| 3 | `NetProtocolHeader.h` + bounded reader/writer | §4.7 헤더 필드, 고정폭·endian 명시 | **ND-a** | **위임** |
| 4 | `INetChannelPolicy.h` | §4.6 전송 정책 표의 코드화 | **ND-a** | **위임** |
| 5 | `INetTelemetrySink.h` | enqueued/applied/dropped/pending/invalid/unauthorized 계수 | **ND-a** | **위임** |
| 6 | `INetworkEntityRegistry.h` | `Bind`/`Rebind`/`Unbind`/`Resolve` 순서 계약 | **ND-a** | **위임**(6.3의 관찰자 위에서) |
| 7 | `SimulationTick.h` | render frame과 **다른 타입**의 tick, `ISimulationClock` | **N3 완료 시** | 사내(N3) |
| 8 | `NetReplicationPolicy.h` | replication condition, quantizer, interpolation, validation bounds. **값 in/값 out 순수 함수** | N0 계측 뒤 | **위임** |
| 9 | `ReplicationSnapshot.h` | 불변 snapshot 값 구조. 포인터·`EntityHandle`·`ryml::NodeRef` 금지 | N0 계측 뒤 | **위임** |
| 10 | `INetCommandSink.h` | 검증된 inbound 값 명령과 backpressure 정책 | N0 계측 뒤 | **위임** |
| 11 | `INetAuthorityPolicy.h` | RPC·spawn allowlist와 권한 판정 | 권한 요구 확정 뒤 | **위임** |

8~10을 미리 동결하지 않는 이유는 quantizer 표현·snapshot 배치·backpressure 임계가
N0 계측 전에는 추정이기 때문이다. **추정을 계약으로 굳히지 않는다.** 다만 N0 자체를
담당자에게 맡기므로, 그 결과를 받아 사내가 동결한다.

`meta::replicated(id, quantizer, condition)` 속성은 `MetaSchema.h`의 기존
`.with(...)`에 **순수 추가**로 붙는다 — `.with`가 consteval 가변 인자라 기존 소비자의
의미가 바뀌지 않는다. 반대로 속성을 매크로로 도입하면 PHASE 18이 없앤 매크로 0종을
되돌리므로 금지한다.

### 6.5 이양 경계표

| | 사내 (1회, 그 뒤 접점 없음) | 담당자 (전량 소유) |
|---|---|---|
| 슬라이스 | **N3-b**(사전 작업 · 위임 대상 아님) · **N1**(게이트 배선 잔여) · PHASE 9.5의 **W8·W4** 대기 | **N0 · N2 · N4 · N5 · N6 · N7 · N10 전 구간** |
| 경계 | 확장점 3종(6.3) · 계약 헤더(6.4) | 그 뒤에서의 모든 구현 |
| 하네스 | — | 자기 개발 하네스와 헤드리스 호스트 |
| 게이트 | 계약 위반 자동 검출 1종 | 기능 게이트 전부 |
| 링크 | `SceneRuntime`이 렌더 없이 서는지 보장 | — |

**초판과 달라진 것 셋.**

1. **C 구간이 사라졌다.** 확장점 3종이 서면 N2·N5·N6가 전부 담당자 소유가 된다.
2. **헤드리스 테스트 호스트를 담당자에게 넘긴다.** 그것은 담당자의 개발 루프이므로
   담당자가 만드는 편이 자연스럽다. 사내는 `SceneRuntime`이 렌더 장치 없이
   링크되는지만 보장한다 — 실측상 `Engine/SceneRuntime`이 RenderEngine·DirectX를
   참조하는 파일은 헤더 5개(구현 포함 15개)뿐이고, `Tools/AssetCooker`가 Console
   Application 선례를 이미 갖고 있다.
3. **N3를 PHASE 20 위임 범위에서 뺀다.** 고정 틱은 네트워크 요구가 아니라 엔진
   기반이고 PHASE 19·14도 같은 clock을 기다린다. 사내가 먼저 닫는다.

### 6.6 사내 1회 선행 — ND-a와 ND-b

| | 내용 | 공수 | 여는 것 |
|---|---|---:|---|
| **ND-a** | 계약 헤더 6종 동결 + 계약 변경 절차 + 경계 게이트 1종 | **2일** | **N4 발주** |
| **N3-b** | 고정 틱 + `OnSimulationTick` + `OnTickCommitted` 확장점 | 4일 | 헤더 7번 동결 · **N5 발주** |
| **ND-b** | 확장점 2종(엔티티 수명 · 쓰기 dirty) | **2.5일** | **N2 · N6 발주 = 전량 이양** |

**이 페이즈 밖 선행 둘 — `ScriptSurfacePlan`(PHASE 9.5)이 소유한다.**

| 선행 | 여는 것 | 이유 |
|---|---|---|
| **W8** 디스패치 오버라이드 감지 (1일) | N3-b | tick당 도는 훅을 감지 없이 더하면 순회가 배가 된다 |
| **W4** 저작 dirty 통로 (3일) | **ND-b** | 저작 축에 훅 통로 자체가 없다(§6.3). W4가 통로를 세우고 ND-b가 관찰자를 분기한다 |

공수 근거: 이 저장소의 기존 인터페이스·정책 헤더는 19~429줄이고 중앙값이 약 160줄,
절반 이상이 주석이다(`EntityHandle.h`는 61줄 중 40줄이 주석). 확장점 2종은 자리가
이미 있어 훅 삽입 자체는 싸지만, `ProxyDirty`의 렌더 전용 의미론을 넓히는 설계가
ND-b 공수의 대부분이다. **여전히 추정이며 착수 뒤 실측으로 갱신한다.**

**경계 게이트(ND-a) — 사내가 소유하는 단 하나의 게이트.**
`verify-net-wire-boundary`가 wire 구조체에 `size_t`·포인터·`EntityHandle`·STL ABI
컨테이너·yaml/json 타입이 들어오면 붉어진다. 금지 타입 canary를 함께 두고 **변이로
이빨을 증명한 뒤** `run-all.ps1`에 배선한다. 세트에 없는 게이트는 없는 것과 같다.
기능 게이트(loopback correctness, ID lifecycle 등)는 담당자가 소유한다.

**계약 변경 절차.** wire ID는 리네임 하나가 프로토콜 파손이다(§9).

- 6.4 헤더 수정은 사내 승인 없이 하지 않는다.
- `NetFieldId`·`Net*TypeId`는 삭제 후 재사용하지 않고 tombstone에 남긴다.
- tombstone 표는 이 계획 문서가 소유하며 코드 주석이 정본이 아니다.

### 6.7 착수 순서

```text
사내 (1회)                                    담당자 (전량)
──────────────────────────────────────────────────────────────
ND-a 계약 6종 + 경계 게이트 (2일)
   └───────────────────────────────────────► N4 Loopback
                                             N0 telemetry
[PHASE 9.5 W8] 디스패치 감지
   └─ N3-b 고정 틱 + OnSimulationTick + OnTickCommitted (4일)
         └─ SimulationTick.h 동결 ──────────► N5 spawn/snapshot
                                             (N0 결과로 헤더 8·9·10 동결)
[PHASE 9.5 W4] 저작 dirty 통로
   └─ ND-b 확장점 2종 (2.5일)
         └───────────────────────────────────► N2 · N6 · N7 · N10
                                             ── 이 시점부터 접점 없음 ──
```

- ND-a는 다른 무엇도 기다리지 않는다. 끝나면 담당자가 N4로 4일을 쓰고, 그 동안
  사내는 W8→N3-b를 진행한다. 담당자가 대기하는 구간이 없다.
- **전량 이양 시점은 PHASE 9.5 W4에 묶여 있다**(2026-09-04 확인). ND-b가 W4의 훅 위에
  서기 때문이다. W4가 늦으면 N2·N6 발주도 같이 밀리므로, 이양 일정을 세울 때
  PHASE 9.5의 진행을 함께 본다.
- 해당 헤더가 동결되기 전에 그 구현을 발주하지 않는다. 그러면 헤더가 구현을
  따라가게 되고, 위임이 아니라 사후 승인이 된다.

### 6.8 접점은 0이 되지 않는다 — 남는 셋

정직하게 적는다. 다만 이들은 **일상적 협업이 아니라 인터페이스 변경 요청**이라
빈도가 낮다.

1. **확장점이 정보를 덜 줄 때.** 담당자가 "여기서 이 값도 필요하다"고 요청한다.
   확장점 설계가 부실할수록 잦아지므로, ND-b에서 담당자가 필요할 정보를 넉넉히
   싣는다. 이 항목의 빈도가 이양 설계의 품질 지표다.
2. **wire ID tombstone 승인.** 프로토콜 파손 방지라 양보하지 않는다.
3. **최종 인수.** 경계 게이트가 자동 판정하고 사내는 그 결과를 본다.

### 6.9 이 절이 판정하지 않는 것

- **인원·일정·계약 조건.** 6.5는 기술 경계이지 공수 배분이 아니다.
- **담당자의 저장소 접근 형태.** 별도 클론인지 워크트리인지, 브랜치 정책이
  무엇인지는 이 문서 밖이다.
- **협업 규약.** 이 저장소에는 `.editorconfig`·`.clang-format`·`CONTRIBUTING.md`가
  없고 최근 200커밋 중 191이 단일 저자다. 다중 저자 규약은 휴면 상태이며 이양과
  함께 되살려야 하지만 이 계획의 범위가 아니다.

### 6.10 이 절 고유의 함정

- **확장점 없이 인터페이스만 주는 것.** 초판이 그랬다. 담당자는 "언제 무엇이
  일어났는가"를 알 수 없어 결국 공유 파일에 자기 호출을 심는다. 인터페이스와
  확장점은 방향이 반대이고 **둘 다** 있어야 이양이 닫힌다.
- **N3를 "틱 루프 한 파일"로 오해.** 착지점은 92줄이지만 작업량의 본체는 물리
  저작값 재보정과 렌더 보간 신설이다(N3 슬라이스의 파급 표).
- **관측 커맨드를 통해 계약을 우회.** `net.*` 커맨드가 live Scene을 직접 읽으면
  §4.5의 스레드 경계가 커맨드 표면으로 샌다. 커맨드도 값 snapshot만 읽는다. 등록은
  `ConsoleCommandSystem.cpp`(12,109줄)를 건드리지 않도록 별도 파일로 분리해 담당자에게
  넘긴다.
- **위임 산출물의 초록을 그대로 수용.** 담당자가 자기 하네스로 낸 초록은 개발
  신호다. 인수 판정은 사내 경계 게이트가 한다.
- **확장점을 심으면서 의미를 좁히는 것.** `OnPropertyChanged`를 렌더 프록시 의미
  그대로 재사용하면 네트워크가 필요로 하는 쓰기(비렌더 컴포넌트)를 놓친다. ND-b는
  채널을 넓히는 작업이지 재사용하는 작업이 아니다.
- **주석의 "이미 있다"를 호출로 읽는 것.** §6.3의 writer boundary가 그랬다 —
  `Component.h` 주석이 "리플렉션 기반 필드는 이 훅을 상속한다"고 적어서 통로가 있다고
  읽었는데, 상속은 참이고 **인스펙터가 그 훅을 부르지 않았다**. 확장점의 자리를 잴
  때는 선언이 아니라 **호출자**를 센다.
- **`ScriptLifecyclePhase` enum에 `OnSimulationTick`을 더하려는 충동.** 이 페이즈가
  통째로 이양되므로 **담당자가 가장 먼저 부딪힐 지점**이다. 그 enum은 생명주기
  **단계**(OnInitialized~OnRemovingFromScene 6종)의 네이티브↔관리 대응표이고,
  `OnSimulationTick`은 단계가 아니라 **틱**이라 값을 더하지 않는다. 기존
  `PrePhysics`/`PostPhysics`와 같이 `ClrHost`의 전용 진입점으로 간다 — §4.4의 배선
  6곳이 그 경로다. 코드 쪽에도 같은 경고를 `ScriptLifecyclePhase.h` 상단에 두었다
  (2026-09-04).

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
