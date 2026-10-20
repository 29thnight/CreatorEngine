# ScriptBinder 분할 판단 자료 (E7 잔여 결정)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 현재 managed 표면은 Component·Entity와 세대 핸들이다. OnInitialized~OnUninitializing 수명 훅과 PrePhysics/PostPhysics를 구분한다.
- native 객체 수명, CLR GC, Scope 취소·reload와 자산 owner는 별도 계약이다. 미래 PrSM 효과/저작 체계를 기존 ContactStream 구현으로 판정하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/SceneRuntime/RegisterReflectManual.h:185](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/RegisterReflectManual.h#L185) — `failures += "\n  " + std::string(type.name()) + (type.is_serializable() ? "" : " [쓰기 불가]")`
- [Engine/SceneRuntime/RegisterReflectManual.h:191](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/RegisterReflectManual.h#L191) — `failures += " " + std::string(field.name()) + "(" + std::string(field.type_name()) + ")";`
- [Engine/SceneRuntime/RegisterReflectManual.h:204](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/RegisterReflectManual.h#L204) — `Debug::PrintLog(spdlog::level::critical, "reflgen 서술자가 쓰거나 읽지 못하는 타입(필드의 serializer 가 등록 함수에"`
- [Engine/SceneRuntime/Scene.cpp:84](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.cpp#L84) — `m_begin = Clock::now();`
- [Engine/SceneRuntime/Scene.cpp:94](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.cpp#L94) — `const TimePoint end = Clock::now();`
- [Engine/SceneRuntime/Scene.cpp:102](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.cpp#L102) — `m_begin = Clock::now();`
- [Engine/SceneRuntime/Entity.cpp:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Entity.cpp#L18) — `Entity::Entity() : Object("Entity"), m_index(0)`
- [Engine/SceneRuntime/Entity.cpp:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Entity.cpp#L23) — `Entity::Entity(Scene* scene, std::string_view name, GameObjectType,`
- [Engine/SceneRuntime/Entity.cpp:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Entity.cpp#L29) — `Entity::Entity(Scene* scene, size_t instanceID, std::string_view name, GameObjectType,`
- [Engine/SceneRuntime/SceneManager.cpp:64](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L64) — `throw std::runtime_error("Scene asset bundle preparation failed (status "`
- [Engine/SceneRuntime/SceneManager.cpp:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L65) — `+ std::to_string(static_cast<unsigned>(result.status)) + ", completed "`
- [Engine/SceneRuntime/SceneManager.cpp:66](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L66) — `+ std::to_string(result.completed) + "/" + std::to_string(result.submitted)`
- [Engine/SceneRuntime/RuntimeFrame.cpp:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/RuntimeFrame.cpp#L16) — `void TickManagedPrePhysics(float deltaSeconds)`
- [Engine/SceneRuntime/RuntimeFrame.cpp:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/RuntimeFrame.cpp#L18) — `auto& clr = ClrHost::Get();`
- [Engine/SceneRuntime/RuntimeFrame.cpp:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/RuntimeFrame.cpp#L29) — `auto& clr = ClrHost::Get();`
- [Engine/SceneRuntime/ComponentFactory.cpp:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ComponentFactory.cpp#L33) — `gc::root_ref<Component> TryCreateManagedComponent(gc::domain& domain,`
- [Engine/SceneRuntime/ComponentFactory.cpp:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ComponentFactory.cpp#L38) — `if (requested.id() == Meta::ToTypeID(TypeTrait::GUIDCreator::GetTypeID<T>()))`
- [Engine/SceneRuntime/ComponentFactory.cpp:47](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ComponentFactory.cpp#L47) — `gc::root_ref<Component> ComponentFactory::CreateManaged(gc::domain& domain,`
- [Engine/SceneRuntime/LifecycleRegistry.cpp:43](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LifecycleRegistry.cpp#L43) — `void Registry::Store(size_t typeID, uint16_t mask)`
- [Engine/SceneRuntime/LifecycleRegistry.cpp:48](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LifecycleRegistry.cpp#L48) — `uint16_t Registry::Find(size_t typeID)`
- [Engine/SceneRuntime/LifecycleRegistry.cpp:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LifecycleRegistry.cpp#L54) — `size_t Registry::Count()`
- [ScriptCore/Prefab.cs:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L10) — `public readonly struct Prefab`
- [ScriptCore/Prefab.cs:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L12) — `private readonly string? _name;`
- [ScriptCore/Prefab.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L14) — `private Prefab(string name) => _name = name;`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

- 작성일: 2026-08-24
- 목적: EngineLayerSeparationPlan E7의 보류 항목 — "`ScriptBinder`를
  `SceneRuntime/ScriptRuntime`으로 재명명 **또는 분할**" — 의 결정 자료.
- 방법: 194파일 전수를 9개 책임 영역으로 분류하고, 영역 간 include 결합
  행렬과 외부 소비자 분포를 실측했다. 수치는 전부 이 날짜의 실측이다.

## 1. 실체 인벤토리 — 이름과 실체의 불일치

총 194파일 · 1,342KB. "스크립트 바인더"라는 이름과 달리 실체는
**게임 오브젝트 모델 + 전 도메인 컴포넌트 시스템 + C# 실행 계층**의
게임 런타임 전체다.

| 영역 | 파일 | KB | 내용 |
|---|---:|---:|---|
| SceneCore | 44 | 503 | Scene·SceneManager·Entity·Transform·Prefab·수명·팩토리 |
| RenderBridge | 32 | 168 | 렌더 프록시 브리지·Mesh/Light/Decal/Foliage/Terrain/Camera 컴포넌트 |
| ClrScript | 11 | 166 | ClrHost(135KB)·ScriptComponent·ScriptObjectRegistry |
| UI | 21 | 129 | uGUI 계열(Canvas·RectTransform·UITickSystem·프록시) |
| Physics | 21 | 108 | PhysicsManager·콜라이더·CCT·래그돌 |
| Animation | 23 | 105 | AnimationJob·Animator·상태기계 |
| Input | 16 | 67 | InputManager·InputAction·PlayerInput |
| AI_BT | 18 | 47 | AIManager·BT·블랙보드·FSM |
| Sound | 7 | 37 | SoundManager 계열 |

## 2. 결합 행렬 — 어디에 칼이 들어가고 어디에 안 들어가나

영역 간 include(내부 간선, 5건 이상):

```
RenderBridge → SceneCore   49      SceneCore → RenderBridge  42
UI           → SceneCore   36      SceneCore → Physics       36
Physics      → SceneCore   28      SceneCore → UI            26
SceneCore    → Animation   14      SceneCore → ClrScript     12
ClrScript    → SceneCore   10      (이하 한 자리 수)
```

### 2.1 도메인 분할선 — 사실상 막혀 있다

SceneCore↔RenderBridge(49+42)·↔Physics(36+28)·↔UI(36+26)가 전부
**양방향 강결합**이다. Scene이 컴포넌트를 순회·파괴·직렬화하고 컴포넌트가
Scene·Entity를 아는 구조라, 도메인을 떼려면 컴포넌트 등록·순회의 타입
소거가 선행돼야 한다 — 그것은 SceneGraphRedesignPlan(파괴 단일점 부재 등
CRITICAL 4종)의 소관이지 프로젝트 분할로 풀 문제가 아니다.

### 2.2 ClrScript 분할선 — 가장 싸지만 공짜가 아니다

ClrScript로의 역방향 유입은 **21건**으로 전 분할선 중 최소다. 그 실체:

**(a) 씬 수명 훅 7지점** — 코어가 C# 실행 계층을 직접 부른다:

| 파일 | 무엇을 |
|---|---|
| Scene.cpp | ClrHost 틱·ScriptComponent 파괴 |
| SceneManager.cpp | 어셈블리 리로드·씬 언로드 통지(SweepOrphans) |
| RuntimeFrame.cpp | 프레임 루프의 TickScripts |
| ComponentFactory.cpp | ScriptComponent 생성·Invalid 폴백 |
| LifecycleRegistry.cpp | 스크립트 수명 등록 |
| Entity.cpp | ScriptObjectRegistry |
| RegisterReflectManual.h | 리플렉션 등록 |

**(b) 도메인의 관리 측 연동 6건** — 설계상 의도된 결합:
BehaviorTreeComponent·BTGraphFlatten(BT 관리 측 재설계 9-8: "BT 틱이
동기라 노드만 C#은 불변식을 깬다"), AnimationEventBridge·AnimationState
(ManagedAniBehavior), PlayerInput, FSMState(IScriptedFSM).

분할하면 (a)의 7지점 전부를 역전 계약(IScriptHost 류 — E4-2 contributor·
E4-6a sink와 같은 패턴)으로 절단해야 하고, (b)는 BT·애니·입력 3도메인이
분할선에 걸쳐 있어 절단면이 깨끗하지 않다.

## 3. 외부 소비자 — 분할해도 소비자는 둘 다 문다

Editor 149건·Player 12건. Editor 소비 상위는 SceneManager(13)·Scene(11)·
Entity(8)에 ClrHost(7)와 전 도메인이 골고루 섞여 있다. 분할 시 두 조각을
모두 참조하게 되므로 **소비자 관점의 이득은 0**이다. Player의 스크립트
없는 구성(ClrScript 비링크)은 이론상 가능해지지만, 게임 실행체가 C#
게임플레이를 전제하는 현 제품 방향에서 실익이 없다.

## 4. 판단

| 안 | 내용 | 비용 | 이득 | 판정 |
|---|---|---|---|---|
| 1 | **단순 개명 → SceneRuntime** | E7-b급 기계 스윕(폴더·vcxproj·sln·검사기 resolve_owner 폴더 힌트·게이트 11파일·$(SolutionDir) 경로) | 이름이 실체를 말한다 | **권장** |
| 2 | 2분할(SceneRuntime+ScriptRuntime) | 수명 훅 7지점의 인터페이스 역전 + 걸친 3도메인 재배선 + ClrHost 112KB 재배선 | C# 호스팅의 독립 진화 | **보류** — 아래 재평가 조건 |
| 3 | 도메인 분할 | SceneCore 양방향 강결합 전면 절단 | — | **기각** — SceneGraph 재설계 소관 |

**안 2의 재평가 조건**: ① BT 관리 측 이관(B축, 9-8) 완료 — (b)의 최대
지분이 자연 소멸, ② SceneGraph E1(파괴 단일점) 착지 — (a)의 파괴 훅이
단일점으로 모여 역전 계약이 한 곳이 된다. 두 트랙이 착지하면 역방향
유입이 실측 기준 절반 이하로 줄어들 것이므로 그때 재실측해 판단한다.

**즉시 실행 가능한 결론**: 개명(안 1)은 분할 결정과 독립적으로 지금 해도
된다 — 안 2로 가더라도 SceneRuntime이라는 이름은 그대로 살아남는다
(ScriptRuntime을 나중에 떼어 내는 형태).

</details>
