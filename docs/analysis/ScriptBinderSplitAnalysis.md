# ScriptBinder 분할 판단 자료 (E7 잔여 결정)

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
