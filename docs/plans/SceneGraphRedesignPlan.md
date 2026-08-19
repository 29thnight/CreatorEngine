# 씬 그래프 재설계 — Entity·Component·Prefab (UE 로드맵 반영)

작성: 2026-08-16 · 계기: GameObject/Object 기반 설계 재평가 + "기존 보완계획을 제거하고
Scene Graph·Entity·Component·Prefab에 대한 UE 로드맵의 내용을 반영한 신규 구조
재설계안"을 세우라는 요구.

**이 문서는 `ObjectModelModernizationPlan.md`(2026-08-10)를 대체한다.** 그 계획에서
완료된 것(A0 결함 수술 3건 · P0 프리팹 지혈)과 실측·게이트 관례 중 유효한 것은 이
문서가 승계하고(§8 대체 매핑표), 원문은 워킹트리에서 제거했다 — git 이력으로 조회
가능하다(`git show HEAD:ObjectModelModernizationPlan.md`).

관련 문서: `Phase5CouplingPlan.md`(간선 절단 — 방향 동일),
`UISystemRedesignPlan.md`(프리팹 오버라이드·instanceID 정책이 트랙 P와 겹친다),
`MaterialPipelinePlan.md`(M5 직렬화 단일화가 트랙 P의 왕복 회귀를 판정에 쓴다),
`UtilityFrameworkModernizationPlan.md`(H4가 트랙 E의 조회 수렴에 재료를 공급한다),
`BuildPipelinePlan.md`(트랙·게이트 관례의 원조).

---

## 0. 재평가 판정 요약 (2026-08-16 전수 재판독)

서브시스템 6개(오브젝트 코어·컴포넌트·씬·트랜스폼·프리팹·C# 경계)를 병렬로 전수
재판독했다. 판정은 셋이다.

1. **8-10 평가의 실측은 대체로 유효하다.** 4중 정체성, 컨테이너 규모 불일치
   (reserve 30 vs 실사용 0~6), 이름 해시 typeID, 프리팹 반쪽 상태 — 전부 재확인됐다
   (§1.1). 다만 서술 4곳은 실측과 어긋나 정정한다(§1.3).
2. **기존 계획에 없던 결함이 다수 나왔다 — CRITICAL 4건 포함**(§1.2). 넷 다
   "파괴·소유 경로가 단일점을 거치지 않는다"는 같은 뿌리에서 나왔고, 이것이 신규
   설계의 규칙 2(파괴 단일점)를 결정했다.
3. **처방을 바꾼다.** 8-10 계획은 "표면 유지, 내부만 점진 교체"였다. 이번 결정은
   저작 모델 자체를 UE 로드맵의 Scene Graph 3원 구조(Entity·Component·Prefab)로
   재정의하는 것이다 — 단, C# 스크립팅 표면 계약은 그대로 유지한다(§2.3). UE
   자신도 Actor를 남겨둔 채 Scene Graph를 세웠다. 우리는 정본을 아래층(Entity
   코어·SceneGraph 스토어)으로 내리고, 전환기 동안 GameObject 심볼이 파사드로
   공존하다가 **최종적으로 Entity로 리네임**한다(사용자 결정 — 트랙 E6, K1-b 이후).

## 1. 재평가 — 무엇이 확인되고 무엇이 새로 나왔나

### 1.1 유효 승계분 (2026-08-16 재검증 완료)

8-10·8-14 실측 중 이번에 코드로 재확인한 것:

| 항목 | 재검증 근거 |
|---|---|
| 컨테이너 `reserve(30)` ×2, 생성자 3벌 전부 | `GameObject.cpp:19-20,33-34,52-53` |
| `GetComponent<T>`가 임시 shared_ptr 생성(atomic 2회), 호출처 107곳 | `GameObject.inl:60,73` |
| typeID = `typeid(T).hash_code()` uint32 절단 — 정본이 타입 **이름** | `TypeTrait.h:199` |
| 컴포넌트 타입 약 30종 < 64 → uint64 마스크 성립 | `LifecycleRegistry.cpp` 등록 31건 |
| `shared_ptr<GameObject>` 99곳/23파일, `enable_shared_from_this`가 UI 내비게이션·Canvas·인스펙터·게임 스크립트까지 확산 | `UIManager.cpp:439-602`, `InspectorWindow.cpp:1458-1553` 외 |
| 프리팹 P-a(재연결 주석 처리)·P-b(`m_prefabOriginal` 비직렬화)·P-d(YAML 덤프 비교)·P-e(all-or-nothing)·P-f(Destroy 후 재생성)·P-i(중첩 평탄화) 잔존 | `SceneManager.cpp:1105-1225`, `GameObject.h:176-177`, `ReflectionYml.h:344-349`, `PrefabUtility.cpp:104-146`, `Prefab.cpp:88-119` |
| P0 반영 확인: 캐시 소유(unique_ptr)·`UnregisterInstance` 배선·cout 0건 | `PrefabUtility.cpp:8-16,77-88,181-201`, `GameObject.cpp:107` |
| A0 반영 확인: 자식 클론 부모 수정·SetParentIndex 단일점·`m_isEnabled` protected | `Object.cpp:146`, `GameObject.cpp:246-250`, `Object.h:66-67` |

오브젝트당 1,152 B·힙 할당 4회, 씬 평균 0.43개/프리팹 89%가 0개, 런타임 typeID
사용 182곳, 디스크 이름해시 3,902줄 — 8-14 수치도 그대로 유효하다(구조 변경 커밋
없음을 git log로 확인).

### 1.2 신규 결함 (이번 재평가에서 발견)

| # | 결함 | 근거 | 심각도 |
|---|---|---|---|
| N-1 | **`GameObject::Destroy`가 `Object::Destroy`를 호출하지 않는다** — `EraseGUID` 호출처는 `Object.cpp:27` 단 한 곳인데 GameObject 경로는 그곳을 지나지 않아, 파괴할 때마다 GUID 레지스트리(`g_guids`)에 항목이 영구히 남는다. 언바운드 누수 + `make_guid` 중복 검사 비용 단조 증가 | `GameObject.cpp:95-126` vs `Object.cpp:20-28` | CRITICAL |
| N-2 | **PrefabEditor 저장(Apply) 시 결정적 이중 해제** — `SavePrefab`이 같은 경로 키의 캐시 항목을 erase(unique_ptr 소멸 → 원본 delete)한 직후 `PrefabEditor.cpp:46`이 같은 포인터를 다시 `delete` | `PrefabEditor.cpp:14,44-47`, `PrefabUtility.cpp:171-176` | CRITICAL |
| N-3 | **드래그-투-프리팹 생성도 이중 해제** — `CreatePrefab`이 `m_createdPrefabs`(unique_ptr 벡터)로 소유권을 가져갔는데 호출자가 수동 `delete prefab;` | `ContentsBrowserWindow.cpp:186-193`, `PrefabUtility.cpp:8-16,63` | CRITICAL |
| N-4 | **`ScriptObjectRegistry::Unregister` 호출처가 코드베이스 전체에 1곳뿐** — C# 스크립트가 명시적으로 그 핸들에 Destroy를 부른 경로(`ClrHost.cpp:681`)만. 엔진 주도 파괴·부모 파괴의 자식 캐스케이드(`GameObject.cpp:118-125`)는 레지스트리를 거치지 않아 세대가 오르지 않고, 같은 프레임 끝 `Scene::DestroyGameObjects`(`Scene.cpp:1661-1692`)가 메모리를 해제한다 — 스크립트·BT를 가진 오브젝트는 전부 등록되므로(`ClrHost.cpp:2551,2615`) C#이 캐시한 핸들이 댕글링. "세대 핸들이 UAF를 구조적으로 막는다"는 주석(`ScriptObjectRegistry.h:37`)은 이 경로들에서 거짓 | `ClrHost.cpp:681` 유일 호출 | CRITICAL |
| N-5 | `CreateScene`만 이전 Scene 객체를 delete하지 않는다(`LoadSceneImmediate:442`·`BeforeAwakeSceneLoad:866`은 delete함) — Scene 소멸자가 안 불려 델리게이트 구독자도 누적 | `SceneManager.cpp:335-352` | HIGH |
| N-6 | **씬은 슬롯맵이 아니다** — `DestroyGameObjects`가 파괴 1회마다 erase_if 압축 + 생존자 **전원**의 index/parent/root/children을 indexMap으로 재부여. "tombstone으로 인덱스 안정" 서술은 절반만 사실 | `Scene.cpp:1661-1726` | HIGH |
| N-7 | `UpdateModelRecursive`에 순환 가드가 없다 — 형제 구현 `LayoutUINode`는 방문집합+깊이 64 제한 보유(지난 세션에 겪고 고친 교훈이 이식되지 않음). 프레임당 2~3회 도는 par 핫패스라 계층 오염 시 즉시 행 | `Scene.cpp:1791-1852` vs `1854-1936` | HIGH |
| N-8 | `Transform::UpdateWorldMatrix`가 부모를 무가드 역참조 — `DetachGameObjectHierarchy`(`Scene.cpp:303`)가 만든 tombstone 구멍(DDOL 분리)과 겹치는 좁은 창에서 널 역참조 | `Transform.cpp:217-226` | HIGH |
| N-9 | `Object::Instantiate`가 원본 소속 씬이 아니라 **항상 활성 씬**에서 자식 인덱스를 조회 — DDOL·백그라운드 씬 원본이면 엉뚱한 오브젝트 복제/누락 | `Object.cpp:126,135-139` | MEDIUM |
| N-10 | instanceID 재할당(`Instantiate`·`MakeInstanceID`)이 이전 GUID를 erase하지 않아 호출마다 고아 항목 누적 | `Object.cpp:123`, `ComponentFactory.cpp:672`, `TypeTrait.h:213-225` | MEDIUM |
| N-11 | DDOL 로드가 태그를 레이어 API로 등록(복붙 버그) — 재로드된 DDOL은 태그 검색 불가 | `SceneManager.cpp:1245-1248` (올바른 패턴: `1129-1131`) | MEDIUM |
| N-12 | `SceneManager::SaveScene` 정상 경로에 return 누락 — 비void 함수 끝 도달(UB) | `SceneManager.cpp:367-407` | MEDIUM |
| N-13 | `Scene::GetGameObject`가 범위 초과 인덱스를 **조용히 루트로 폴백** — 계층 오염을 은닉(올바른 `TryGetGameObject`가 바로 옆에 공존). 반면 범위 내 tombstone은 null 그대로 반환해 `CreateGameObject` 등 무검사 호출부에서 크래시 여지 — 두 방향 모두 문제 | `Scene.cpp:217-243`, `169-170` | MEDIUM |
| N-14 | UI 타입 루트는 인스턴스화 시 새 instanceID를 받지 않는다 — 같은 UI 프리팹 2회 소환 시 ID 충돌(의도 여부 미확인, UISystemRedesignPlan C3와 연동) | `Prefab.cpp:153-156` | MEDIUM |
| N-15 | 죽은 코드 무더기: nested `GameObject::Type`(참조 0) · `StaticGameObjectType`(참조 0) · `m_inverseMatrix`(계산 코드 없음, 호출부 0 — B-1의 "온디맨드 강등" 대상이 아니라 그냥 삭제 대상) · `GetComponentByTypeID`(항상 nullptr) · `Prefab.cpp:37,68`의 `gameObjNode` | `GameObject.h:20-30`, `GameObjectType.h:17-23`, `Transform.h:81`, `GameObject.cpp:205` | INFO |

**N-1~N-4의 공통 뿌리는 하나다: 파괴와 소유가 단일점을 거치지 않는다.** GUID
erase는 GameObject가 우회하고, 관리 핸들 무효화는 호출자 하나만 알고, 프리팹
소유권은 타입이 아니라 관례로만 유지된다(그래서 두 호출부가 각자 delete했다).
§3 규칙 2가 이 뿌리를 겨냥한다.

### 1.3 기존 계획 서술의 정정

| 구계획 서술 | 실측 |
|---|---|
| "A0의 SetParent 단일점 덕에 계층 4필드 갱신 지점은 이미 한 곳"(A1-2) | 단일점은 `m_parentIndex`+`Transform::m_parentID` 쌍뿐. **`m_childrenIndices`는 최소 6파일 15곳**에서 직접·별칭 경유로 조작(`GameObject.cpp:239-240,243`, `Object.cpp:124,138,147,149`, `Prefab.cpp:160,178`, `Scene.cpp:106,269,361,1687`, `HierarchyWindow.cpp:347,553`, `GameObjectCommand.h:139`), **`m_rootIndex`는 3파일 5곳**(`Object.cpp:82,148`, `ModelSceneBridge.cpp:197,331`, `Scene.cpp:1707`). 별칭(참조 변수) 경유 쓰기가 있어 grep 카운트는 하한이다 — 트랙 E-2는 전수 인벤토리 재작성에서 시작한다. (검증 주의: `GameObjectCommand.h:237`의 `m_rootIndex`는 커맨드 클래스 자체의 동명 멤버라 대상이 아니고, `Object.cpp:132`는 주석 처리된 죽은 줄이다 — 동명 필드·죽은 줄이 grep을 오염시킨 실례) |
| "조회 API 4갈래" | **9갈래** — 전역 `Find*` 4종 + 동일 로직을 중복 구현한 `OwnerSceneFind*` 4종(`GameObject.cpp:346-409`) + 무검사 `SceneObjectAt` |
| "인덱스 안정성은 tombstone으로 처리 중 — slot map의 절반을 이미 손으로 구현" | 절반이 아니라 **거의 0** — 파괴 시 전원 재인덱싱(N-6). EntityHandle은 최적화가 아니라 정합성 회복이다 |
| "`m_index`는 uint32" | `using GameObjectIndex = int`(`GameObjectIndex.h:8`). INVALID_INDEX는 uint32_max의 int 축소로 우연히 -1, `SetParentID(uint32)`로 넘어갈 때 다시 0xFFFFFFFF — 두 표현이 우연히 정합해 동작 중 |

### 1.4 보존해야 할 자산 (재설계가 깨면 안 되는 것)

- **C# 경계의 "틱당 1회 크로싱" 계약** — 진입점 4개(`ClrHost.h:28-31`), 스크립트
  순회는 관리 측 BehaviourRegistry에서 종결, Behaviour 파생 GetComponent는 경계를
  넘지 않음(`Component.cs:39-52`). 재설계로 컴포넌트별 네이티브 콜백을 늘리면 이
  계약이 깨진다. 이 계약의 실체는 **크로싱 횟수와 ABI**이지 훅 이름이 아니다 —
  생명주기 훅 이름 집합은 트랙 L이 교체한다(사용자 결정, §2.2).
- **블리터블 핸들 ABI** — `ScriptObjectHandle`(uint32×2)과 평면 함수포인터 표
  (ScriptApiTable, Version 필드). 교체 시 배치 유지 또는 명시적 버전업.
- **영속 참조는 정체성 경유** — `[SerializeField]` 오브젝트 참조는 핸들 값이 아니라
  GameObject 정체성으로 왕복(`ClrHost.cpp:2816-2834`). 세션 로컬인 핸들을 파일에
  적지 않는 현행이 옳다 — 신규 설계도 유지.
- **씬 언로드 경로의 안전성** — `NotifySceneUnload`→`Clear`(전 슬롯 세대 일괄 증가,
  `ClrHost.cpp:2513-2518`)는 올바르다. N-4는 씬 도중 개별 파괴에 국한된다.
- **페이즈 리스트 6종의 등록/해제 구조** — 편입 2지점·해제 1지점(FlushPendingDestroy),
  swap-and-pop과 재진입 안전까지 견고(`Scene.cpp:700-758,964`). 트랙 C의 승격 대상이지
  재작성 대상이 아니다.
- **네이티브 컴포넌트 노출표** — `NativeComponentTable` 13종, 새 타입은 표 한 곳만
  고치면 되는 구조(`Component.cs:40-105`).

## 2. UE 로드맵의 Scene Graph — 무엇을 가져오나

### 2.1 조사 사실 (2026-08-16, Epic 공식 문서·발표 기준)

**로드맵 위치.** Scene Graph는 UE6의 신규 게임플레이 프레임워크다 — State of
Unreal 2026 "The Road to UE6"에서 Verse 위에 완전히 새로 구축되며 UE5와 UEFN을
하나의 엔진으로 통합하는 축으로 발표됐다(UE6 Early Access 2027년 말 목표).
UEFN 검증 이력: 2024-03 GDC 프리뷰 발표 → 2024-06 v30.10 Experimental 공개 →
2024-12 Beta 목표였으나 2025-03 로드맵에서 Q2로 연기(Epic 공표 사유는 완성도 확보)
→ 2025-06 v36.00 Beta 출시(이 릴리스에 LUF 좌표계 전환 등 breaking change 포함).
UE 본체 5.6/5.7에는 "longer-term" 항목일 뿐 GA가 아니다. **Actor/Blueprint는 UE6
초기에도 공존하고, 성숙 후 변환 도구와 함께 점진 폐지**가 공식 방침이다. 저층에는
데이터 지향 ECS인 Mass가 별도로 있다 — Scene Graph는 Mass의 대체재가 아니라 그 위의
**저작(authoring) 레이어**이며, Actor ↔ Scene Graph ↔ Mass 상호운용 계층 작업은
Epic 채용 공고 문면으로 확인된다(공식 발표 문서로는 미확인 — 출처 등급을 구분해
둔다).

**의미론.**

- **Entity** — 컴포넌트와 다른 엔티티의 순수 컨테이너. 단일 트리(루트 = simulation
  entity). 노출 데이터 멤버가 없고, Epic은 "로직은 컴포넌트에 두라"고 명시 권고.
  **트랜스폼조차 내장이 아니라 `transform_component`다** — Verse로 만든 엔티티는
  트랜스폼이 없을 수 있다.
- **Component** — 데이터+행동. 6단계 생명주기(OnInitialized → OnAddedToScene →
  OnBeginSimulation → OnEndSimulation → OnRemovingFromScene → OnUninitializing).
  **엔티티당 동일 타입(서브클래스 포함) 1개 제한** — 다중이 필요하면 자식 엔티티를
  만든다. 에디트·플레이 양쪽 모드에서 실행.
- **Prefab** — entity를 상속한 클래스가 곧 prefab(별도 타입 계통이 아님). 정의는
  모든 인스턴스가 공유하는 단일 진실 공급원이고, 정의 수정은 전 인스턴스에 자동
  전파된다. 오버라이드는 **컴포넌트 속성 단위**로 추적·표시되며 "오버라이드는 해당
  컴포넌트 수준에서 부모 설계를 대체"하고 나머지는 계속 전파를 받는다. **중첩
  프리팹 지원.**
- **계층·쿼리** — global = local ∘ parent-global 합성(Origin으로 기준 대체 가능).
  `GetParent`/`AddEntities`/`GetEntities`, `FindDescendantEntities(WithComponent/WithTag)`,
  `SendUp`/`SendDown` 씬 이벤트, 태그. 베스트프랙티스: 매 프레임 광역 쿼리 금지 —
  시작 시 1회 캐시 + 이벤트 구독.
- **실무 함정(커뮤니티 보고)** — 재부모화·재추가 관련 개별 버그(가시성, 복제 누락,
  제거 후 재추가 시 부모 변경 불가 제약)가 공식 포럼·문서에 보고돼 있다.
  "생명주기 통째 재순환"으로 일반화할 확정 근거는 없다 — 다만 어느 쪽이든 우리
  정책은 같다(§2.2 마지막 행).

**생명주기 관점의 전환 (사용자 분석 2026-08-16 반영).** Scene Graph의 생명주기는
Actor 생명주기의 리네임이 아니다. 핵심 전환은 **"스크립트 인스턴스가 Actor와 함께
태어나 BeginPlay→Tick→EndPlay를 도는 구조"에서 "컴포넌트가 씬 그래프의 phase에
참가하는 구조"로의 이동**이다. 네 가지 함의가 있다:

1. **기준점이 Actor(오브젝트)에서 Component로 이동한다.** BeginPlay는 "Actor가
   게임플레이를 시작함"이지만 OnBeginSimulation은 "**이 컴포넌트가** simulation
   phase에 진입함"이다. Actor가 살아 있으면 컴포넌트들이 함께 움직이는 구조에서,
   각 컴포넌트가 독립적으로 Scene/Simulation phase를 갖는 구조로 — 런타임 조합
   변경(AIComponent 제거, BurnComponent 추가 등)이 자연스러워진다.
2. **Existence와 Simulation이 분리된다.** 씬에 존재함(OnAddedToScene)과 시뮬레이션
   중임(OnBeginSimulation)은 별개 축이다 — 에디터에서 씬을 열어 객체가 존재한다고
   게임플레이 로직이 실행되어야 하는 것이 아니다. 에디터와 런타임을 하나의 씬
   그래프로 관리하는 데 이 분리가 결정적이다.
3. **phase catch-up이 API 규칙이다.** 이미 씬에 있는(그리고 시뮬레이션 중인)
   엔티티에 컴포넌트를 추가하면 OnAddedToScene(→OnBeginSimulation)까지 현재
   phase에 맞춰 자동 진입시킨다 — "나는 BeginPlay를 놓쳤는데?" 상태가 존재하지
   않는다.
4. **Tick은 생명주기의 중심이 아니다.** 기본 제공 Event Tick이 아니라,
   OnBeginSimulation에서 **필요한 컴포넌트만** TickEvent를 구독한다. 전 객체
   Tick() 순회에서 "필요한 컴포넌트의 콜백/이벤트/비동기 태스크"로 — 대규모 씬에
   적합한 방향. Verse의 spawn/suspends(코루틴+Sleep)가 이것과 결합해, **생명주기
   콜백 자체보다 그 콜백에서 시작시킨 태스크들의 수명 관리**가 설계의 중심이 된다.

Actor 모델과의 대응(1:1이 아니라 설계적 이해용):

| Actor / Blueprint | Scene Graph / Verse |
|---|---|
| Constructor | Component 생성 |
| RegisterComponent | Entity에 Component 추가 |
| OnRegister | ≈ OnAddedToScene |
| BeginPlay | OnBeginSimulation |
| Tick | TickEvent 구독 / async task |
| Event dispatcher | Verse event |
| Timer | Sleep / async logic |
| EndPlay | OnEndSimulation |
| UnregisterComponent | ≈ OnRemovingFromScene |
| DestroyActor | Entity를 그래프에서 제거 |

### 2.2 채택 / 번안 / 기각 결정표

UE를 베끼는 것이 목적이 아니다 — 각 항목을 우리 실측(§1)에 대고 판정했다.

| UE 개념 | 판정 | 근거 |
|---|---|---|
| Entity = 순수 컨테이너, 계층·정체성의 단일 정본 | **채택** | 4중 정체성(§1.1)·계층 4필드 분산(§1.3)·조회 9갈래가 전부 "정본이 여럿"에서 나온 병. Entity 코어 하나로 수렴 |
| 파괴·수명은 핸들 유효성 검사 기본(UE의 TWeakObjectPtr 문화) | **채택** | N-1~N-4의 뿌리. EntityHandle{index,generation} + 파괴 단일점(§3 규칙 2) |
| Prefab 3원칙: 정의=진실 공급원 · 오버라이드=명시적·속성 단위 · 중첩=참조 유지 | **채택** | 현행은 정반대(완전 스냅샷 + 문자열 비교 추론 + 평탄화 — `Prefab.cpp:88-119`). 구계획 P1과 같은 방향이며 P-i(중첩)를 선택에서 **핵심으로 승격** |
| 저작 모델 / 실행 모델 분리 + 상호운용 계층 | **채택** | UE의 층 분리 철학(Actor↔SG↔Mass — 상호운용 계층은 채용 공고 수준 근거, §2.1). 우리 대응: GameObject 파사드 ↔ Entity 코어 ↔ SceneGraph 스토어(SoA)·렌더 프록시. 렌더·물리 소비자는 스토어를 직접 읽는다 |
| 점진 이행 + 변환 도구, 하드 컷오버 금지 | **채택** | Epic조차 Experimental 1년+과 breaking change를 겪었다. 슬라이스·게이트·읽기 호환(§5)이 우리의 등가물 |
| 트랜스폼도 컴포넌트(`transform_component`) | **채택** (사용자 결정 2026-08-16 — 초안의 번안을 번복) | 저작 모델에서 Transform은 **TransformComponent**가 된다 — GameObject 값 멤버 소멸, 부착·조회·직렬화가 다른 컴포넌트와 동일 표면. 단 **데이터는 여전히 TransformStore(SoA)가 소유**한다(컴포넌트는 스토어 슬롯의 핸들 뷰) — 규칙 7의 저작/실행 분리가 이것을 가능하게 하고, 렌더 직결(S-4) 이점은 그대로다. "엔티티는 트랜스폼이 없을 수 있다"도 함께 성립: UI 엔티티는 RectTransform만, 순수 로직 엔티티(GameManager류)는 공간 컴포넌트 0개 — 지금 UI 오브젝트마다 행렬 3개+벡터 3개가 죽은 채 실려 있다(`Scene.cpp:1806-1813`이 UI에서 아무것도 안 함). 상세는 트랙 S |
| 컴포넌트 타입당 1개, 다중은 자식 엔티티로 | **번안** | 네이티브 컴포넌트는 현행도 타입당 1개(AddComponent가 기존 것 반환) — 명문화만 한다. C# 스크립트 다중 부착(`AddComponentAllowMultiple`)은 이미 계약이라 유지 — ScriptComponent가 스크립트 목록을 담는 현행 구조가 UE의 "자식 엔티티" 해법과 등가 역할 |
| Component 중심 생명주기 6단계 | **채택** (사용자 결정 2026-08-16 — 초안의 기각을 번복하고 같은 날 전면 확장) | Behaviour 생명주기를 OnInitialized → OnAddedToScene → OnBeginSimulation → OnEndSimulation → OnRemovingFromScene → OnUninitializing으로 전환한다(**트랙 L**). 리네임이 아니라 기준점 이동이다(§2.1 "생명주기 관점의 전환") — 수명 단위가 오브젝트에서 컴포넌트로. OnEnable/OnDisable은 활성 전이 보조 훅으로 존치. "틱당 1회 크로싱"·ABI 계약은 불변 |
| Existence와 Simulation의 분리 + phase catch-up | **채택** | 엔티티가 ScenePhase(Detached/Attached/InScene/Simulating)를 들고, AddComponent가 현재 phase까지 자동 진입("BeginPlay를 놓친 컴포넌트" 상태가 타입상 소멸). 에디터 씬=InScene, 재생 진입=Simulating 전이 — 에디터와 런타임을 하나의 씬 그래프로 관리하는 근거. 씨앗: `m_pendingAwake/Start` 드레인이 절반을 이미 구현 |
| Tick은 opt-in(OnBeginSimulation에서 구독) | **채택(번안)** | 전 객체 가상 Update 순회 폐지 방향. 명시 구독 API가 정본, 가상 오버라이드 감지(LifecycleRegistry가 이미 하는 것)는 암묵 구독으로 병존해 기존 코드 무비용(트랙 L4). C# 348개 스크립트는 BehaviourGenerator 감지로 암묵 구독 |
| Verse가 맡는 역할(씬 그래프의 저작·시뮬레이션 스크립팅 계층) | **번안 — C#/CoreCLR이 맡는다** (사용자 결정) | 커스텀 컴포넌트 저작=C# 클래스(`class(component)` 대응), spawn/suspends=C# 태스크+SimulationScope(취소 토큰, EndSimulation 일괄 해지), TickEvent 구독=L4 스케줄러, `@editable`=[SerializeField]. C#은 파사드가 아니라 이 생명주기의 1급 저작 언어다 |
| Verse 언어 자체·소프트웨어 트랜잭션 메모리·분산 서버 | **기각** | 언어를 들이지는 않는다 — 역할은 위 행처럼 C#/CoreCLR이 승계. STM·분산 실행은 우리 문제(정체성·수명·프리팹)와 직교라 계속 비범위 |
| Mass류 아키타입 ECS 전면 전환 | **기각** + 외부 ECS(EnTT/Flecs) 채택 자체를 배제(사용자 결정 2026-08-16) | UE 스스로 Scene Graph를 Mass 위 저작 레이어로 분리했다 — "모든 오브젝트를 ECS화"는 UE도 안 한다. 구계획의 D 게이트(EnTT/Flecs 평가)는 폐지 — 대량 개체 수요가 생겨도 자가 스토어 확장으로 다룬다(§4) |
| 재부모화가 컴포넌트 상태에 개입하는 의미론 | **기각(반면교사)** | UE 쪽은 재부모화·재추가 관련 제약과 버그 보고가 있는 지점(§2.1 — "생명주기 통째 재순환"까지는 미확정). 우리는 재부모화가 생명주기를 돌리지 않는 현행 의미론을 유지하고 §6 함정에 명시 |

### 2.3 용어 (신규 모델)

| 용어 | 정의 |
|---|---|
| **Entity** | 씬 그래프의 노드. 정체성(EntityHandle)+이름/태그+컴포넌트 셋. 코드 심볼도 최종적으로 `Entity`가 된다(사용자 결정 — 트랙 E6, K1-b 이후). 전환기에는 `GameObject`가 별칭으로 공존 |
| **EntityHandle** | `{uint32 index, uint32 generation}`, 0=무효. 런타임 정체성의 유일 정본. ScriptObjectHandle과 배치 동일 → 경계 ABI 무변경 |
| **SceneGraph 스토어** | 계층(부모/자식/루트)+TransformStore(SoA)의 유일 정본. 순회·유효성 검사·순환 가드를 정본 API 한 벌로 제공 |
| **Component** | 데이터+행동. 네이티브는 타입당 1개+uint64 타입마스크, 스크립트는 ScriptComponent 경유 다중 |
| **TransformComponent** | 공간 컴포넌트(엔티티당 Transform 또는 RectTransform 하나, 없을 수도 있음). 데이터 없는 핸들 뷰 — 정본은 TransformStore(트랙 S) |
| **Prefab 정의** | DataSystem이 소유하는 불변 에셋(FileGuid 정본). 모든 인스턴스의 단일 진실 공급원 |
| **PrefabInstance** | 엔티티에 붙는 인스턴스 데이터: 정의 GUID + 명시적 오버라이드 목록(속성 경로 단위) + 추가/제거 노드 목록 |

## 3. 목표 구조

```
[스크립팅 표면]   C#/CoreCLR = 씬 그래프의 저작 언어(Verse의 자리 — 트랙 L)
                  Behaviour(6단계 생명주기 + opt-in 틱 + SimulationScope) · GameObject 파사드
                        │ 계약 불변: 틱당 1회 크로싱 · 블리터블 핸들 ABI · 영속 참조는 정체성 경유
[저작 모델]       Entity = { EntityHandle + 이름/태그 + 컴포넌트 셋(SBO+타입마스크)
                            + PrefabInstance(선택) }
                  Prefab 정의(에셋) ←스탬프→ PrefabInstance(오버라이드는 명시 데이터)
[씬 그래프]       SceneGraph 스토어 = 계층 + TransformStore(SoA) 의 유일 정본
                        │ 순회 API 한 벌(순환 가드·유효성 검사 내장)
[실행 모델]       ScenePhase(Existence/Simulation 분리 + catch-up — 트랙 L)
                  SystemSchedule(페이즈 리스트 승격, 틱은 opt-in — L4·C) · LifecycleRegistry(유지)
                  파괴 단일점: 슬롯 해제 = 세대 증가 + GUID erase + 관리 핸들 무효화
[경계]            ScriptObjectRegistry → 코어 핸들과 통합(옆 테이블 소멸)
                  렌더 프록시 ← 변경분 커밋(dirty 인덱스만)
```

규칙 일곱. 1~6은 구계획 승계(문구 조정), 7이 신설이다.

1. **정체성은 핸들 하나.** EntityHandle이 런타임 정본, GUID는 영속(직렬화·에셋 참조)
   전용, 이름은 조회 편의. 조회 9갈래는 `Resolve(EntityHandle)` + GUID 조회 둘로
   수렴한다.
2. **소유는 씬 슬롯 테이블 하나, 파괴는 단일점 하나.** 슬롯 해제 지점에서 세대
   증가·GUID erase·관리 핸들 무효화가 **구조적으로 함께** 일어난다 — 호출자가
   기억해야 하는 정리 목록을 없앤다. N-1(GUID 누수)·N-4(관리 UAF)는 이 규칙의
   반례이자 근거다. 프리팹 등 에셋의 소유는 타입으로 강제한다 — 소유자 아닌 코드에
   mutable raw 포인터를 내보내지 않는다(N-2·N-3의 재발 방지).
3. **계층·트랜스폼의 정본은 SceneGraph 스토어.** GameObject의 계층 4필드는 소멸.
   순회는 정본 API 한 벌(순환 가드·깊이 제한·유효성 검사 내장 — 현재 4곳이 제각각
   재귀하며 가드 수준이 다르다). dirty는 push, 재계산은 lazy pull — 매 프레임 2~3회
   전체 풀패스(`Scene.cpp:1068-1078,992-994`)를 변경 서브트리만 도는 구조로.
4. **프리팹은 정의=진실 공급원, 오버라이드는 명시 데이터, 중첩은 참조.** "지금 값이
   원본과 다른가"를 문자열로 되묻는 현행(P-d·P-e)을 폐기하고, 인스턴스가 "무엇을
   덮었는지"를 속성 경로 단위로 들고 저장한다. 프리팹 안의 프리팹은 펼치지 않고
   참조 노드로 남긴다(P-i 해소).
5. **컨테이너는 실사용 규모(0~6개)에 맞춘다.** SBO 배열+uint64 타입마스크. 예약은
   측정에서 나온다.
6. **타입 식별자는 층마다 다르다.** 런타임은 순차 인덱스(비트 위치), 디스크는 고정
   UUID(리네임 불변). 현행 이름 해시는 리네임 시 컴포넌트를 조용히 지운다.
7. **저작 모델과 실행 모델을 분리한다.** 렌더·물리 소비자는 파사드가 아니라
   스토어를 직접 읽고, 커밋은 변경분만 넘긴다(현행: dirty 무관 전 렌더러 매 프레임
   2단 값복사 — `Scene.cpp:451-509`, `ProxyCommand.cpp:403-494`). UE의 저작/실행
   층 분리 철학(§2.1)의 우리식 번안이다.

## 4. 실행 계획

판정 관례 승계: 각 슬라이스는 독립 커밋, ① `CreatorEngine.sln` 전체 빌드 그린
(Debug|x64, VS 18 MSBuild, 약 2분) ② `Tools/regression/run-all.ps1`(**pwsh로** —
5.1은 한글 주석이 깨져 거짓 실패) ③ 성능 슬라이스는 측정 첨부 ④ 프리팹·계층
슬라이스는 왕복 검사(`verify-prefab-roundtrip.ps1`) 포함.

트랙 의존 관계 (명시 목록 — 여기 없는 의존은 없다):

| 트랙/슬라이스 | 선행 조건 | 비고 |
|---|---|---|
| G (지혈) | 없음 — 즉시 | G1의 임시 배선은 E1이 회수 |
| K (컴포넌트) | 없음 — 즉시, G·E와 병행 | K0은 오늘 가능 |
| P1 (명시 오버라이드) | 없음 — G·K·E와 병행 가능 | 포맷 예외 1 |
| E (Entity 정체성) | G1 (파괴 단일점 씨앗) | E1이 슬롯맵·단일점 완성. **E6(리네임)은 K1-b 이후, E7(enum 소멸)은 K1-a·S3 이후** |
| P2 → P3 → P4 | P2는 **E1+P1**, P3는 P1, P4는 P1~P3 | P2 완료 = 왕복 검사 `-Strict` |
| S (SceneGraph 스토어) | E1 (핸들 없인 스토어 인덱스가 못 선다) | S4까지가 한 묶음 |
| C (시스템 스케줄) | C1은 선행 없음, C2·C3는 S 이후 권장 | |
| L (Component 중심 생명주기) | G 이후 권장 — E·K·S와 병행 가능 | **C3 이전 완료 필수**(C3의 전제가 L4). L4는 C1과 한 구조물 |
| ~~게이트 D~~ | **폐지** — EnTT/Flecs 채택 없음(사용자 결정) | 대량 개체 수요는 자가 스토어 확장으로 |

### 트랙 G — 지혈: 신규 결함 수술 (재설계와 무관하게 지금 가치)

A0·P0의 후속편이다. 전환 대상 코드가 틀린 채로 이관되지 않게 먼저 고친다.

- ✅ **G1 — 파괴 단일점의 씨앗** (2026-08-16, `6a4df74e`): `GameObject::Destroy`가
  `EraseGUID`·`ScriptObjectRegistry::Unregister`를 지나게 배선(N-1·N-4). 임시
  배선 — E1에서 슬롯 해제 지점으로 수렴하며 자연 소멸. LiveCount 콘솔 명령은
  생략(회귀 세트 생명주기 검사로 갈음).
- ✅ **G2 — 프리팹 소유권 이중 해제 2건** (2026-08-16, `fd8ceb45`): 호출부
  `delete` 2건 제거(N-2·N-3), `CreatePrefab`/`Load*` 반환의 비소유를 선언부에
  명시, PrefabEditor::Close가 저장 후 포인터를 다시 잡지 않게. 반환 타입 강제
  (관찰 포인터 별칭)는 트랙 P 재설계와 묶어 후속.
- ✅ **G3 — Scene 소유 통일** (2026-08-16, `42365615`, **부분 후퇴**): CreateScene
  경로 delete 추가로 세 전환 경로 대칭 완성(N-5). `unique_ptr<Scene>` 전환은
  PrefabEditor가 raw `Scene*`를 `m_scenes`에 직접 넣고 스스로 지우는 구조라
  폭발 반경이 커서 **트랙 E5와 묶어 진행**으로 후퇴(실측 근거 커밋 메시지).
- ✅ **G4 — 소잔챙이 일괄** (2026-08-16, `6a4df74e`·`42365615`): DDOL 태그→레이어
  복붙(N-11) · SaveScene return(N-12) · 순환 가드 이식(N-7 — par 병렬이라
  방문집합을 루트 호출 스택에 격리, S-2 정본 API 수렴까지의 방어) · Instantiate
  활성 씬 하드코딩(N-9) · instanceID 재할당 GUID 고아(N-10).
- ✅ **G5 — 죽은 코드 제거** (2026-08-16, `6a4df74e`): nested `GameObject::Type` ·
  `StaticGameObjectType` · `m_inverseMatrix`(+`GetInverseMatrix`) ·
  `GetComponentByTypeID` · `gameObjNode` — 전부 참조 0 재확인 후 삭제.

### 트랙 E — Entity 정체성 (구 A1·A2 승계, 정정 반영)

- ✅ **E1 — EntityHandle + 진짜 슬롯맵** (2026-08-16, `6dbc8c11`):
  `EntityHandle.h`+`Resolve`/`HandleOf`, `AllocateSlot`/`ReleaseSlot` 단일점,
  재인덱싱 폐지, 루트 폴백 제거(+가려져 있던 무가드 역참조 9곳 널 가드).
  **발굴**: 기존 재인덱싱은 로드 시 파일 인덱스↔슬롯 위치 어긋남(실측 — 14개 씬
  완전 반전, 본 60여 개)을 첫 파괴 때 몰래 수선하던 패스이기도 했다 — 폐지하자
  계층이 뒤엉켜 회귀 2건(스택 오버플로·타임아웃)으로 드러났고, **로더 배치
  리매핑**(파일인덱스→슬롯 맵으로 parent/children/rootIndex 일괄 리매핑, 루트
  children은 자식들의 최종 부모 기준 재구성)으로 해소. §6 "폴백이 버그를 숨긴다 —
  발굴이지 회귀가 아니다"의 실증 사례. 유보: G1 배선(GUID·관리 무효화)의 슬롯
  해제 지점 이동은 E4(레지스트리 통합)에서 — 지금 옮기면 유효 구간 의미가 변한다.
  잔여 관찰: LoadScene(비-즉시)류의 DDOL 루프가 이전 씬에 슬롯을 할당하는 기존
  동작, DDOL 섹션의 SetDontDestroyOnLoad가 리매핑 전 파일 스킴으로 서브트리를
  타는 기존 동작(둘 다 이전부터 있던 것 — E5·P2에서 재방문).
- ✅ **E2 — 계층 필드 수렴** (2026-08-16, `74b33316`): 전수 재인벤토리 실측
  (쓰기 19곳/6곳 — §1.3 하한 경고 적중), Attach/Detach/Clear/Set 정본 API 신설·
  전 지점 전환(조건부 erase_if 1곳만 API 부재로 잔존). AttachChildIndex가 중복
  검사를 정본화. **필드 봉인은 유보** — 읽기 158회/55파일, Dynamic_CPP 게임
  스크립트 ~40파일이 직접 읽어 private화가 편집 권한 밖 레이어를 깨는 실측 근거.
  봉인은 S1(스토어 이관)에서 재방문.
- ✅ **E3 — 조회 수렴** (2026-08-16, `73ba93b7`): 공개 시그니처 9종 유지, 내부
  중복 본문 8벌 → 씬 인자 단일 구현 4벌 위임. SceneObjectAt 무검사 제거, 매직
  0 → `kSceneRootIndex`+`Scene::GetRootObject()`. INVALID 부호 축소(int←uint32
  max)는 관찰 기록만 — E6 즈음 별도 판단.
- ✅ **E4 — 레지스트리 정제** (2026-08-16, `b9dec885`, **완전 통합은 기각 확정**):
  역방향 map 소멸, Unregister를 GameObject::Destroy 한 곳으로 정본화(G1 배선
  회수 — N-4 구조 차단), 배치·ABI·C# 무변경. **"핸들 값까지 통합"은 의미론
  지도로 기각**: 씬 전환마다 NotifySceneUnload 창에서 SweepOrphans가 DDOL 포함
  전 Behaviour의 IsAlive를 실검사하는데, 그 시점 DDOL은 슬롯이 해제된 상태
  (E1의 Detach)라 씬 세대 판정으로 옮기면 산 스크립트가 뜯긴다 — 매 전환마다
  밟는 실경로. DDOL 저장소가 자기 세대를 갖는 재설계(E5) 후에만 재평가.
  mutex는 정적 추적상 제거 가능성 높으나 동시성 실측 전 보수 유지.
- ⬜ **E5 — shared_ptr 축소**: 소유는 슬롯 `unique_ptr` 하나. 보관성 참조 99곳/
  23파일을 핸들로. `enable_shared_from_this` 제거. DDOL은 재등록 방식(구계획 A2
  결정 승계 — 핸들 무효화가 명시적이라 "옛 인덱스 유령"이 사라진다).

  ✅ **E5-0 — `EntityHandle`에 씬 식별자 도입** (2026-08-19): 핸들이
  `{sceneId, index, generation}`이 됐다. `Scene`마다 생성 시 단조 일련번호를
  받고(`RenderEngine/Skeleton.h`의 `m_serial` 선례 — 목록 인덱스는 씬 삭제 시
  재사용돼 ABA), `HandleOf`가 채우고 `Resolve`가 검사한다. `operator==`에도 들어가
  **"다른 씬의 같은 슬롯"이 구조적으로 차단**된다. `m_sceneId`는 `const`이고
  `Scene`은 `std::mutex`·`std::future` 멤버로 이미 복사·이동 불가라 번호 중복 경로가
  없다. 직렬화 무관(어떤 `reflect()`에도 안 실림) → 골든 재기준선 불필요. 회귀 12/12.
  → 이로써 `PrefabUtility::InstanceRef`가 `Scene*`를 따로 드는 우회가 원리적으로
  불필요해졌다(정리는 별도).

  ✅ **E5-R2 — `Scene::Canvases`/`CanvasMap` 핸들 전환** (2026-08-19, **커밋 대기**):
  분류 A(씬 소속 목록)를 `std::vector<EntityHandle>` ·
  `unordered_map<std::string, EntityHandle>`로 바꿨다. 목표는 성능이 아니라
  **fail-closed** — DDOL 이송(`DetachGameObjectHierarchy`)은 오브젝트를 살려 둔 채
  슬롯만 놓으므로 `weak_ptr`의 `.lock()`이 계속 성공했고, `RemoveCanvas`는
  `UIManager::DeleteCanvas`(진짜 파괴)에서만 불려 **떠난 캔버스가 유령으로 남았다**.
  핸들이면 `ReleaseSlot`의 세대 증가와 `Resolve`의 sceneId·세대 검사가 그 자리에서
  거른다.
  - `FindCanvasName`/`FindCanvasIndex`가 해석까지 끝낸 `Entity*`를 반환(옛 `shared_ptr`).
  - **쓰기 창구를 `Scene::AddCanvas` 하나로 모았다.** 전에는 `UIManager::MakeCanvas`와
    `UIManager::AddCanvas`가 각자 써 넣었고 **키가 갈려 있었다** — 전자는 *요청한*
    이름(`name.data()`), 후자는 *확정된* 이름(`canvas->ToString()`). `CreateGameObject`가
    충돌로 개명하면 그 캔버스는 자기 이름으로 찾히지 않는다. 확정 이름이 정본이다.
    (호출자 0이던 `Scene::AddCanvas`가 이로써 정본 창구가 됐다.)
  - 부수로 닫은 선행 결함 셋: ① `FindCanvasName`이 `string_view::data()`를 키로 써
    널 종단이 아닌 뷰에서 범위 밖을 읽었다 ② `RemoveCanvas`가 `Canvas*`를 널 검사
    없이 역참조 ③ `UIManager::Update`도 같은 형태. **핸들 유효성과 컴포넌트 존재는
    별개 검사**라는 규약을 세 곳 모두에 적용했다.

  ⚠ **게이트를 완주시키지 못했다 — 그 사유가 기록이다.** 이 전환의 관측 가능한 델타는
  "DDOL 이송된 캔버스가 옛 씬 목록에서 사라지는가" 하나인데, **DDOL을 태울 CLI가 없다**
  (`DetachGameObjectHierarchy` 호출자는 `SceneManager`의 씬 로드 경로 2곳뿐). 통과하는
  검사를 새로 만들면 전·후가 똑같이 통과해 자가 눈을 감는다 — `prefab_roundtrip`이
  자식 없는 probe로 중첩을 못 본 것과 같은 실패 양식이다. 게이트 신설은 DDOL CLI가
  선 뒤로 미룬다.

  ★ **다음(미착수)**: 분류 D(`UIManager::CurCanvas`/`SelectUI`)는 "DDOL 오브젝트가
  선택된 채 씬을 건너는" 시나리오가 미확인이라 그대로 뒀다. 분류 B/C는 아래 표대로
  차단 상태다.

  ★ **사용자 확정 설계 방향** (2026-08-19): UI를 별도 DOM/객체 체계로 분리하지
  않는다 — UI도 일반 GameObject이고 컴포넌트 조합만 다르다. 별도 `UIHandle`을
  만들지 않고 세대 기반 GameObject 핸들을 재사용한다. **핸들은 수명을 소유하지
  않는다**(Scene이 소유, 시스템은 안전 접근만). **핸들 유효성과 컴포넌트 존재는
  별개 검사**다 — 저장은 핸들로, 사용은 `Resolve` → `TryGetComponent<T>()`.
  Canvas는 자식을 소유하지 않고 목록은 **소유가 아니라 캐시**이며 이벤트를 놓쳐도
  핸들 검증으로 안전하게 무시돼야 한다. 렌더러는 **프레임 스냅샷만** 소비한다.

  ⛔ **E5-a(`weak_ptr` 8필드 일괄 전환) — 그대로는 성립하지 않는다** (2026-08-19,
  두 차례 조사). 근본 이유가 하나로 좁혀졌다:

  > **`weak_ptr`은 정체성 기반**(제어블록으로 C++ 객체 자체를 추적)이라 DDOL 씬
  > 전이로 슬롯이 바뀌어도 안 끊긴다. **`EntityHandle`은 슬롯 기반**이라 그 순간
  > `ReleaseSlot`이 세대를 올리고 새 씬이 새 슬롯을 배정하는데, **캐시된 옛 핸들을
  > 갱신할 지점이 코드 어디에도 없다** — `DetachGameObjectHierarchy`는
  > `OnRemovingFromScene`만, `AttachExistingGameObject`는 `OnAddedToScene`만 부른다.

  즉 씬 식별자를 넣어도(E5-0) 이 문제는 안 풀린다. 필드별 판정:

  | 분류 | 필드 | 판정 |
  |---|---|---|
  | A — 씬 소속 목록 | `Scene::Canvases`·`CanvasMap` | **전환 가능·오히려 개선**. "이 씬에 속한 캔버스"가 옳은 의미라 DDOL로 떠나면 무효인 게 정답이다. 지금은 `RemoveCanvas`가 진짜 파괴에서만 불려 **떠난 캔버스가 유령으로 남고 `.lock()`이 성공한다** — 핸들이면 fail-closed로 이 결함이 고쳐진다 |
  | B — 계층 내부 관계 | `Canvas::UIObjs` · `UIComponent::navigation` · `m_ownerCanvasObject` | **차단.** DDOL 전이가 자식까지 통째로 재배정하는데 재동기화 훅이 없다 |
  | C — 전역 싱글턴 | `AIManager::m_aiComponentMap` | **차단.** 등록이 `Initialize`/`OnUninitializing`(평생 1회)에 걸려 있어 DDOL 전이를 안 탄다 — 씬 스코프 핸들이 아니라 **전역 식별자**가 필요한 자리 |
  | D — 선택 상태 | `UIManager::CurCanvas`·`SelectUI` | **조건부.** A와 같은 논거가 유력하나 "DDOL 오브젝트가 선택된 채 씬을 건너는" 시나리오 미확인 |

  **선행 조건**: B를 열려면 **DDOL 전이 시 계층 내부 핸들을 일괄 재동기화하는 훅**이
  신설돼야 한다. `m_ownerCanvasObject`는 추가로 `Scene::Resolve`에 락(또는 락-프리
  세대 검증)이 필요하다 — 지금은 `m_generations`/`m_SceneObjects`를 잠금 없이
  인덱싱한다.

  ⛔ **E5-d(`m_SceneObjects` → `unique_ptr`)는 8필드 전환으로 열리지 않는다.**
  `shared_ptr<GameObject>`/`shared_from_this` 사용처 **146건 · 45파일**이고, 절대다수가
  8필드가 아니라 **`Scene`/`UIManager`의 공개 API 시그니처**에서 나온다
  (`CreateGameObject`·`GetGameObject`·`AttachExistingGameObject`·`MakeCanvas` 등이
  전부 `shared_ptr<GameObject>`를 주고받고, 엔진·에디터·게임 스크립트가 그것을 직접
  소비한다). 8필드 전환이 지우는 것은 그중 `shared_from_this()` 십여 건뿐이다.
  → E5-d는 **공개 API 전체를 핸들 반환으로 재설계하는 별도 트랙**이다.

  ⚠ **미해결(실행 차단)**: 워커 UI 푸시 파이프라인(`Scene.cpp`의 `WorkerPools`
  3태스크 + `PushUIRenderData`/`GetUIRenderDataBuffer`)이 헌법 위반(워커가 GameObject
  상태를 락 없이 읽음)이면서 동시에 **실소비자가 없어 보인다**(유일 소비자가 로그
  문자열의 `.size()`뿐, `PushUIRenderQueue`는 호출자 0). 그런데 **바로 그 자리 주석이
  "UI 쪽만 남는다 — GetUIRenderDataBuffer에 실소비자가 있다"**고 적는다(RenderSceneViewPlan ③
  정리 때 명시적으로 남긴 결정). 이 모순을 풀기 전에는 삭제하지 않는다.

  ※ 참고로 **1단계(`CommitRenderProxies`)는 이미 헌법을 지키고 있다** — 게임 스레드에서
  동기로 돌며(그 자리 주석이 "스레드풀에 넣지 않는다"고 사유까지 적는다) 값을 불변
  struct(`ProxyCommand::ImageUpdate` 등)로 굳혀 스레드 안전 큐로 넘긴다. 즉 "스냅샷
  경계를 새로 만든다"가 아니라 **이미 있는 것을 지키고 죽은 2단계를 걷는** 일이다.

  ⛔ **(옛 판정) E5-a — 타입 변경 없이 주석만** (2026-08-19 1차 조사). "보관 참조를
  핸들로"라는 E5의 전제가 **이 필드들에는 성립하지 않는다.** `weak_ptr<GameObject>`
  멤버는 **8종**이고(계획서의 "99곳/23파일"과는 다른 축의 수치다):
  `Canvas::UIObjs` · `UIComponent::navigation`/`m_ownerCanvasObject` ·
  `UIManager::CurCanvas`/`SelectUI` · `AIManager::m_aiComponentMap` ·
  `Scene::Canvases`/`CanvasMap`. 전부 전환 불가이며 사유가 셋으로 갈린다:

  | 사유 | 대상 | 근거 |
  |---|---|---|
  | **스레드 안전성** | `m_ownerCanvasObject` | 렌더 프록시가 워커 스레드에서 읽고 `.lock()`의 원자성에 의존한다. `Scene::Resolve`는 `m_generations`/`m_SceneObjects`를 **락 없이** 인덱싱한다 — 핸들로 바꾸면 그 안전성이 사라진다 |
  | **DDOL 씬 전이** | `UIObjs` · `navigation` | `DetachGameObjectHierarchy`/`AttachExistingGameObjectHierarchy`가 계층을 새 씬 슬롯에 재배정한다(index·generation 전부 교체). 캐시된 `EntityHandle`은 재동기화 지점이 없어 그 순간 무효화되거나 **새 씬의 엉뚱한 슬롯을 오조준**한다. `navigation`은 `DeserializeNavi()`가 최초 1회 래치라 재동기화 지점조차 없다 |
  | **씬 비종속 싱글턴** | `CurCanvas` · `SelectUI` · `m_aiComponentMap` | `UIManager`/`AIManager`는 `Scene*`을 소유하지 않는 전역 싱글턴이다. `AIManager`의 `obj->m_ownerScene != GetActiveScene()` 필터가 **맵이 여러 씬의 오브젝트를 동시에 담는다**는 직접 증거다. `EntityHandle`은 씬 스코프라 이 전제를 깬다 |

  ★ **`EntityHandle`은 씬 스코프다.** 같은 index+generation이 씬마다 다른 오브젝트를
  가리킬 수 있다 — `PrefabUtility::InstanceRef`가 이미 그 이유로 `{Scene*, 핸들}`
  쌍으로 저장한다. 이 8필드를 안전하게 전환하려면 그 쌍 + `ForgetScene` 콜백
  인프라가 필요하고, 그건 "정리"가 아니라 별도 설계 작업이다.
  → 타입은 그대로 두고 **각 필드에 "왜 핸들로 못 바꾸는지"를 주석으로 남겼다.**
  그 근거가 곧 계약이다.

  잔여 관찰(범위 밖, 기록): ① `UIManager::CheckInput`의 `break`가 널 검사 블록 **밖**에
  있어 첫 항목이 죽은 weak_ptr이면 그 프레임에 나머지 `UIObjs`를 검사하지 않고 끝난다.
  ② `AIManager.h`/`.cpp`가 CP949 인코딩이라 편집 시 주의가 필요하다. ③ `Dynamic_CPP`
  게임플레이 스크립트에도 `weak_ptr<GameObject>` 필드가 8개 더 있다(컴파일 대상 아님).
- ✅ **E6 — GameObject → Entity 심볼 리네임** (사용자 결정 2026-08-16, **K1-b
  이후에만**) — **사용자가 IDE에서 직접 수행**(2026-08-19, 99파일). §5 읽기 별칭만
  이쪽에서 마감(아래 ⚠): 이름이 디스크 정본인 동안 리네임하면 §1.1의 "리네임 시 컴포넌트
  소실"과 같은 병을 오브젝트 노드에서 재현한다 — K1-b(영속 UUID)가 선 뒤 E6가
  그 첫 소비자이자 실증이 된다. C++은 `using GameObject = Entity;` 전환 별칭 →
  일괄 치환 → 별칭 제거 3단계, C#은 전환기 별칭(`[Obsolete]` 위임 또는 global
  using) 후 348개 스크립트 일괄 치환. 리플렉션·씬 파일의 구 클래스명은 읽기
  별칭으로 흡수(§5). `Behaviour.GameObject` 프로퍼티도 `Entity`로, 구명은
  `[Obsolete]` 위임.

  ✅ **읽기 별칭(§5) 마감 + 그 자리에서 드러난 선행 결함** (2026-08-19, **커밋 대기**).
  리네임 직후 저작 자산이 하나도 열리지 않았다 — 회귀 세트가 **9/12**로 떨어졌고
  (`트랜스폼 값 왕복`·`리플렉션 골든`·`생명주기 순서`), 씬 로드가 널을 돌려주면서
  검사들이 **기본 씬 3개를 재고 있었다**. 원인이 둘로 갈렸다:

  | | 내용 |
  |---|---|
  | ① 별칭 부재 | `RegisterReflectManual.h`는 `X(Entity)`를 등록하는데 디스크 노드 키는 `- GameObject: <typeID>` 그대로다. `Find("GameObject")`가 널 |
  | ② **`ExtractTypeFromYAML`의 순서가 거꾸로였다** | 이름 판정보다 **값 변환이 먼저**라, 헤더가 안 맞으면 루프가 다음 항목으로 내려가 **평범한 데이터 필드의 값을 typeID로 읽으려 들었고** yaml-cpp가 던졌다. 그 예외가 `SceneManager::LoadScene`의 catch까지 올라가 **씬 전체 로드를 널로 끝냈다** |

  ★ **②가 증상의 위치를 속였다.** 오류는 `line 3, column 13` — `m_name: Test1`을
  가리킨다. 진짜 원인은 2행의 `- GameObject:` 헤더다. 이름 판정을 값 변환 앞으로
  옮기고 변환을 폴백 오버로드(`as<size_t>(0)`)로 바꿔 닫았다. **E6와 무관한 선행
  결함이다** — 등록되지 않은 타입 헤더를 가진 노드는 어떤 사유에서든 씬 전체를
  날렸다.
  → 별칭은 `Meta::ResolveRenamedTypeName`(`ReflectionYml.h`) 표 하나. 새 이름으로
  재저장되는 순간 표를 안 지나므로 자연히 치유된다. 리네임을 지나온 이름은 typeID가
  어긋나는 것이 **정상**이므로(ID가 이름의 FNV-1a) 그 자리의 불일치 경고는 억제한다 —
  오브젝트마다 한 줄씩 나오면 진짜 불일치가 묻힌다.
  → 리플렉션 골든은 **재기준선**했다. 정규화 후 diff가 `GameObject:`→`Entity:` 세 줄과
  그로 인한 알파벳 순서 이동뿐임을 확인하고 떴다(그 외 0줄).
  **회귀 12/12 복귀** — 트랜스폼 해시 `f593139644a26cf1`(재설계 전과 동일) ·
  생명주기 **93 사건 순서 동일**.
- ⬜ **E7 — GameObjectType enum 소멸** (사용자 결정 2026-08-16, K1-a·S3 이후):
  엔티티의 본성은 타입 enum이 아니라 **컴포넌트 조합**이다(UE: 빈 엔티티는 순수
  컨테이너, 타입 개념 없음). 실사용 51곳/19파일(그중 Dynamic_CPP 레거시 6) —
  판정 분기는 K1-a 타입마스크로(`HasComponent<Canvas>()` 등), UI 분기
  (`Scene.cpp:1806-1813` 등)는 공간 컴포넌트 종류(S3의 Transform/RectTransform
  상호배타)로 대체. 생성자의 타입별 컴포넌트 부착 분기(`GameObject.cpp:36-58`)는
  "컴포넌트 셋을 받는 생성"으로 — 프리팹(트랙 P)이 그 정식 공급자다. 직렬화된
  `m_gameObjectType` 필드는 읽되 컴포넌트 구성 검증용으로만 쓰고 저장은 중단
  (읽기 호환 — §5).

  ★ **착수해 보니 한 슬라이스가 아니라 셋이었다** (2026-08-19 재측정).
  실사용은 **61곳/14파일**이고(위 "51곳/19파일"은 오측), 그 61곳이 성격으로 갈린다:
  - **생성 시 인자 ~40곳** — "이런 걸 만들어 줘"라는 팩토리 파라미터다. enum으로
    남아도 무해하고, 저장할 이유가 없을 뿐이다. 제거 대상이 아니다.
  - **저장된 상태를 읽는 판정 5곳** — 이것이 E7의 진짜 대상이다.

  - ✅ **E7-a — 판정 3곳을 컴포넌트 질의로** (2026-08-19): 기즈모의
    `GameObjectType::Light`/`::Camera` 검사 둘은 **바로 다음 줄에서 이미
    `GetComponent<LightComponent>()`/`<CameraComponent>()`를 조회하고 있어 순수
    중복**이었다 — 걷어내니 코드가 짧아졌다. 배타(`else if`)도 함께 풀었다
    (컴포넌트가 정본이면 둘 다 가진 오브젝트에 둘 다 그리는 것이 옳다).
    `Scene::UpdateModelRecursive`의 UI 분기는 `HasTransform()` 선판정으로 교체 —
    S3가 "UI는 Transform 없음"을 만들어 둔 덕에 같은 사실을 두 곳에 적어 두지
    않게 됐다. Canvas는 둘 다 가지므로 안 걸리고 `default`로 가 월드 공간
    캔버스가 보존된다. 직렬화 무관 → 회귀 10개 전체 통과.
  - ✅ **E7-b — `Bone` 판정** (2026-08-19): `BoneComponent` 마커 신설(필드 0 ·
    MethodOnly — 빈 스키마는 `adapt<T>()`의 static_assert가 막는다), 생성 경로
    (`ModelSceneBridge` 2곳)·구파일 승격(`PromoteLegacyBone`을 `PromoteLegacyTransform`
    안에서 위임 호출 → SceneManager 3곳 + Prefab 1곳을 한 번에 덮음) 배선, 순회
    판정을 `GetComponent<BoneComponent>()`로 교체. Test1.creator에서 마커 **61/61**,
    저장본에 블록 61개, 회귀 10/10.

    **적대적 검토가 뒤집은 판단**: 두 구현 레인 모두 "`RegisterReflectManual.h`
    등록 누락은 로그 스팸일 뿐"이라 했으나 **틀렸다** — 미등록이면 ① 오브젝트
    복제(`Object::Instantiate`, 승격을 의도적으로 건너뛴다)와 ② 프리팹 "인스턴스에
    적용"(`ApplyComponentDiff`, `kept[]` 표시가 안 돼 살아있는 컴포넌트를 파괴)
    두 경로에서 마커가 **조용히 소실**된다. 회귀 게이트 어느 것도 못 잡는다
    (`reflect.golden`은 **등록된** 타입만 순회하므로 미등록은 diff에 안 뜬다).

    **캐시의 ABA를 일련번호로 차단**: 인덱스 캐시를 `Skeleton*` 포인터 동일성으로
    검증하면 해제 후 같은 주소 재할당 시 거짓 적중해 다른 모델의 뼈 인덱스를 조용히
    재사용한다. `Skeleton::m_serial`(생성마다 증가)로 비교해 원천 차단.

    ★ **성능 목표는 달성하지 못했고, 그 실패가 이 슬라이스의 최대 수확이다.**
    A/B(`scene.bonecache 0|1`)가 차이를 안 보여 자를 의심하고 관문별 계수를 넣었더니:
    분기 도달 **61/61**, 스켈레톤 뼈 수 **62**, 그런데 **인덱스 해석 0개** —
    `Skeleton::FindBone`이 61개를 전부 못 찾는다. 씬의 뼈 이름은 하이픈
    (`Bip01-Pelvis`)인데 `FindBone`은 모델에서 온 원본 이름과 완전 일치 선형 탐색이다.
    nullptr이 정상 경로라 로그 한 줄 없이 `GetLocalMatrix()` 폴백으로 빠진다.
    → "뼈 744개 × 문자열 선형 탐색 = 성능 축"이라던 전제가 성립하지 않았다.
    E7-b가 만든 회귀가 아니라(옛 `case Bone:`도 같이 실패했다) 선행 결함이며,
    별건 과제로 분리. 캐시는 정확하고 비용이 없어 존치 — 이름이 고쳐지는 순간
    값을 낸다. 무엇보다 이제 그 실패가 계측에 보인다.
  - ⬜ **E7-c — 필드 제거**: `m_gameObjectType`을 `reflect()`에서 빼면 자산
    218개의 형상이 바뀐다(골든 재기준선 + 승격 경로 — S1-b와 같은 규모).

  ★ **E7-c는 `UISystemRedesignPlan` U7에 묶여 있다** — 이 계획서에 빠져 있던 의존.
  `Prefab.cpp`의 `if (type != GameObjectType::UI)`는 UI만 instanceID 재발급을
  건너뛰게 하는데, Navigation이 `navObject`를 instanceID **참조**로 저장하기
  때문이다. 지금 걷어내면 "같은 프리팹을 두 번 배치했을 때만" 깨지던 것이
  "인스턴스화할 때마다 매번" 깨진다 — 그 자리 주석이 이미 그렇게 적어 뒀다.
  U7의 "Navigation instanceID → 프리팹-로컬 인덱스 재계산"이 선행이다.

  ★ **`UISystemRedesignPlan`은 이 계획에 맞춰 개정되어야 한다** (2026-08-19 기입).
  그 문서는 2026-08-16 작성이고, 그 뒤 이 트랙이 착지시킨 것들이 그 전제를 여럿
  무효화했다. E7-c를 U7에 그냥 얹으면 **이미 사라진 축 위에 설계가 서 있게 된다**.
  개정이 필요한 지점(실측):

  | UI 계획의 서술 | 지금 사실 | 근거 |
  |---|---|---|
  | `UIElement`가 `Awake`에서 rect 캐시·캔버스 연결(§3, §4 표) | **`Awake`는 없다** — L3이 레거시 훅 3종과 브리지를 철거했다. 6단계(`OnInitialized`/`OnAddedToScene`/…)로 다시 써야 한다 | 트랙 L·L3 |
  | "UIButton은 Awake/OnDestroy가 없어 등록 자체가 안 됨"(결함 목록) | **이미 해소** — C3에서 UIButton이 `OnAddedToScene`/`OnRemovingFromScene`으로 `UITickSystem`에 등록된다 | 트랙 C·C3 |
  | 위젯이 매 프레임 스스로 도는 것을 전제한 틱 설계 | **틱 축이 Component에 없다** — `Update`/`LateUpdate`/`FixedUpdate` 가상이 사라졌고 `UITickSystem`이 조밀 배열로 전담한다 | 트랙 C·C3 완결 |
  | UI 판정에 `GameObjectType::UI`·`Scene.cpp:1806-1813` 인용 | **그 분기는 없다** — S3로 UI는 `RectTransformComponent`만 갖고 Canvas만 둘 다 가지며, 순회는 `HasTransform()`으로 가른다. 인용된 줄 번호도 무효 | S3·E7-a |
  | YAML 변환 도구가 **"타입명 치환"** 기반(U7, §6) | **이름은 정본이 아니다** — K1-b가 컴포넌트 영속 UUID(`m_typeUUID`)를 세웠다. 이름 기반 치환은 그 규약을 우회하며, "리네임 시 컴포넌트 소실"을 다시 부른다 | 트랙 K·K1-b |

  즉 U7은 **E7-c의 선행이면서 동시에 이 트랙의 결과를 흡수해야 하는 문서**다.
  개정 없이 착수하면 두 계획이 서로 다른 생명주기·틱·타입 판정을 전제하게 된다.

  ★ **`BoneComponent`의 계약이 PHASE 13 S3.6에서 한 번 더 바뀐다** (2026-08-19 기입).
  주관은 `AnimationSchedulerPlan`(§2.2④ 관측 본 물질화 · §3 S3.6)이지만 **대상이
  E7-b의 산출물**이라 이 트랙에도 남긴다. 착수 전 양쪽을 함께 읽을 것.

  | | 지금 (E7-b 착지분) | S3.6 이후 |
  |---|---|---|
  | 뼈 GameObject의 Transform | **포즈 저장소** — `Scene::UpdateModelRecursive`가 뼈마다 매 프레임 기록 | **읽기 전용 투영면** — 관측되는 뼈에만 기록 |
  | 씬에서의 존재·계층 | 있음 | **그대로 있음** (유니티 노선 유지) |
  | `m_boneIndex`·`m_resolvedSerial` | 순회 판정의 캐시 | **투영 경로의 주소 지정** — 그대로 쓰인다 |

  **"관측된다"의 정의**(구조 변경 시에만 재계산): ① 뼈가 아닌 자식 보유(무기·
  이펙트·콜라이더) ② 소켓이 걸림 ③ 에디터 선택·기즈모 대상 ④ `m_bPinned` 명시 고정.
  비관측 뼈를 누가 읽으면 `BoneComponent::GetWorldTransform()`이 포즈에서 조상
  사슬만 타고 온디맨드 FK(**O(깊이)**)로 답하고 그 뼈를 관측 집합으로 **자동
  승격**한다 — grep이 놓친 소비자가 조용히 깨지지 않고 스스로 드러나는 구조다.

  ★ **폐기가 아니라 완성이다.** E7-b가 세운 것(마커 · 인덱스 캐시 · 일련번호
  ABA 차단 · 승격 경로) 중 되돌리는 것은 하나도 없다. S3.6이 바꾸는 것은
  **"매 프레임 쓰느냐"** 하나뿐이다. 근거: 저작 자산에 Bone 노드 744개
  (Test1.creator 61 · 플레이어 프리팹마다 ~54) × `UpdateModelRecursive` 프레임당
  3회 순회 — `BoneComponent.h` 주석이 성능 축으로 지목한 그 수치이고, 유니티가
  *Optimize Game Objects*로 되사는 것과 같은 청구서다. 다만 그쪽은 옵트인
  탈출구이고 S3.6은 **기본 동작**이다.

  **의존 관계**: S3.6은 **E7-b 완료를 전제**로 하고(마커·캐시가 있어야 성립),
  **E7-c와는 독립**이다(`m_gameObjectType`과 무관 — 관측 판정은 컴포넌트 보유와
  구조로만 정해진다). 즉 E7-c의 골든 재기준선을 기다릴 필요가 없다.
- 게이트: `shared_ptr<GameObject>`·`GameObject::Index` 보관 잔존 수 래칫(99·94에서
  단조 감소). 새 코드의 Index 보관 금지(리뷰 항목). E7 이후 `GameObjectType::`
  잔존 수 래칫(51에서 단조 감소).

### 트랙 K — 컴포넌트 (구 K 승계, 변경 없음 — 즉시 착수 가능)

- ✅ **K0** (2026-08-16, `6a4df74e`) — `reserve(30)` 6줄 제거(생성자 3벌×2) +
  `GetComponent`의 임시 shared_ptr 제거(107곳 혜택) + AddComponent 중복 검사를
  맵 조회로. 빌드 그린·회귀 세트 전체 통과(생명주기 92 사건 순서 동일). 메모리
  측정 수치는 K2(구조 교체) 시점에 before/after로 묶어 첨부.
- ✅ **K1-a** (2026-08-16, `97402aa3`) — ComponentTypeIndex(순차, 직렬화 금지) +
  `m_componentTypeMask`(uint64, 비직렬화). `HasComponent` 마스크 한 줄화,
  `GetComponent` 조기 반환. 다중 부착은 "하나 이상 있음" 의미론.
- ✅ **K1-b** (2026-08-16, `97402aa3`) — 수동 리터럴 UUID 30종
  (`ComponentTypeUUID.h`) + 기동 중복·누락 즉시 검출. 직렬화 UUID 우선/이름 폴백
  (§5 예외 2). 통합 발굴: 중복 검출이 `RegisterAllComponents` 이중 호출(Scene
  선등록+Factory 재호출) 전제를 깨 기동 abort — 멱등 가드로 해소. UUID 데이터는
  레이어 제약(Utility→ScriptBinder 불가)으로 ScriptBinder에, 조회 창구만
  TypeTrait에.
- ✅ **K2 완결 — unique_ptr + SBO** (2026-08-17, `151808b2`·`f158be46`): 후퇴
  2건이 PHASE 18로 해제되어 재개(CT10이 벡터 메타 삭제, unique_alloc 신설).
  1차 재시도는 Animator의 enable_shared_from_this가 bad_weak_ptr 크래시로 차단
  (실측) — 선행 슬라이스로 AnimationJob 추적을 프레임-로컬 raw로 해체(불변식:
  NotifyAllAndWait 동기 완결이 파괴보다 먼저 — 적대 리뷰가 조기 return 구멍을
  잡아 수리, 'Component 파생 enable_shared_from_this 금지' static_assert로 재발
  차단). 이후 m_components = InlineVector<Managed::UniquePtr<Component>, 4> 완성.
  측정: sizeof(GameObject) +24B ↔ 컴포넌트 1~4개 재할당 스톰 소멸.
- ⛔ **K2 스테이지 B(SBO) 폐기 — InlineVector 은퇴** (2026-08-17): 스테이지 A
  (unique_ptr)는 존치, 스테이지 B만 되돌렸다. `m_components`는
  `std::vector<Managed::UniquePtr<Component>>`로 복귀하고 `InlineVector.h`와
  그 리플렉션 특수화(`TypeTrait.h`의 `is_vector_v`/`VectorElementType` 짝)를
  삭제했다. 근거 넷:
  1. **이 컨테이너는 틱 경로에 없다.** 프레임당 컴포넌트 순회는 SystemSchedule의
     평탄한 `vector<Component*>` 3벌이고, `m_components`의 프레임당 유일한 독자는
     `Scene::DestroyComponents`의 정리 스윕이다. SBO가 없앤 비용(스폰 시 힙 할당)은
     이 컨테이너의 지배 비용이 아니었다.
  2. **측정이 패리티였다** — 커밋이 스스로 "리플렉션 골든 perf 베이스라인 동등"으로
     기록했다. 개선된 것은 할당 횟수라는 대리 지표뿐이다.
  3. **근거 수치가 오측이었다** — 헤더에 박힌 "프리팹 89%가 0개"는 실측 36.2%다
     (씬 58.3%는 맞다). 정정하면 무게중심이 "모든 오브젝트"에서 "스폰 경로"로
     좁혀지는데 그 경로의 이득은 측정된 바 없다.
  4. **N을 현 프로젝트 에셋 분포로 정할 수 없다** — 장르가 바뀌면 분포가 통째로
     달라지고, N은 타입 레이아웃의 일부라 DLL/plugin ABI상 나중에 못 바꾼다.
     현재 에셋 통계는 이번 게임의 로컬 최적화 근거일 뿐 범용 엔진 설계 근거가
     아니다.
  대안 판정(상용 엔진 대조): Unreal의 `TSet<UActorComponent*>`는 순서를 보장하지
  않아 이 엔진의 계약(`FindComponentSlot` 첫 매치 · `AddComponentAllowMultiple` ·
  YAML 시퀀스 왕복 게이트)을 못 지킨다. Godot식 이름 키 맵은 컴포넌트를 타입으로만
  찾는 이 엔진에 소비자가 0이다. 따라서 **Unity식 순서 보존 동적 배열 + 기존
  `m_componentTypeMask` O(1) 기각**이 정본이다. 세 엔진 모두 프레임 순회가
  오브젝트별 컨테이너를 거치지 않는다는 점(Unreal `FTickFunction` 레지스트리 ·
  Unity behaviour 매니저 리스트)이 SystemSchedule과 일치하며, SBO가 이 컨테이너의
  축을 잘못 골랐다는 것을 뒷받침한다.
  ★ **SBO 적용 기준(재적용 시 넷 다 충족)**: ① 제거되는 힙 할당이 핫패스에서 실제
  지불되고 ② 인라인 버퍼가 비는 비율 × 크기 증가분이 그 이득보다 작고 ③ 객체가
  커져서 손해 보는 다른 순회가 없고 ④ before/after 측정치가 있다. `m_childrenIndices`는
  기각된 후보다 — 씬 루트·UI 캔버스의 fan-out이 수백~수천까지 가므로 소형 배열이
  아니고, 계층은 SceneGraph 중앙 저장(트랙 S)이 해결할 몫이다.
  검증: Debug x64 전체 솔루션 빌드 통과(Player.exe·Academy_4Q.exe 링크).
- **기존 결함 발굴(K2 diff 무관)**: DDOL 생존 Animator가 씬 전환
  시 AnimationJob 추적에서 영구 이탈(CleanUp 전체 삭제 + Awake 1회 게이트) —
  L1의 OnAddedToScene 훅이 자연 해법, 별도 작업.
- (승계 기록) 구 K2 1차분 (2026-08-16, `b08ec7e4`, 당시 부분 후퇴 2건):
  m_componentIds 맵 삭제 — 벡터 단일 정본 + FindComponentSlot(마스크 선판정+
  소배열 선형). 제거 의미론은 유지(즉시 삭제는 SystemSchedule raw 구독의 프레임
  끝 단일 해제와 충돌 — 실측 근거). **후퇴**: SBO는 리플렉션 (역)직렬화의
  std::vector 하드코딩 때문에([[Property]] 표면 유지 §5), unique_ptr는 할당이
  ManagedHeap 공유 전제 API(shared_alloc/CreateShared)를 거쳐 감사 전 보류 —
  둘 다 E5와 묶는다. 부수 정밀화: 다중 부착에서 하나 제거 후 GetComponent가
  생존 인스턴스를 정확히 반환. 잔여: GetComponent<T>(uint32)의 기존 경계 버그·
  RemoveComponentIndex 죽은 코드 → K3.
- ✅ **K3 — 죽은 함수 정리** (2026-08-18, `fa7a055c`에 포함): K2가 남긴 둘을 삭제.
  `RemoveComponentIndex`(호출처 0)와 `GetComponent<T>(uint32)`(호출처 0 — 인덱스
  범위를 안 보고 `!empty()`만 보던 경계 버그가 있었으나, 소비자가 없어 수리가
  아니라 제거가 답이다). C# 바인딩 영향 0(ScriptCore는 타입 기반 조회만 쓴다).
  **손대지 않은 0-호출처 4종**: `FindAttachedID`·`OwnerSceneFind`·
  `OwnerSceneFindAttachedID`·`SetStatic` — 앞 셋은 E3가 세운 대칭 Find 계열의
  일부라 하나만 빼면 표면이 비대칭이 되고, 넷 다 스크립트 공개 API다. 죽은
  내부 코드와 성격이 달라 별도 판단 사안으로 남긴다.
  검증: Debug x64 전체 솔루션 빌드 오류 0(Academy_4Q.exe·Player.exe 링크).

### 트랙 S — SceneGraph 스토어 + Transform 컴포넌트화 (구 B 승계 + 사용자 결정 2026-08-16 개정)

초안은 "Transform은 스토어 내장, 컴포넌트화 안 함"이었으나 사용자 결정으로
**컴포넌트화를 채택**했다(§2.2). 구조는 두 층이다 — **TransformComponent(저작
표면, 데이터 없음)** 가 **TransformStore(SoA, 데이터 정본)** 의 슬롯을 가리키는
핸들 뷰. 데이터가 컴포넌트 객체 안으로 들어가는 것이 아니다(§7).

**컴포넌트화로 얻는 이점과, 그것이 성립하도록 보완할 클래스**:

| 이점 | 성립 조건 (보완 대상 클래스) |
|---|---|
| 공간 데이터가 선택이 된다 — 순수 로직 엔티티(GameManager류)·UI 엔티티는 Transform 0개. 스토어 슬롯·dirty 순회 비용이 함께 빠진다 | `GameObject`: `m_transform` 값 멤버 제거(`GameObject.h:150`), 타입별 생성 경로에서 Transform 부착 여부 결정 |
| 조회·판정이 다른 컴포넌트와 동일 표면 — `GetComponent<Transform>()`·`HasComponent<Transform>()`이 특례 없이 성립, K1-a 타입마스크 1비트로 "공간을 가졌는가"를 묻는다 | `Transform` → `TransformComponent : Component`(스토어 핸들만 보유). 기존 메서드 표면(`SetPosition`/`GetWorldMatrix`…)은 전부 유지 — 호출처·리플렉션 불변 |
| 공간 컴포넌트의 상호배타가 타입으로 표현된다 — 엔티티당 Transform **또는** RectTransform 하나(UE "타입당 1개"의 자연 적용) | `RectTransformComponent`: TransformComponent와 같은 공간 컴포넌트 계열로 정리, `AddComponent`가 계열 중복을 거부 |
| 직렬화 통일 — GameObject 특례 필드가 아니라 컴포넌트 블록으로 저장. K1-b UUID·P1 오버라이드가 Transform에도 그대로 적용(현행은 프리팹 오버라이드가 Transform을 특례 처리해야 한다) | 로더: 구파일의 `m_transform` 필드를 TransformComponent로 승격해 읽는 호환 경로(§5 예외 4) |
| 트랙 L 생명주기와의 정합 — 6단계가 Transform에도 균일 적용(OnAddedToScene에서 스토어 슬롯 획득, OnRemovingFromScene에서 반납) | `Component::m_pTransform`(`Component.h:90` — 전 컴포넌트가 든 raw 포인터): 소유 엔티티의 TransformComponent 조회로 교체 — "없을 수 있음"이 타입에 드러난다 |
| C# 표면 정식화 — 현행 관리 측은 Transform을 GameObject의 값 멤버로 특례 취급(컴포넌트 아님). `GetComponent<Transform>()`이 정식 경로가 되고 `Behaviour.Transform` 편의 필드는 유지 | `ScriptCore`의 GameObject/Transform 바인딩: 특례 제거, NativeComponentTable 경로로 통일(핸들 struct 표면 불변) |

- ✅ **S1 1단계 — TransformStore(SoA) + Transform 뷰 전환** (2026-08-16,
  `766158bf`): 비직렬화 6필드(행렬 2·월드 성분 3·dirty)를 씬 소유 스토어로,
  슬롯맵과 수명 동기. [[Property]] 4종은 물리 멤버 유지(리플렉션이 pointer-to-
  member·offset 기반 — 하드 제약 실측). `m_transform` 값 멤버는 **뷰로 존치** —
  직접 접근 445곳(Dynamic_CPP 41파일, 접근자 호출 0건) 실측이 제거를 막는다.
  씬 미등록 오브젝트는 점유자 아이덴티티 검사+LocalFallback 자가 치유.
- ⬜ **S1-b — TransformComponent 컴포넌트화** → **S3와 병합 판정** (2026-08-18
  재측정). 원래 적힌 차단 사유(MetaGenerator가 "리플렉션 클래스 = 동일명 헤더 +
  vcxproj 등록"을 강제)는 **해소됐다** — PHASE 18로 그 툴이 은퇴해 vcxproj·props
  어디에도 참조가 없고 PreBuildEvent 자체가 없다(전수 grep). 그러나 착수하려고
  다시 재보니 **진짜 관문은 그게 아니었다**:
  1. **직렬화 형상** — `Component` 파생이 되는 순간 기반 필드 4종(Object의
     `m_name`·`m_instanceID`·`m_isEnabled` + Component의 `m_FileID`)이
     `m_transform` 블록에 함께 방출된다(프리팹의 기존 컴포넌트 블록이 그 넷을
     달고 있는 것이 증거). 현재 저작 자산 **218개**(씬 12·프리팹 206)의 해당
     블록은 4필드뿐이라 형상이 바뀌고 골든 diff가 깨진다. 구파일 승격 경로
     (§5 예외 4)가 **선행**이며 그것이 곧 S3에 묶인 작업이다.
  2. **`m_transform` 값 멤버 존치가 강제된다** — 직접 접근 실측 **428곳/76파일**
     (Dynamic_CPP 334곳). 스토리지를 `m_components`로 옮기면 전부 `->`로 바뀐다.
  3. 반면 **복사 시맨틱은 관문이 아니다** — `Object`가 복사를 삭제하지만
     Transform 전체를 대입·복사하는 지점은 실측 0곳이다(`ModelLoader`의
     `node->m_transform`은 동명의 Matrix 필드로 무관).
  ★ 결론: 데이터·직렬화를 그대로 둔 채 컴포넌트 표면만 얹으면 오브젝트당 힙
  할당 1회가 늘고 얻는 것은 조회 표면 통일뿐이다 — **K2 스테이지 B와 같은
  형태의 미측정 구조 추가**가 된다. 따라서 단독 슬라이스로 진행하지 않고
  **S3(공간 컴포넌트 상호배타 + 로더 승격)와 한 묶음**으로 착수한다.
  `Component::m_pTransform` 교체도 같은 묶음이다.
- ✅ **S2 — 정본 순회 API + dirty push/lazy pull** (2026-08-18): `UpdateModelRecursive`가
  깨끗한 노드의 `GetLocalMatrix`+곱셈+`SetAndDecomposeMatrix`를 통째로 건너뛴다
  (`LayoutUINode`가 이미 쓰던 `parentChanged` 관용구를 승계). 가드(인덱스·파괴
  표시·순환 방문·최대 깊이)는 `Scene::TryEnterTraversal`로 수렴 — `LayoutUINode`와
  공유. `GetComponentsInChildren`은 `Scene.h` 순환 금지로 별도 헬퍼(가드 3종은
  **원래 하나도 없었다** — 자산 손상으로 순환이 생기면 그대로 무한 재귀였다).
  `DetachGameObjectHierarchy`는 BFS+슬롯 해제라 형태가 달라 손대지 않았다.
  A/B 토글 `scene.dirtytraversal 0|1`과 벤치 `scene.traversalbench <N> <frames>` 신설.

  **측정 (Release x64 · 4회 반복 중앙값 · 프레임당 µs)**

  | 규모 | 시나리오 | 옛 경로 | dirty | 평균 | 최소 기준 |
  |---|---|---:|---:|---:|---:|
  | 1,000 | 전부 정지 | 322.5 | 255.8 | **-20.7%** | -11.7% |
  | 1,000 | 10% 이동 | 347.2 | 294.7 | **-15.1%** | -7.2% |
  | 10,000 | 전부 정지 | 4,759.6 | 4,058.8 | **-14.7%** | -14.3% |
  | 10,000 | 10% 이동 | 5,249.4 | 5,292.2 | +0.8% | +0.07%(동등) |

  ★ **측정이 두 번 방향을 바꿨다 — 기록해 둘 함정 둘.**
  1. **Debug로 재면 거짓말이 나온다.** 같은 조건(1,000 정지)이 Debug 9,704µs /
     Release 387µs로 **25배** 벌어지고, 개선 폭도 -14.8% vs -29.6%로 뒤집힌다.
     `_ITERATOR_DEBUG_LEVEL=2`가 방문집합 비용을 부풀려 절감을 덮는다
     (`ContainerLibraryDesign` §8이 예고한 축과 같은 것). **성능 판정은 Release로만.**
  2. **게이트가 아끼는 것보다 비쌌다.** 첫 구현은 `Transform::IsDirty()`/
     `ConsumeWorldChanged()`를 노드마다 불렀는데, 이 접근자들은 호출마다
     `ResolveStore()`(소유자→씬→`GetGameObjectRaw` 점유자 확인→스토어)를 돈다.
     그 결과 10,000·10% 이동이 옛 경로보다 **약 4% 느렸다**. 순회는 바로 위에서
     `m_SceneObjects[objIndex]`로 객체를 꺼냈으므로 점유자 확인이 이미 끝나 있다
     — 게이트와 스킵 경로를 슬롯 직독으로 바꾸자 퇴행이 사라지고(+0.07%, 잡음)
     정지 시나리오 이득도 -12.1%→-14.7%로 커졌다. `Transform.h` StoreSlot 주석이
     "트래버설 경로의 캐시(재해석 생략)는 S2 소관"이라고 미리 지목한 자리였다.

  ★ **다음 레버는 방문집합이다(S2 잔여).** 10,000 정지에서 decompose를 전부
  건너뛰고도 4,059µs가 남는다 — 순회의 지배 비용은 행렬 계산이 아니라 순회
  자체(노드당 `unordered_set` 삽입 + `shared_ptr` 역참조 + 재귀)다. 세대 스탬프
  배열로 방문집합을 없애는 안은 `std::execution::par`(루트 자식 단위 병렬)에서
  공유 상태가 되어 이번 회차에 보류했다. 스탬프를 스레드별로 나누거나 순회
  병렬 단위를 바꾸는 설계가 선행이다.

  ⛔ **S2 잔여 판정 — 하지 않는다** (2026-08-19 재조사). 위 "다음 레버" 서술이
  **자기 자신을 반증한다**: 4,059µs는 세 요인(해시셋 삽입 · `shared_ptr` 역참조 ·
  재귀 오버헤드)을 **뭉쳐서** 잰 값이고, 셋을 나눠 잰 데이터는 계획서에도 코드에도
  없다. 방문집합을 없애서 얻을 절감은 그중 알 수 없는 일부이지 4,059µs 전체가
  아니다. 게다가 **자가 실제 씬과 반대 극단을 재고 있다** — `scene.traversalbench`가
  합성하는 10,000개는 폭 10 BFS로 **단일 루트 아래 하나의 깊은 서브트리**라
  `std::execution::par`의 병렬 분기가 사실상 1개인데, 실제 씬은 `CreateGameObject`가
  부모 미지정 시 씬 루트를 부모로 삼는 기본값 탓에 루트 팬아웃이 넓을 개연성이
  크다. 브랜치마다 O(N) 배열을 새로 잡는 안은 **벤치에서 이기고 실제 씬에서 질**
  수 있다. 재개 조건: ① 순회 안에 세부 타이머를 넣어 방문집합 비용을 분리 측정,
  ② 벤치에 넓은 루트-팬아웃 위상을 추가. 그 둘 전에는 착수하지 않는다.
  (별건 소품: `TryEnterTraversal<GameObject::Index>`의 깊이초과 보고용
  `static bool reported`는 `par` 순회에서 동기화 없이 쓰여 이미 데이터 레이스다 —
  `std::atomic<bool>` 한 줄. 세대 스탬프와 무관하게 처리 가능.)

  **동작 변화 1건(범위 밖 스코프 크리프, 기록)**: `GetComponentsInChildren`이
  파괴 표시된 자식의 컴포넌트를 결과에서 제외하게 됐다. 더 옳지만 공개 API
  동작 변경이다 — 호출부 7곳은 전부 게임플레이 스크립트이고 파괴 진행 중에
  부르는 곳은 찾지 못했다.

  검증: Debug 전체 빌드 오류 0 · 회귀 세트 전체 통과(골든 diff 0 · 생명주기
  92사건 순서 동일 · BT 크로싱 0.99 · 프리팹 왕복 통과).
- ✅ **S1-b + S3 — Transform 컴포넌트화 + 공간 컴포넌트 상호배타** (2026-08-18,
  `00c3aa4c`·`2efe7432`·`c9bbdd56` + S3분). 계획대로 한 묶음으로 착수했고, 착수
  **전에** 자를 먼저 세웠다(0단계).

  **0단계 — 트랜스폼 값 왕복 검사 신설**(`00c3aa4c`): 역직렬화기는 모르는 키를
  조용히 무시하므로, `m_transform`이 스키마를 떠나면 승격 누락분의 값이 에러 없이
  사라진다. 그런데 기존 세트는 그걸 못 잡았다 — 프리팹 왕복은 **개수만** 보고
  값 대조는 UI 전용 authored_rects뿐이었다. `scene.transformdigest` + 저장·재로드
  대조 검사를 먼저 만들었다. **이 판단이 이번 회차에서 가장 값이 컸다**(아래).

  **1단계**(`2efe7432`): `Component::SetOwner`를 virtual로. Transform도 같은
  시그니처를 갖고 있어, 상속하면 이름 은닉이 되어 리플렉션 로드 경로(정적 타입
  `Component*`)만 다른 함수를 부른다 → `m_owner` 널 → `ResolveStore` 영구 실패 →
  파일에서 연 오브젝트만 LocalFallback으로 떨어진다(에러 없음). 상속 **전에** 막았다.

  **2단계**(`c9bbdd56`, 80파일): `Transform : Component` 승격, `m_components`로 이동,
  접근부 **533곳/80파일** 이행, 구파일 승격 로더 4곳. 형상 변경은 **한 번만**
  (둘로 쪼개면 218개 자산을 두 번 이행시킨다).

  **3단계 — S3**: `GameObjectType::UI`는 `RectTransformComponent`만 갖는다.
  부착 규칙을 `AttachSpatialComponent()` 한 곳으로 모았다(생성자 두 벌 복사가
  갈리는 것을 막는다). `UIButton`의 죽은 `GetWorldQuaternion()` 읽기를 명시 상수로.

  ★ **경계는 UI이지 Canvas가 아니다 — 실측으로 되돌린 결정.**
  처음엔 UI·Canvas를 함께 묶었는데 `Transform_()`의 널 폴백 로그가 **Canvas 5종**을
  찍었다. 추적하니 `canvasWorld = owner->Transform_().GetWorldMatrix()`를 읽는 곳이
  둘(UIProxyBridge·ProxyCommand) 있었고, `UpdateModelRecursive`의 특례는
  `case GameObjectType::UI:` **하나뿐**이라 Canvas는 `default:`로 월드 행렬이 실제
  계산된다. `CanvasRenderMode::WorldSpace`가 그 값을 쓴다. 저작 자산이 전부
  ScreenSpaceOverlay라 **회귀가 절대 못 잡았을** 결함이었다. Canvas는 rect(자식
  레이아웃 기준)와 Transform(월드 배치)을 둘 다 갖는 명시적 예외로 둔다.
  이득은 699→680개(97%)로 거의 유지.

  ★ **컴포넌트화가 들여온 새 상태: "Transform은 개별 제거 가능".** 값 멤버였을 때는
  "오브젝트가 살아 있으면 Transform도 있다"가 타입으로 보장됐다. 그 불변식이 런타임
  규약으로 내려앉으며 세 경로에서 동시에 문제였고, 셋 다 "Transform은 예외"를 코드에
  못 박아 닫았다 — ① `GameObject::Destroy()`의 전량 파괴 마크(씬 전환 중 실제 크래시,
  덤프 스택 확보) ② `PrefabUtility::ApplyComponentDiff`의 "소스에 없으면 지운다"
  ③ `Component::SetOwner`의 캐시 조회 순서(생성자가 `m_pTransformComponent =
  AddComponent<Transform>()`인데 그 AddComponent 안에서 SetOwner가 불린다).

  ★ **정적 분석 두 번, 런타임이 둘 다 정정했다.** 착수 전 조사는 "UI가 Transform을
  만지는 코드는 `UIButton.cpp:24` 하나뿐"이라 했지만 Canvas 경로 둘을 놓쳤다 —
  그 호출이 `Canvas* → GetOwner() → Transform_()`라는 간접 경로라 파일 이름으로도
  문맥으로도 안 걸린다. **참조를 돌려주는 접근자에 널 폴백+로그를 붙인 것이 그것을
  드러냈다.** 크래시로 두었다면 원인 규명에 훨씬 오래 걸렸을 것이다.

  **정정 — 이 슬라이스가 실제로 없앤 것**: 계획서 원문은 "UI마다 행렬 3+벡터 3이
  죽은 채 실림"이라 적었지만, 그 데이터는 `TransformStore`(오브젝트 인덱스 평행
  배열)에 있어 **컴포넌트를 안 붙여도 슬롯은 그대로 남는다**. 실제로 없앤 것은
  컴포넌트 객체(680 × ~68B ≈ 46KB)와 로드 시 힙 할당 680회다. 스토어까지 걷어내려면
  희소 스토어가 선행이며 별도 슬라이스다.

  **잔여**: 계열 중복을 `AddComponent`가 **타입으로 거부**하는 규칙은 미구현
  (지금은 생성자가 규칙을 지킬 뿐, 나중에 손으로 붙이면 막지 못한다). 앵커/피벗
  해석 시스템과 `UISystemRedesignPlan` 레이아웃 트랙 경계 조율도 그대로 남는다.
  `Transform_()`의 널 폴백은 S3 안정화 뒤 제거하고 널 검사를 호출부로 올린다.

  ✅ **계열 중복 — 구멍 자체를 막았다** (2026-08-19). "타입으로 거부"는 성립하지
  않는다는 것을 먼저 확인했다: `AddComponent<T>()`는 T만 알고 상호배타 판정에
  필요한 역할은 인스턴스의 런타임 값이며, **Canvas 예외(rect+Transform 둘 다)**
  자체가 그 값에 의존한다. 공용 진입점 `AddComponent(const Meta::Type&)`에 가드를
  두는 안도 기각했다 — 그 진입점은 디스크 로드도 공유해서 컴포넌트 로드 순서에
  따라 정상 Canvas가 거부될 수 있고, 판정 근거가 곧 사라질 `GetType()`에 묶인다.
  → **에디터 Add Component 목록에서 뺐다.** 원인이 거기였다: `ComponentFactory`의
  이름 규칙이 `"Component"` **부분 문자열**로 걸러 `RectTransformComponent`를
  목록에 실었고(반면 `Transform`은 다섯 규칙 어디에도 안 걸려 원래부터 빠져 있었다),
  `AddComponent`의 중복 검사는 **같은 타입만** 보므로 3D 오브젝트에 붙는 것을 못
  막는다. 붙으면 `UpdateUILayout`이 씬 루트부터 전체 트리를 훑으며 "RectTransform을
  가졌는가"만으로 UI를 가르기 때문에 그 오브젝트가 **매 프레임 UI 레이아웃 순회에
  끼어들어 자식에게 스크린 좌표계를 전파한다 — 로그도 에러도 없이.**
  `BoneComponent`도 같은 이유로 함께 뺐다(마커의 부착 주체는 `ModelSceneBridge`와
  구파일 승격이다).

  ⛔ **`Transform_()` 널 폴백 제거 — 미룬다** (2026-08-19). "S3 안정화 뒤"라는
  조건이 **아직 성립하지 않았다**는 반증을 코드에서 찾았다: 같은 날 작성된
  `CameraSystem.h`가 주석에 *"`HasTransform()` 방어를 추가하지 않는다 … 실제로 그런
  프리팹이 생기면 `Transform_()`의 폴백 로그가 그 사건을 그대로 지목한다 — 이
  시스템이 따로 막으면 오히려 그 신호를 지운다"* 라고 적고 가드를 **의도적으로
  뺐으며**, 실제로 `owner->Transform_().GetWorldPosition()`을 무가드로 부른다.
  지금 걷으면 그 자리가 널 역참조다. 재개 조건: ① 이 폴백을 믿고 자체 가드를 뺀
  자리 전수 확인(현재 `CameraSystem` 1곳), ② 그 자리에 명시 가드를 넣거나 위
  "구멍 봉쇄"로 조합 자체를 구조적으로 막은 뒤, ③ 폴백 로그가 실사용에서 일정 기간
  0건임을 재확인. 그 전에 거두면 이 저장소가 이미 배운 "전환에는 진단 장치를 함께"를
  스스로 어기는 것이다.

  검증: Debug 전체 빌드 오류 0 · 회귀 10개 전체 통과 · **트랜스폼 값 왕복 해시가
  변경 전과 동일**(f593139644a26cf1, 68객체) · UI 캔버스 2종 소환 씬에서 335개 중
  **326개가 Transform 없음** + 폴백 로그 0건(적극 확인).
- 🔶 **S4 — 소비자 직결·변경분 커밋** (2026-08-18, **고정 비용분만 완료**):
  측정이 이 항목의 전제를 둘로 갈랐다.

  **기준선 (Release, `scene.proxybench` 신설 · 컴포넌트 규모별 · 200프레임)**

  | 컴포넌트 | before 평균 | after 평균 | 차이 |
  |---:|---:|---:|---:|
  | 2 | 51.5 µs | 45.4 | -12% |
  | 52 | 95.4 | 63.0 | **-34%** |
  | 102 | 86.0 | 71.7 | -17% |
  | 202 | 193.6 | 108.8 | **-44%** |
  | 402 | 307.3 | 167.7 | **-45%** |

  최소값 기준 회귀선은 **고정 ~28µs + 컴포넌트당 ~0.19µs**였다.

  ★ **계획이 겨냥한 축과 실제 비용이 있는 축이 어긋나 있었다.**
  이 항목은 "변경분만 커밋"으로 **가변 비용**을 겨냥했는데, 가변분은 402개에서도
  프레임의 0.6%다. 정작 큰 쪽은 **규모와 무관한 고정 비용**(매 프레임 컴포넌트
  벡터 8개를 값으로 새로 만드는 것)이었고, 저작 씬 대부분(렌더 컴포넌트 150개
  미만)에서는 그 고정분이 가변분의 합보다 컸다. `ContainerLibraryDesign`의
  mimalloc 판정("빠른 구간과 실제 할당 구간이 어긋난다")과 같은 형태다.
  → 고정분을 멤버 스크래치 버퍼(`assign`, capacity 재사용)로 걷었다. 값 복사라는
  규약은 그대로이고 담기는 곳만 바뀌므로 **의미론적 위험이 0**이다.
  부수: `Scene::CommitRenderProxies()` 추출(벤치 진입점 + S4가 손볼 단일 자리).

  ⬜ **잔여 — dirty 게이트는 선행 조건이 있다.** 프록시는 트랜스폼만 담지 않는다:
  재질(`shared_ptr<Material>`)·`m_bitflag`·`isEnabled`·LOD가 함께 실리고, 그
  필드들은 public이라 인스펙터와 스크립트가 **직접 쓴다**(setter 훅이 없다).
  트랜스폼 dirty만으로 게이트를 걸면 재질 교체가 화면에 반영되지 않는데 **에러도
  로그도 없다** — 이 저장소가 반복해 겪은 조용한 유실 양식이다. 그래서 지금
  만들지 않는다. 선행은 그 필드들의 쓰기 창구를 setter로 모으는 것이고 별도
  슬라이스다. 착수 시 `scene.proxybench`가 그대로 A/B 자가 된다.

  ⛔ **잔여 판정 — 미룬다, 트리거를 명시한다** (2026-08-19 재조사). 위 "setter로
  모으면 된다"는 전제가 **실측으로 무너졌다**: `ImGuiDrawHelperMeshRenderer`가
  `ImGui::DragScalar`/`Checkbox`에 **필드 주소를 직접 넘긴다**
  (`&meshRenderer->m_bitflag`, `&meshRenderer->m_isEnableLOD`). ImGui가 메모리를
  직접 덮어쓰므로 setter를 아무리 만들어도 **인스펙터는 그것을 거치지 않는다** —
  필드를 private로 막지 않는 한 우회가 구조적으로 열려 있다. 리플렉션 경유 쓰기
  (역직렬화·프리팹 적용·제네릭 드로어는 pointer-to-member로 직접 쓴다)도 같다.
  즉 "setter N개 추가"가 아니라 **인스펙터 위젯 패턴을 get/set으로 바꾸거나
  리플렉션 쓰기 경로에 훅을 심는** 작업이다. 필드별 현황: `isEnabled`는
  `Component::SetEnabled` 단일 창구가 **이미 있고**(사실상 공짜), `m_Material`
  직접 대입 13곳, `m_bitflag` 5곳+ImGui 바인딩 1곳, LOD는 직접 대입 0곳이고
  **오직 ImGui 체크박스로만** 바뀐다. 이득 0.6%에 이 비용은 맞지 않는다.
  **재개 트리거**: 저작 씬의 렌더 컴포넌트가 상시 200~400개를 넘길 때(현재 실측
  기준 150개 미만). 그때 순서는 ① ImGui 포인터 바인딩·리플렉션 쓰기 우회를 먼저
  봉쇄 → ② `isEnabled`는 있는 것 재사용 → ③ 나머지 setter.

  검증: Debug 전체 빌드 오류 0 · 회귀 10개 전체 통과.
  단서: before는 1회, after는 2회 측정이다(공정한 n이 아니다). 다만 402 지점에서
  after 두 실행이 167.75/167.61로 0.1% 이내 일치해 그 지점의 재현성이 높고,
  before 307.3은 그 분산 밖이다 — 효과 크기가 관측 분산을 압도한다.
- C# 표면 변경(위 표 마지막 행)은 S1이 아니라 **트랙 L의 L2와 묶어** 진행한다 —
  관리 경계를 건드리는 변경을 한 창구로 모은다.

### 트랙 P — 프리팹 (구 P 승계 + P4 승격)

- ✅ **P0 — 지혈** (2026-08-10 완료, 승계). 왕복 검사·`prefab.status` 가동 중.
  G2가 P0의 소유권 전환을 마저 닫는다(이중 해제 2건은 P0의 사각이었다).
- ✅ **P1 — 오버라이드를 명시 데이터로** (2026-08-16, `df28a13f`):
  `vector<PrefabOverride>{컴포넌트타입, 프로퍼티명, 값YAML}` [[Property]] 부착
  (BTBuildNode 선례 관용구 — 코드생성기가 헤더당 [[Serializable]] 하나 제약이라
  별도 헤더). DeserializePrefab이 명시 목록 기반으로 교체(P-d 해소), 컴포넌트
  all-or-nothing 제거 — 항상 갱신+오버라이드 프로퍼티만 되먹임(P-e 해소, 기존보다
  세밀). `m_prefabOriginal`은 과도기 시딩 비교 기준으로 강등(P-b 완화 — 비면
  "오버라이드 없음"). 잔여: 에디터 프로퍼티 변경 시점의 정본 기록 배선(후속),
  동일 타입 다중 컴포넌트의 인스턴스 단위 식별(P3), Destroy 후 재생성(P-f→P3).
  검증: 빌드 그린·회귀 전체 통과·왕복 신규 실패 0.
- ✅ **P2 — 인스턴스 추적을 핸들로 + 왕복 복원** (2026-08-16, `23b12f46`):
  `{Scene*, EntityHandle}` 추적+세대 필터+ForgetScene, 로더 7개 호출부+DDOL
  재연결(배치 리매핑 이후 시점), 인스턴스 루트 판정을 P1 데이터 기반으로 재설계
  (parent==0 관습 폐기 — 재부모화에 견딘다), 죽은 주석 블록 제거. **왕복 검사
  엄격 승격 완료 — "재로드 후 등록 2" 완전 통과, P-a(8-10부터) 종결.** N-14는
  현상 유지 판정(Navigation instanceID 리매핑 부재 근거 — UISystemRedesignPlan
  C3·U7 소관). 잔여 관찰: 비동기 로드 중 씬 등록 전 좁은 창의 일시 누락 계산
  (크래시 아님·자연 해소·동기 경로 무관).
- ✅ **P3 — 갱신을 비파괴로** (2026-08-16, `67f7b445`): 타입+순번 차집합 패치 —
  유지 컴포넌트는 같은 인스턴스에 패치(포인터가 갱신을 건넌다), 제거/추가 타입만
  파괴/생성. 동일 타입 다중의 '저장값 전원 강제 적용' 값 오염 소멸.
  잔여: `PrefabOverride`에 순번(ordinal) 필드가 없어 오버라이드 제외 판정이
  타입명 단위 — 동일 타입 일부만 오버라이드 시 나머지의 동기화 누락 가능(값
  오염 아님, 왕복 검사 비가시). 순번 필드 추가는 P4 착수 시 함께 결정.
- ⬜ **P4 — 중첩 프리팹 (선택 → 핵심 승격)**: 프리팹 안의 프리팹 인스턴스를
  펼치지 않고 참조 노드(`{정의 GUID + 오버라이드}`)로 저장·복원. UE 3원칙의 세
  번째 기둥이라 승격한다 — 단 순서는 불변(P1의 오버라이드 모델이 자리잡은 뒤).
  베리언트(프리팹 상속)는 P4에서도 별도 결정으로 남긴다 — 에디터 UX가 얽힌다.

  ★ **"선택 기능"이 아니었다 — 이미 저작 자산에서 깨져 있었다** (2026-08-19 실측).
  `.prefab` 208개를 전수 스캔해 "루트와 다른 non-null guid를 가진 자식"을 세니
  **6건**이다: `Bridge` · `CriticaHitEffect` · `ItemSlotPrefab2`(2단) ·
  `TutorialObj`(같은 프리팹 3벌) · `TutorialObj3` · `UI_CanvasesBoss`(UI 중첩).
  즉 P4는 신규 기능이 아니라 **지금 조용히 뭉개지는 것을 고치는 일**이다.

  - ✅ **P4-a — 중첩 정체성·등록 정합성 + 게이트 신설** (2026-08-19):
    두 결함을 고쳤다. ① `InstantiateRecursive`가 `obj->m_prefabFileGuid = m_fileGuid;`
    를 **재귀 프레임마다** 실행해 루트부터 최말단까지 전부 바깥 프리팹의 guid로
    덮었다 — 중첩 프리팹의 정체성이 그 자리에서 소실된다. ② `RegisterInstance`가
    `if (parent == 0)` 가드로 **최외곽 루트에만** 불려 중첩 루트가 등록부에 잡히지
    않았다.
    → guid를 재귀에 물려주는 방식으로 바꿨다: 최상위는 자기 guid, 아니면 노드가
    이미 자기 guid를 갖고 있으면 **보존**, 없으면 부모의 확정값을 상속.
    중첩 루트의 자손이 바깥이 아니라 **그 중첩 루트의** guid를 물려받아야 하기
    때문이다(`ItemSlotPrefab2`의 2단 사례로 확인). 등록 판정은 P2의
    `ReconnectPrefabInstance`가 이미 쓰는 패턴(`부모 guid != 자기 guid`)을 재사용.

    ★ **결함이 살아남은 이유는 검사가 눈을 감고 있어서였다.** 프리팹 왕복 검사가
    쓰는 `BTProbe.prefab`은 **자식이 하나도 없다** — 다중 노드·중첩 프리팹을 단 한
    번도 태운 적이 없다. 그래서 게이트를 먼저 세웠다: 중첩을 가진 probe 자산
    2종(`NestedProbeParent`/`NestedProbeLeaf`) + 시나리오 + `prefab.objectguid`
    진단 명령(`std::printf` — `Debug->LogWarning`은 stdout에 안 간다).
    **음성 시험으로 확인**: 수정을 되돌리니 guid가 `...eee2`→`...eee1`로 덮이고
    등록 1개 — **두 결함 모두 정확히 잡혔다**. 되살리니 통과.
    회귀 **12/12 통과**(새 검사 포함), 골든 재기준선 불필요(직렬화 형식 무변경).

    부수: 새 명령을 `else if` 사슬에 넣었더니 **MSVC 블록 중첩 상한(C1061)**에
    걸려 컴파일이 깨졌다 — 그 사슬이 이미 한계에 닿아 있었다. `scene.bonecache`
    등이 쓰는 조기 디스패치 관례로 옮겨 해결. 앞으로 명령을 더할 때 같은 벽을 만난다.
  - ⬜ **P4-b — 직렬화 형상(참조 노드로 저장)**: `SerializeRecursive`가 아직 중첩을
    통째로 펼친다. 참조 노드로 바꾸는 것은 자산 형상 변경이라 별도 단계다 —
    §5 "예외 3"이 읽기-호환 규약을 이미 정해 뒀다(참조 노드 부재 = 평탄 데이터).
  - ⬜ **P4-c — `PrefabOverride` 순번 필드**: P3가 P4로 미뤄 둔 잔여(동일 타입
    컴포넌트가 여럿일 때 오버라이드 판정이 타입명 단위). 필드를 늘리면 골든
    재기준선이 따라오므로 P4-a/b 완료 후 별도 판단.

  ⚠ **함정(기록)**: `UpdateInstances`는 `m_childrenIndices`를 **전혀 순회하지 않는다**
  (grep 0건) — 인스턴스 루트 자신의 프로퍼티·컴포넌트만 패치한다. 중첩과 무관하게
  이미 있던 결함이고 P4-a가 악화시키지도 완화하지도 않았다.
- N-14(UI instanceID 미갱신)는 P2에서 함께 판정 — `UISystemRedesignPlan` C3와
  같은 결정에 묶인다.

### 트랙 C — 시스템 스케줄 (구 C 승계, L4와 합류)

- ✅ **C1** (2026-08-16, `f381dfb5` — L4와 한 구조물로 완료): SystemSchedule이
  구독 대상 관리(L4)와 실행 순서 관리(C1)를 함께 담는다.
- 🔶 **C2 — 구조 변경 지연 커밋** (2026-08-19 전면 재작성, **원 문구는 이미 된
  일을 하라고 적혀 있었다**):

  원문은 이랬다 — *"`m_pendingAwake/Start`가 이미 절반 — 파괴·부착까지 커맨드
  버퍼로 확장, 페이즈 경계 커밋."* 착수하려고 전수로 재보니 **셋 중 둘은 이미 돼
  있고 하나는 원리적으로 불가능**했다.

  | 원문의 가정 | 2026-08-19 실측 | 근거 |
  |---|---|---|
  | 컴포넌트 제거가 즉시 | **이미 지연** — `RemoveComponent<T>`·`RemoveComponentTypeID`는 `Destroy()` 마크만 하고 물리 삭제는 `Scene::DestroyComponents`(프레임 끝 압축 패스) | `GameObject.inl` 주석이 이유까지 적는다 — "지금 지우면 스케줄 리스트의 raw 포인터가 댕글링된다" |
  | 오브젝트 파괴가 즉시 | **이미 지연** — `Destroy()`는 마크, 실제 파괴는 `FlushPendingDestroy` 단일 지점 | `Scene.cpp` — "순회 중인 것이 죽는 상황이 표현 자체가 불가능해진다" |
  | 부착을 지연시킬 수 있다 | **불가능** — `GameObject::GameObject()` 자신이 `m_pTransformComponent = AddComponent<Transform>()`의 반환값을 **그 줄에서** 역참조한다. 임의 호출부가 아니라 모든 오브젝트가 태어나는 줄이다 | `GameObject.cpp` 생성자·`AttachSpatialComponent` |

  ★ **게임 로직이 프레임 한복판에 구조를 바꾸는 경로 자체가 사실상 없다.** C3가
  틱 가상함수를 걷어내 네이티브 컴포넌트가 스스로 도는 축이 사라졌고, C# 바인딩에는
  `AddComponent`/`RemoveComponent`/`SetParent`가 **아예 없다**(`ClrHost`·
  `ScriptCore/Native.cs` 전수 grep 0건). C#이 부를 수 있는 구조 변경은
  `Prefab.Instantiate()`와 `GameObject.Destroy()` 둘뿐이고, 후자는 마크만 하며
  전자는 `RegistryDrainAwakeAndStart`(swap 후 지역 사본 순회)와
  `BehaviourRegistry.AwakeNewlyCreated`(현재 순회 중인 `_active`를 건드리지 않고
  `_pendingAwake`만 소비)가 이미 격리한다. Dynamic_CPP의 `AddComponent` 호출들은
  컴파일 대상이 아니다.

  즉 **"커맨드 버퍼를 새로 만들 대상"은 없었다.** 있었던 것은 좁은 반례 하나였고,
  그 해법도 지연이 아니라 재할당에 안전한 순회였다.

  - ✅ **C2-1 — 인스펙터 `m_components` 재진입 UB** (2026-08-19): `InspectorWindow`가
    `m_components`를 range-for로 도는 한복판에서 지형 드로어가 **같은 오브젝트에**
    `AddComponent<FoliageComponent>()`를 부른다(`ImGuiDrawHelperTerrainComponent` —
    "Paint Foliage"를 열었는데 FoliageComponent가 없을 때). `push_back`이 커패시티를
    넘기면 벡터가 재할당되고 순회 중인 반복자가 무효화된 채 증가한다.
    **증상이 늦게 나타나는 이유까지 확인**: 드로어가 반환된 뒤 그 반복에서 `component`를
    더 쓰지 않아, 터지는 것은 반복자 증가 한 줄뿐이다.
    → 인덱스 순회로 교체. **커맨드 버퍼로 미루지 않은 이유**: 드로어가 반환값을 바로
    다음 줄에서 역참조한다(`foliage->GetFoliageTypes()`) — 지연시키면 그 참조가 깨진다.
    `GameObject::FindComponentSlot`이 같은 이유로 이미 쓰는 관용구를 재사용했다.
    회귀 10/10 통과(이 경로는 콘솔 하네스가 지나지 않으므로 무영향이 정상).
  - ✅ **C2-0 — 재진입 스트레스 하네스 재배선** (2026-08-19): `FireReentrancyStress(true)`
    (순회 중)를 부르는 자리가 **코드에 전무했다**. C3가 `RegistryTick`을 걷어내며
    발화점이 함께 사라졌고, `PumpReentrancyStress`의 호출부 셋은 전부 비순회 지점이었다 —
    즉 C2-1이 잡은 종류의 버그를 잡을 자가 없었다.

    ★ **먼저 확인한 것: 탐지기는 이미 있다.** `_ITERATOR_DEBUG_LEVEL=2`가 Debug에서
    살아 있다(0으로 낮추려던 시도가 PhysX 때문에 좌초 — `Directory.Build.props`).
    즉 range-for가 무효 반복자를 증가시키면 MSVC가 그 자리에서 assert한다.
    **문제는 탐지가 아니라 그 경로를 태우는 자가 없다는 것이었다** — 이 슬라이스는
    새 탐지기를 만드는 게 아니라 기존 탐지기 앞으로 코드를 끌고 오는 작업이다.
    (합성 패턴을 Scene.cpp에 재현하는 안은 기각했다 — 진짜 코드가 아니라 그 복제본을
    시험하므로 실제 사이트가 깨져도 통과한다.)

    ★ **감사 결론 — 지금은 3겹으로 막혀 있다(B안).** 시스템 9종 전부 range-for로
    자기 vector를 돌고 `Unregister*`는 그 vector에 swap-and-pop을 하지만, 루프 본문에서
    거기 닿는 경로가 없다: ① 파괴는 마크만 하고 실 `Unregister`는 프레임 끝
    `FlushPendingDestroy`에서만, ② 부착도 `PendingAwake` 큐를 거쳐 `RegistryDrainAwakeAndStart`
    에서만 `Register`에 닿고, ③ C# 틱(`TickScripts`)이 `GameLogic`(9종 시스템 순회)
    **뒤에만** 돌아 같은 프레임에 재진입할 물리적 경로가 없다. 컴포넌트 9종 중
    `OnEnable`/`OnDisable`을 오버라이드하는 것도 0개다.
    → **이 시험이 지키는 것은 "지금 안전한가"가 아니라 그 3겹이 앞으로도 유지되는가**다.
    누군가 `RemoveComponent`를 즉시 삭제로 바꾸거나, `TickScripts`를 `GameLogic` 앞으로
    옮기거나, 시스템이 자기 루프에서 C#을 동기 호출하게 하면 그 순간 이 시험이 잡는다.

    구현: `CameraSystem::Update`가 `std::function<void()>` 프로브를 받고 루프 **본문
    첫 반복**에서 부른다(시스템이 `Scene.h`를 새로 물지 않게 콜백 주입 — 9종 어느
    것도 Scene에 의존하지 않는다는 감사 사실을 지켰다). CameraSystem을 고른 근거는
    회귀 씬 4종 **전부에서 `m_cameras`가 비지 않고**(다른 6종은 0개라 시험이 조용히
    증발한다 — `PumpReentrancyStress` 주석이 기록한 바로 그 사고), 루프 본문이 순수
    필드 대입이라 재진입 신호가 다른 부작용과 안 섞이기 때문이다.
    `FixedUpdate`의 폴백은 **제거**했다 — `Physics()`가 `GameLogic()`보다 항상 먼저
    돌아 순회 중 지점이 한 번도 못 서고 매번 무장을 가로챘을 것이다.

    ★ **자를 함께 고쳤다.** `verify-asan-lifecycle.ps1`은 그동안 발화가 아니라
    **무장**을 세고 있었다 — 매칭하던 `"순회 한복판에서"`는 `lifecycle.stress`가 무장
    직후 찍는 콘솔 printf 문구이고, 정작 발화 로그는 `Debug->LogWarning`으로만 나가는데
    그 싱크는 인메모리·HTML뿐 **stdout에는 한 글자도 안 쓴다.** 주석에는 "발화했는지
    확인한다"고 적혀 있었으니 자가 재는 것과 이름이 어긋난 사례다(같은 날 고친
    `scene.traversalbench`의 "분기 도달" 라벨과 같은 종류). `FireReentrancyStress`가
    같은 줄을 stdout에도 내게 하고, 검사를 ① 실제 발화 ≥5회 ② **그중 순회 중 ≥1회**로
    바꿨다. "전부 순회 중"은 기대하지 않는다 — 앞선 파괴가 카메라를 마킹하면 다음
    프레임부터 폴백으로 떨어지는 것이 정상이다.

    검증: 회귀 10/10 통과 · ASan 재진입 **발화 6회(그중 순회 중 2회) · ASan 보고 0건**.
    생명주기 기준선 93 사건 불변(그 시나리오는 `reentrant` 모드를 안 부르므로
    `g_stressArmed`가 시종 false).
  - ✅ **C2-2 — 스크립트 부착의 이중 초기화 + 게이트 신설** (2026-08-19): 같은 결함이
    **두 곳**에 있었다 — `InspectorWindow::AttachManagedScript`와 콘솔 `script.add`.
    둘 다 `AddComponentAllowMultiple` 직후 `script->OnInitialized()`를 수동으로 부르는데
    `State_AwakeCalled` 비트는 `RegistryDrainAwakeAndStart` 안에서만 서므로, 이미 큐에
    든 `PendingAwake` 항목이 다음 프레임에 한 번 더 부른다.
    → `Api_Prefab_Instantiate`가 이미 쓰는 관용구(`scene->Awake()` 동기 재드레인)로
    교체. 에디터/콘솔 전용 우회를 새로 만들지 않고 런타임과 같은 경로를 탄다.

    ★ **원래 설계했던 게이트는 작동하지 않았다.** 계획은 "`OnInitialized` 트레이스가
    정확히 1회인지 본다"였는데, `LIFECYCLE_TRACE(Phase::Awake, ...)` 호출부가
    `Scene.cpp` 드레인 **한 곳뿐**이라 수동 호출에는 트레이스가 없다 — 결함이 있던
    옛 코드에서도 트레이스는 1건이다. 게다가 `ScriptComponent::OnInitialized`의
    `if (HasInstance()) return;` 가드가 정상 타입에서는 두 번째 호출을 완전히 무해한
    no-op으로 만들어 관측 가능한 부작용이 아예 없다.
    → **미등록 스크립트 타입**(`NoSuchScriptType_C2_2_Regression`)을 붙이는 것으로
    풀었다. `CreateBehaviour`가 항상 실패해 `HasInstance()`가 영원히 false이므로 가드가
    무력화되고, 호출될 때마다 실패 로그가 한 줄씩 남는다 — **그 줄 수가 곧 호출 횟수**다.

    ★ **게이트가 결함을 실제로 잡는 것을 확인했다**(음성 시험): 수정을 되돌리고 빌드해
    돌리니 **2줄 — 실패**, 되살리니 **1줄 — 통과**. 게이트를 만들었으면 그것이 결함
    앞에서 실패하는지 봐야 한다는 이 저장소의 관례 그대로다.
    `Tools/regression/run-all.ps1`에 11번째 검사로 등록.

    검증: 회귀 **11/11 통과**(새 검사 포함) · 생명주기 기준선 93 사건 불변
    (그 시나리오는 `script.add`를 부르지 않는다).

    잔여 관찰: `InspectorWindow::DrawManagedScripts`도 `OnInitialized()`를 직접 부르지만
    그쪽은 **이미 `State_AwakeCalled`가 선 기존 컴포넌트**(타입 없이 붙어 있다가 나중에
    콤보박스로 타입을 고르는 경우)를 다루는 자리라 드레인 큐에 안 걸린다 — 구조가 다른
    케이스이고 이번 결함의 세 번째 사이트가 아니다.
  ★ **범위 밖으로 명시**(유혹되면 여기부터 다시 읽을 것): `AddComponent<T>()`의 물리적
  `push_back` 지연(생성자가 즉시 역참조 — 위 표), `GameObject::Destroy()`의 즉시 부작용
  3종 지연(검증 시나리오 부재), 계층 API 커맨드 버퍼(도달 가능한 호출자 0),
  `ApplyComponentDiff` 통합(에디터 전용, 재진입 위험 없음 — 손대면 "왜 파괴 경로가
  둘인가"라는 새 질문만 생긴다), DDOL 훅 타이밍(씬전환 뮤텍스가 이미 격리).

  ★ **덤으로 발견**: `GameObject::RemoveComponent(const Meta::Type&)`는 **본문이 통째로
  비어 있다**(호출자 0건 — `InspectorWindow`의 유일한 호출은 `Component*` 인자라 템플릿
  오버로드로 해석된다). 누군가 "타입으로 지우기"를 기대하고 부르면 조용히 아무 일도
  안 일어난다. C2와 무관한 별도 정리 항목.
- ✅ **C3 (1차 — Animator)** (2026-08-18): `Animator::Update` 가상 오버라이드를
  제거하고 본문을 `AnimatorSystem`(조밀 `vector<Animator*>`)으로 옮겼다. 등록·해지는
  L1의 `OnAddedToScene`/`OnRemovingFromScene`. `LifecycleRegistry`의 마스크 감지가
  `&T::Update != &Component::Update`로 판정하므로 오버라이드 제거가 곧 암묵 구독
  해제다 — 이중 틱 없음(코드로 확인). `RegistryTick`이 공통으로 해 주던 가드
  (owner 없음·파괴 표시·비활성)는 시스템이 같은 순서로 직접 진다.
  호출 자리는 `RegistryTick(UpdateList)` **직후** — 근거는 실측이다: Animator를 가진
  프리팹 17개 전부에서 루트의 스크립트(`ModuleBehavior`)가 Animator를 가진 자식보다
  파일상 먼저 오고, `Prefab::InstantiateRecursive`가 "자기 컴포넌트 먼저 → 자식 재귀"로
  등록하므로 옛 update 리스트에서도 스크립트가 먼저였다. 다만 이제는 프리팹 구조와
  무관하게 "전 스크립트 → 전 Animator"가 결정론적으로 보장된다(옛 구조보다 강한 보장).

  ★ **적대적 검토가 잡은 CRITICAL — 즉시 파괴 경로의 훅 누락.**
  `PrefabUtility::ApplyComponentDiff`는 저장소에서 컴포넌트를 **즉시** 소멸시키는
  유일한 경로다(`GameObject::RemoveComponent`조차 마크만 하고 프레임 끝 압축에
  맡긴다 — 그 이유가 GameObject.inl 주석에 이미 적혀 있다). 그런데 `Destroy()` →
  `OnDestroy()` → `UnregisterComponent` → `reset()` 순서라 **`OnRemovingFromScene`을
  건너뛴다.** 프리팹 편집에서 Animator를 지우고 "인스턴스에 적용"하면 다음 프레임에
  해제된 객체를 틱하는 UAF였다. `Scene::FlushPendingDestroy`와 같은 순서
  (`OnEndSimulation` → `OnRemovingFromScene` → `OnDestroy`)로 맞춰 닫았다.
  ★ 교훈: **"컴포넌트를 즉시 소멸시키는 경로"는 이 저장소에 하나뿐이고, 그 하나가
  6단계 훅 계약에서 빠져 있었다.** 앞으로 L3로 훅 이관을 넓힐 때마다 이 경로를
  먼저 확인해야 한다 — 훅에서 자기를 떼는 시스템이 늘어날수록 위험이 비례해 커진다.

  C# 훅은 ScriptSystem 일괄 호출 하나 — 크로싱 계약 불변(회귀에서 0.99 확인).

- ✅ **C3 (2차 — 기준선 비영향 6종)** (2026-08-18): DecalComponent · FoliageComponent ·
  SpriteSheetComponent · TextComponent · SoundComponent · CharacterControllerComponent를
  전용 시스템 5개(Decal·Foliage·UITick·Sound·CharacterController)로 이관.

  ★ **대상 수가 "~29종"이 아니라 10종이었다** — 앞선 기록이 틀렸다. 틱 훅을 가진
  네이티브 컴포넌트를 전수로 세니 10종이고(Animator 완료분 제외), 그중 **4종은
  생명주기 기준선에 등장한다**: LightComponent(6)·CameraComponent(6)·
  ImageComponent(3)·Canvas(3) — 기준선의 Update 18건이 정확히 이 넷이다.
  그 넷을 시스템으로 빼면 `RegistryTick`이 더는 틱하지 않아 트레이스 위치가
  프레임 끝으로 밀린다. **사건 집합이 같아도 순서가 바뀐다.** 92사건 순서는
  PHASE 9부터 지켜 온 가장 오래된 불변식이라 재기준선은 별도 결정 사안이다.
  → 이번 회차는 **기준선에 안 걸리는 6종만** 이관했다(패턴 검증 후 게이트를
  건드리는 순서). 남은 4종은 §잔여.

  **배선 근거(옛 위치 보존)**: 여섯 다 `RegistryTick(UpdateList/LateUpdateList/
  FixedUpdateList)` 안에서 돌았다. 그래서 Update계는 두 번째 `AllUpdateWorldMatrix`
  (= `UpdateUILayout` 재실행) **이전**에, LateUpdate계는 `UpdateRenderData` **이전**에,
  CCT의 FixedUpdate는 `PhysicsManagers->Update` **이전**에 둔다. 마지막 것은 특히
  중요하다 — 옛 구현이 `Physics->AddInputMove`로 그 프레임의 이동 입력을 큐에 싣고
  바로 다음 물리 스텝이 같은 프레임에 소비했으므로, 순서가 뒤집히면 캐릭터 이동이
  한 프레임 밀린다.

  ★ **적대적 검토가 잡은 CRITICAL — 개명은 삭제보다 위험하다.**
  `Scene::InternalPauseUpdateForUI`(일시정지 전용 우회로)가 `textComponent->Update()`·
  `spriteSheetComponent->Update()`를 **타입 포인터로 직접** 부른다. `Update`를
  `TickLayout`으로 개명하자 그 호출이 **기반의 빈 가상 `Component::Update`에 조용히
  붙었다** — 컴파일 통과, 크래시 없음, 일시정지 중 UI 배치만 멈춘다. 함수를 아예
  지웠다면 컴파일 에러로 즉시 드러났을 것이다.
  세 레인이 다 놓친 이유가 구조적이다: 이 경로는 `RegistryTick`도 시스템도 아니고,
  게임이 멈추면 `GameLogic()` 전체를 건너뛰고 UI만 따로 살리려고 만든 곳이라
  "누가 이 컴포넌트를 틱하는가"를 정상 프레임 경로에서만 찾으면 걸리지 않는다.
  ★ **규칙**: 훅·접근자를 개명할 때는 **기반에 동명 멤버가 있는지 먼저 확인한다.**
  있으면 개명 대신 삭제하거나, 개명 후 옛 이름의 호출부를 전수 검색한다.
  (같은 계열: S1-b 1단계의 `SetOwner` 이름 은닉, 2단계의 역직렬화 미지 키 무시 —
   셋 다 "이름이 같지만 다른 것에 붙는" 형태이고 셋 다 컴파일러가 안 잡는다.)

  **지표**: `lifecycle.registry`의 암묵 구독 수. before를 못 쟀으므로(이전 바이너리
  부재) 직접 증거로 교차 검증했다 — UI 캔버스 프리팹은 `TextComponent` 34개 +
  `ImageComponent` 139개를 갖는데, 측정된 update 리스트는 **149**(=139 + Canvas·
  Camera·Light 등 10)로 **Text 34개가 리스트에 없다**. 이관이 발효됐다는 뜻이다.

  검증: Debug 전체 빌드 오류 0 · 회귀 10개 전체 통과 —
  **생명주기 92사건 순서 동일**(이 슬라이스의 절대 게이트) · 트랜스폼 값 왕복 해시
  f593139644a26cf1 유지 · 골든 diff 0 · BT 크로싱 1.00.

  ⬜ **잔여 (4종 — 재기준선 결정 필요)**: LightComponent · CameraComponent ·
  ImageComponent · Canvas. 이관하면 92사건의 **순서**가 바뀐다(집합은 동일).
  선택지 둘 — ① 시스템이 같은 `LIFECYCLE_TRACE`를 발화시키고 재기준선을 뜬다
  (diff가 그 넷의 Update 위치 이동뿐임을 증거로 남긴다) ② 기준선을 지키기 위해
  이 넷은 이관하지 않는다. **①이 맞다고 보지만 사용자 결정 사안이다** — 이
  기준선은 PHASE 9부터의 불변식이고, 재기준선은 리플렉션 골든 때와 달리
  "의도된 형상 변경"이 아니라 "실행 구조 변경"이라 성격이 다르다.
  ✅ 그 외: `DecalSystem::Update`의 `while (timer >= slicePerSeconds)`가
  `slicePerSeconds <= 0`이면 무한루프였다 — 원본에 있던 결함을 C3가 바이트 그대로
  이관한 것이다. `slicePerSeconds`는 저작값이고 0(기본값)이 그대로 들어오므로
  실경로다. 선행 가드로 닫았다(2026-08-19, **커밋 대기**).

- ✅ **C4 — `Scene`의 레거시 생명주기 이름 정리** (2026-08-19 기입·완료, **커밋 대기**):
  L3이 `Component`에서 레거시 훅(Awake/Start/OnDestroy)을 걷어냈는데 **`Scene`의
  동명 멤버는 그대로 남았다.** 그물에 안 걸린 이유는 `Scene`의 그것들이 가상
  함수도 오버라이드도 아니라서다 — 이름만 같고 계층이 다르다.

  실측한 현재 몸통:

  | `Scene::` | 실제로 하는 일 | 판정 |
  |---|---|---|
  | `Awake()` | `RegistryDrainAwakeAndStart()` 한 줄 — 씬이 깨어나는 게 아니라 **큐를 비운다**. **매 프레임 돈다** | 개명 |
  | `Start()` | **빈 함수** — 주석: "Awake 단계가 pendingStart까지 소진하므로 여기서 또 부르면 같은 프레임에 Start가 두 번 돈다" | 삭제 |
  | `OnEnable()` | **빈 함수** — 주석: "활성 전이는 `Component::SetEnabled`가 그 자리에서 처리한다(9-2)" | 삭제 |
  | `OnDisable()` | **빈 함수**(PROFILE 매크로만) | 삭제 |
  | `OnDestroy()` | `FlushPendingDestroy` + `DestroyLight/Components/GameObjects` + AI async 시작. **매 프레임 도는 프레임 끝 정리 패스다** | 개명 |
  | `FixedUpdate`/`Update`/`LateUpdate` | 프레임 페이즈 디스패치 | **유지** |

  ★ **틱 셋은 건드리지 않는다.** `Scene`은 Component가 아니라 **드라이버**이고,
  드라이버의 페이즈 이름이 프레임 페이즈를 따르는 것은 옳다. C3가 없앤 것은
  "컴포넌트가 스스로 도는 축"이지 프레임 페이즈가 아니다.

  ★ **`OnDestroy`가 특히 나쁘다 — 매 프레임 돈다.** 씬이 파괴될 때 도는 것이
  아니라 프레임 끝 정리 패스인데 이름이 "파괴 시"를 가리킨다. `Awake`도 같다
  (매 프레임 큐 드레인). **이름이 판단을 방해한 실례가 이미 있다**: C2-2의 권고
  해법이 `scene->Awake()` 동기 재호출인데, 이름만 보면 "씬을 다시 깨운다"로 읽혀
  위험해 보인다 — 실제로는 드레인이라 안전하다. `Api_Prefab_Instantiate`가 이미
  그렇게 쓰고 있다는 사실을 확인하고서야 안전이 납득됐다.

  제안: `Awake()` → `DrainPendingLifecycle()`, `OnDestroy()` → `EndFramePass()`
  (또는 `FlushFrameTeardown()`), 빈 셋은 호출부(`SceneManager`)와 함께 삭제.

  ✅ **이행 결과** (2026-08-19). 호출부 실측: `Awake` 5 · `OnDestroy` 7 ·
  `OnDisable` 7 · `OnEnable` 1 · `Start` 1.
  - `Awake()` → `DrainPendingLifecycle()`, `OnDestroy()` → `EndFramePass()`.
  - 빈 셋(`Start`·`OnEnable`·`OnDisable`) 삭제 — 호출부 9곳도 함께. `OnDisable`은
    `PROFILE_CPU` 매크로만 있어 아무것도 재지 않는 스팬 하나가 사라졌다.
  - `SceneManager::Awake`의 `Awake→OnEnable→Start` 3단이 **드레인 한 줄**이 됐다.
    뒤의 둘이 빈 함수였으므로 원래부터 3단이 아니었던 것이 이제 보인다.
  - 경계 주석도 함께: `ClrHost.h`의 "반드시 `scene->OnDestroy()` 뒤에서 부를 것"이
    `EndFramePass()`로.

  ★ **선행 확인 ②(93 사건 기준선)의 답**: 라벨은 호출부의 고정 enum이라 함수 이름과
  무관하다 — 개명·삭제 어느 쪽도 트레이스 내용을 바꾸지 못한다(L3 때와 같은 사유).
  다만 **이번에는 그 게이트를 돌려서 확인하지 못했다**: 같은 시점에 진행 중인 E6
  리네임이 씬 로드를 깨뜨려 `생명주기 순서`·`트랜스폼 값 왕복`·`리플렉션 골든` 셋이
  이미 빨간 상태였다(아래 ⚠). 대신 `verify-script-add-awake-once`가 개명된
  `DrainPendingLifecycle()` 경로를 실제로 태우고 통과한다.

  **선행·주의**: ① 빈 셋 삭제는 소품 크기(호출부만 정리)지만 개명 둘은 호출부
  수를 먼저 세야 한다. ② 생명주기 기준선 93 사건은 `LIFECYCLE_TRACE` 라벨이
  호출부의 **고정 enum**이라 함수 이름과 무관할 공산이 크지만(L3 때 같은 이유로
  기준선이 불변이었다) 착수 전 확인이 필요하다. ③ `Scene::Awake()`는 public이고
  `ClrHost`의 `Api_Prefab_Instantiate`가 부른다 — 개명은 그 경계도 함께 간다.

### 트랙 L — Component 중심 생명주기 (사용자 결정 2026-08-16 신설, 같은 날 전면 확장)

초안은 훅 교체를 기각했으나 사용자 결정으로 채택·확장됐다. 이 트랙의 본질은
리네임이 아니라 **생명주기의 기준점 이동**이다(§2.1 "생명주기 관점의 전환"):
"스크립트가 오브젝트와 함께 태어나 Awake→Update→OnDestroy를 도는" Unity·Actor식
구조에서, **"컴포넌트가 씬 그래프의 phase에 참가하는"** 구조로 옮긴다. 수명 단위는
GameObject가 아니라 Component이고, **Verse가 UE Scene Graph에서 맡는 역할(씬
그래프의 저작·시뮬레이션 스크립팅 계층)은 우리 엔진에서 C#/CoreCLR이 맡는다**
(사용자 결정) — C#은 파사드 호환층이 아니라 이 생명주기의 1급 저작 언어다.

**ScenePhase 상태 기계** — 엔티티가 상태를 들고, 컴포넌트 훅은 상태 전이에서
발화한다. Existence(씬에 존재)와 Simulation(시뮬레이션 참가)은 별개 축이다:

```
             Attach                Scene 진입             Simulation 시작
  Detached ─────────→ Attached ─────────────→ InScene ─────────────────→ Simulating
      ↑                                          ↑                           │
      │              Scene 이탈                  │      Simulation 종료      │
      └── Detach ←── (OnRemovingFromScene) ←─────┴───── (OnEndSimulation) ←──┘
```

| 전이 | 발화 훅 |
|---|---|
| 부착(생성·역직렬화 직후) | `OnInitialized` |
| InScene 진입 (또는 이미 InScene인 엔티티에 부착) | `OnAddedToScene` |
| Simulating 진입 (또는 이미 Simulating인 엔티티에 부착) | `OnBeginSimulation` |
| Simulation 종료 | `OnEndSimulation` |
| 씬 이탈(파괴 전 단계·DDOL 이송 포함) | `OnRemovingFromScene` |
| 탈착·파괴 직전 | `OnUninitializing` |

**Phase catch-up이 API 규칙이다.** `AddComponent`는 엔티티의 현재 phase까지 새
컴포넌트를 자동 진입시킨다:

```cpp
void Entity::AddComponent(Component* c) {
    c->OnInitialized();
    if (phase >= ScenePhase::InScene)    c->OnAddedToScene();
    if (phase >= ScenePhase::Simulating) c->OnBeginSimulation();
}
```

"BeginPlay를 놓친 컴포넌트" 상태가 구조적으로 존재하지 않는다 — 시뮬레이션 중
PoisonComponent를 붙이는 종류의 런타임 조합 변경이 특례 없이 성립한다. **씨앗**:
현행 `m_pendingAwake/m_pendingStart` 드레인이 이 규칙의 절반을 이미 구현하고 있다
(플레이 중 생성된 컴포넌트도 Awake/Start를 받는다) — 큐 관례를 phase 비교로
정식화하는 것이지 무에서 만드는 것이 아니다.

**Tick은 opt-in이다.** 전 객체 가상 Update 순회가 아니라, OnBeginSimulation에서
**필요한 컴포넌트만** 스케줄러에 구독한다(L4). **시뮬레이션 태스크 스코프** —
Verse spawn/suspends의 C# 번안: 컴포넌트당 SimulationScope(취소 토큰)를 두고,
OnBeginSimulation에서 시작한 태스크·코루틴·이벤트 구독·틱 구독은 전부 스코프에
귀속되어 OnEndSimulation/OnRemovingFromScene에서 **일괄 해지**된다. `while(timer>3)`
폴링 대신 `await scope.Delay(3f)` 루프가 관용구가 된다. "구독만 있고 해지가 없는"
구조(N-4가 정확히 그 병이었다)가 생명주기 차원에서 막힌다.

OnEnable/OnDisable은 6단계 밖의 **활성 전이 보조 훅**으로 존치한다(PHASE 9-2의
전이 기반 호출·인스펙터 체크박스가 이 훅에 묶여 있다).

**매핑과 의미 변화** (단순 리네임이 아니다 — 분해 2건, 신설 1건):

| 현행 | 신규 | 의미 변화 |
|---|---|---|
| `Awake` | **분해**: `OnInitialized` + `OnAddedToScene` | 1회 보장 로직(현행 `State_AwakeCalled` 가드)은 OnInitialized로. OnAddedToScene은 가드 없이 **씬에 편입될 때마다** 발화 — DDOL이 씬을 건널 때 현행은 Awake 가드로 침묵시키지만(`Component.h:40-47` 주석이 그 사연), 신규는 OnRemovingFromScene → OnAddedToScene 쌍이 대칭으로 발화한다. "씬당 초기화"를 Awake에 넣고 DDOL에서 안 불려 곤란하던 종류의 코드가 갈 곳이 생긴다 |
| `Start` | `OnBeginSimulation` | 시뮬레이션(플레이) 시작 시 1회. 시뮬레이션 중 생성된 컴포넌트는 등록 직후(현행 `m_pendingStart` 드레인과 같은 자리) |
| (대응 없음) | `OnEndSimulation` **신설** | 현행은 플레이 종료가 OnDisable→OnDestroy로 뭉개진다. 종료 정리를 파괴 정리와 구분할 자리가 생긴다 |
| `OnDestroy` | **분해**: `OnRemovingFromScene` + `OnUninitializing` | 씬에서 빠지는 것(파괴 전 단계·DDOL 이송 포함)과 실제 파괴 직전을 구분. `Behaviour.cs:60-66`의 "Awake 없이 OnDestroy만 불리는" 방어는 "Initialized 없이 Uninitializing 없음" 계약으로 재정의 |
| `OnEnable`/`OnDisable` | 존치 (6단계 밖) | 활성 전이 훅. 변경 없음 |
| `FixedUpdate`/`Update`/`LateUpdate` (전원 가상 호출) | **opt-in 구독으로 전환** (L4) | 틱은 생명주기의 중심이 아니다. OnBeginSimulation에서 필요한 컴포넌트만 구독. **씨앗**: `LifecycleRegistry.h:50-57`의 override 감지(`if constexpr (&T::Update != &Component::Update)`)가 이미 "오버라이드한 타입만 등록"을 한다 — 이를 **암묵 구독**으로 재해석해 기존 코드 무비용 이관, 명시 구독 API를 정본으로 세운다. C# 348개 스크립트도 BehaviourGenerator의 오버라이드 감지로 암묵 구독 |

에디터/플레이 구분: OnInitialized·OnAddedToScene·OnRemovingFromScene·
OnUninitializing은 에디터·플레이 공통(현행 Awake·OnDestroy가 그렇듯),
OnBegin/EndSimulation은 시뮬레이션 전용 — 에디터 재생 진입(에디터 씬 사본 생성)·
종료(DeleteEditorOnlyPlayScene)와 정합.

- ✅ **L1 — ScenePhase 상태 기계 + 네이티브 훅 재편** (2026-08-16, `5ec29bcc`):
  6단계 훅+브리지(대응 셋은 옛 훅 호출, 신설 축 셋은 빈 기본 — 92 사건 기준선
  불변 실측), ScenePhase 보유, catch-up은 RegisterComponent 판정 확장으로,
  End/Removing은 FlushPendingDestroy 단일점 배선(핫패스 무비용), DDOL 이송
  Removing/Added 쌍 동기 발화, LifecycleRegistry 8→11비트. OnBeginSimulation
  발화는 pendingStart 드레인 위임(재생 진입 직접 호출은 Start 이중 발화).
  원안 세부는 아래 — 구현이 대체:
  (Detached/Attached/InScene/Simulating) 도입, `Component.h` 가상 함수 6단계 추가,
  `AddComponent` 계열 4경로가 전부 catch-up을 지나게(현행 `AttachComponentLifecycle`
  단일점이 그 자리 — `GameObject.h:50-55`의 "경로 넷을 한 곳으로" 원칙 재활용).
  `LifecycleRegistry` 마스크 확장(현행 8비트 → 6단계+enable/disable+틱3).
  전환기 브리지: 신규 훅의 기본 구현이 대응하는 옛 훅을 호출해 미이관 컴포넌트가
  깨지지 않게. Scene 디스패치 재편 — `m_pendingAwake/Start` 큐를 phase 비교로
  정식화, DDOL 이송 경로(`DetachGameObjectHierarchy`/`AttachExistingGameObjectHierarchy`)에
  Removing/Added 쌍 배선, 씬 언로드·재생 종료에 OnEndSimulation 배선. 에디터
  씬은 InScene까지만, 재생 진입(에디터 씬 사본)이 Simulating 전이가 된다 —
  Existence/Simulation 분리가 에디터/플레이 구분의 정식 표현이 된다.
- ✅ **L2 — C#을 씬 그래프의 저작 언어로** (2026-08-16, `c6f19246`): Behaviour
  6단계 훅+브리지(348개 스크립트 무변경), IsInitialized 리네임, SimulationScope
  (프레임 dt 결정적 Delay·구독 자동 해지·Cancel은 가상 체인 밖 무조건 선행),
  BehaviourRegistry TearDown이 네이티브와 동일 순서 발화. **크로싱 무추가**
  (현행 PIE는 재생 종료=실제 파괴라 기존 DestroyBehaviour 하나로 세 훅 합성,
  ScriptApiTable 무변경, 실측 0.98회/프레임). [Obsolete]는 L3에서.
  잔여: ① DDOL 이송의 Removing/Added가 ScriptComponent를 거쳐 C#까지는 전달되지
  않음(관측 차이 없음 — L3에서 결정) ② BehaviourGenerator의 LifecycleMethods
  제외 목록이 신규 훅 이름을 모름 — L3에서 실제 오버라이드 생기기 전 수정 필수.
  원안 세부는 아래 — 구현이 대체:
  `BehaviourRegistry` 디스패치 확장, 기존 Awake/Start/OnDestroy는 `[Obsolete]`
  별칭 + 폴백 브리지(신규 훅 미구현 스크립트는 옛 훅 호출 — 348개 스크립트가 한
  번에 안 깨진다). `IsAwakened` → `IsInitialized`. **SimulationScope 도입** —
  컴포넌트당 취소 토큰 + `scope.Delay`/`scope.Subscribe` 관용구, OnEndSimulation/
  OnRemovingFromScene에서 일괄 해지(Verse spawn/suspends의 C# 대응물 — 관리 측
  태스크는 경계를 넘지 않고 BehaviourRegistry 안에서 산다, 틱당 1회 크로싱 불변).
  ScriptApiTable 진입점 수가 변하면 Version 명시 버전업(§1.4). Transform 컴포넌트화의
  C# 특례 제거(트랙 S 표 마지막 행)도 이 슬라이스에 묶는다.
- ⬜ **L3 — 이관과 브리지 철거**: 네이티브 컴포넌트 ~30종 + 관리 스크립트의 훅
  리네임·분해 적용, 브리지·별칭 제거. 컴포넌트당/스크립트 묶음당 독립 커밋.
  선행 결정(2026-08-16): ① DDOL 이송의 Removing/Added 신호는 **C#까지 전달한다**
  — ScriptComponent가 두 훅을 오버라이드해 희귀 이벤트 크로싱으로 전달(틱당 1회
  계약은 틱 경로 한정이라 무저촉). SimulationScope의 구독 해지·재구독이 씬 이송과
  일관되려면 필요. ② BehaviourGenerator 제외 목록 갱신은 3차에서 선행 처리.
- ✅ **L4 — 틱 opt-in 스케줄러** (2026-08-16, `f381dfb5`, C1과 한 커밋):
  SystemSchedule 신설(페이즈 리스트 6종+드레인 순서 통합, 관측 불변 실측 —
  92 사건 동일), 명시 Subscribe 정본 개통, 암묵 편입은 SubscribeImplicit 재배선,
  암묵/명시 잔존 카운터+CLI 노출(래칫 기반). 현재 헤더 전용(vcxproj 무변경
  제약) — 추후 .cpp 분리 여지. 관리 측 opt-in·소비자 배선은 C3 소관.
- 판정: 빌드 그린 + pwsh 회귀 세트. **회귀 세트의 "생명주기 순서"(92 사건) 검사는
  순서가 의도적으로 바뀌므로 기준선 재작성이 각 슬라이스 판정에 포함된다** —
  기준선을 다시 뜨기 전의 실패는 회귀가 아니라 예정된 차이다. 신규 검사 둘:
  ① DDOL 씬 이송 시나리오(Removing/Added 쌍 발화) ② **phase catch-up 검사** —
  시뮬레이션 중 AddComponent가 Added→Begin을 순서대로 받는지, 스코프 해지가
  EndSimulation에서 실제로 일어나는지.

### ~~게이트 D~~ — 폐지 (사용자 결정 2026-08-16: EnTT/Flecs 채택 없음)

구계획의 "EnTT/Flecs 채택 평가 게이트"는 **폐지한다 — 외부 ECS 라이브러리 채택은
없다.** Flecs `IsA`와 트랙 P 오버라이드 모델의 비교 관찰도 종료한다. 근거는 사용자
결정이며 조사 결과와도 정합한다: UE 조사 결과 Scene Graph조차 Mass를 대체하지
않고(범용 오브젝트 모델과 대량 시뮬레이션 ECS는 역할이 다르다), 우리 씬 규모(228
오브젝트)에서 범용 전환은 비용>이득이 명백했다. 향후 대량 개체(군중·탄막류) 수요가
실제로 생기면 **자가 스토어의 확장**(SceneGraph 스토어와 같은 SoA 관례)으로 다룬다
— 그때도 외부 ECS를 들이는 결정으로 되돌아가지 않는다.

## 5. 직렬화·호환 전략

**파일 포맷 불변이 기본, 예외는 다섯 — 전부 읽기 호환.**

- 씬 YAML은 지금처럼 index/parentIndex(정수)+GUID 기록. 세대는 런타임 전용, 순차
  타입 인덱스(K1-a)는 **절대 저장 금지**(프로세스마다 달라도 되는 값).
- **예외 1 (P1)**: 인스턴스에 오버라이드 목록 필드 추가. 부재 = 오버라이드 없음.
- **예외 2 (K1-b)**: 컴포넌트 헤더 이름해시 → UUID. 부재 시 이름+숫자 폴백.
- **예외 3 (P4, 신설)**: 프리팹/씬 안의 중첩 프리팹 인스턴스가 참조 노드로 기록
  된다. 구버전 파일의 평탄화된 스냅샷은 그대로 읽힌다(참조 노드 부재 = 평탄 데이터).
- **예외 4 (S1, Transform 컴포넌트화)**: Transform이 GameObject 특례 필드
  (`m_transform` [[Property]])에서 컴포넌트 블록으로 이동한다. 로더는 구파일의
  `m_transform` 필드를 만나면 TransformComponent로 승격해 읽고(구파일 그대로
  열림), 저장할 때만 컴포넌트 블록으로 쓴다. UI 오브젝트의 구파일 `m_transform`은
  읽되 부착하지 않는다(S3의 상호배타 — 어차피 죽은 데이터였음이 실측돼 있다).
- **예외 5 (E6·E7)**: 심볼 리네임과 enum 소멸의 파일 흔적. 구파일의 클래스명
  노드(`GameObject`)는 리플렉션 읽기 별칭으로 흡수하고(K1-b UUID가 정본이 된
  뒤라 이름은 표시용일 뿐이다), `m_gameObjectType` 필드는 읽되 저장을 중단한다
  (부재 = 컴포넌트 조합으로 판정). 어느 쪽도 기존 파일을 깨지 않는다.
- 로드 시 "m_index == 벡터 위치" 불변식이 현재 관례로만 유지된다(로더가 YAML의
  m_index로 덮어씀 — `Scene.cpp:188-215` + `SceneManager.cpp:1119-1128`). E1에서
  로더가 이 불변식을 **검증**하게 한다 — 부분 편집·병합된 씬 파일이 조용히 깨지는
  경로를 시끄럽게 만든다.
- 관리 경계: 핸들 배치(uint32×2) 유지 시 C# 무변경. 배치가 변하면 ScriptApiTable
  Version 필드로 명시 버전업(무언의 어긋남 금지 — static_assert 관례 승계).
- 영속 참조는 계속 정체성(GUID) 경유 — 핸들을 파일에 적지 않는다(§1.4).
- 기존 206개 프리팹·씬 12개 일괄 변환 금지(승계) — 새 필드는 저장 시 자연 부착.

## 6. 함정 (승계 + 신규)

승계(전부 이 코드베이스에서 실제로 밟았던 것): 유니티 빌드 전이 include(전체 빌드가
판정 기준) · 워크트리 동시 커밋(커밋 전 HEAD 재확인, 래칫 `--update` 금지) ·
Scene.h↔GameObject.h 순환(핸들은 POD라 오히려 완화) · 소비자 없는 출력 = 미완성
패스(S-4) · 두 핸들 체계 공존기의 Index 보관 금지(래칫 감시) · DDOL(E5에서 재등록
방식으로 명시 처리) · 프리팹 회귀는 데이터 손상으로 나타난다(왕복 검사 필수) ·
"제거가 없어서 조용했던 불변식"이 K2에서 깨어난다(UnregisterComponent 동기를 같은
커밋에) · grep 범위는 데이터 파일 제외 전수 검색(206개 프리팹이 결과를 채운다).

신규:

- **파괴 경로는 늘 새로 생긴다.** Unregister 호출처가 1곳뿐이었던 것(N-4)은 누가
  게을렀던 게 아니라, "파괴 시 할 일 목록"을 호출자가 기억해야 하는 구조였기
  때문이다. E1 이후 새 파괴 경로를 만드는 코드는 반드시 슬롯 해제 단일점을 지나야
  하며, 우회 경로가 컴파일되지 않게 설계한다(멤버 접근 봉인 — A0-2의 Transform
  봉인과 같은 수법).
- **소유권은 주석이 아니라 타입으로.** N-2·N-3은 unique_ptr 전환(P0) 후에도
  호출부가 raw 포인터를 delete해서 났다 — 소유 전환 슬라이스는 반환 타입·명명까지
  포함해야 완료다.
- **UE 개념 직수입 주의.** UE 쪽에서 재부모화·재추가가 컴포넌트 상태와 얽혀
  버그·제약 보고가 이어지는 지점이다(§2.1 — 개별 사례 확인, 일반화는 미확정).
  우리는 재부모화가 생명주기를 건드리지 않는 현행 의미론을 유지한다.
  "타입당 1개" 제약도 스크립트에는 적용하지 않는다(§2.2 번안).
- **빅뱅 금지의 실증.** Epic조차 Experimental 1년+, Beta 연기, 프리팹 스키마
  breaking change를 겪었다. 트랙 하나를 건너뛰고 다음을 당기고 싶어질 때 이 사실을
  다시 읽는다.
- **폴백이 버그를 숨긴다.** `GetGameObject`의 루트 폴백(N-13)은 계층 오염을 몇 달
  숨겨 왔다. E1에서 무효 = nullptr로 바꾸면 숨어 있던 오염이 **드러나며 시끄러워질
  수 있다** — 그것이 정상이다. 전환 직후 회귀 세트가 새로 잡는 실패는 회귀가
  아니라 발굴일 가능성부터 본다.

## 7. 이 계획이 의도적으로 하지 않는 것

- **C# 스크립팅 표면의 구조 변경** — GetComponent/Instantiate 시그니처, 틱 3종,
  틱당 1회 크로싱, 블리터블 ABI는 유지. 단 **생명주기 훅 이름 집합은 예외다** —
  트랙 L이 UE 6단계로 교체한다(사용자 결정 2026-08-16, 초안의 "표면 불변" 원칙을
  이 범위에 한해 번복). 쿼리 기반 API 같은 표면 차별화는 여전히 코어 전환 후 별도
  결정.
- ~~GameObject → Entity 심볼 리네임~~ — **채택으로 전환**(사용자 결정 2026-08-16,
  트랙 E6). 단, 순서 제약이 있다: 클래스명이 리플렉션·씬 파일·C#에 박혀 있으므로
  **K1-b(영속 UUID)가 정본이 된 뒤에만** 안전하다 — 이름이 디스크 정본인 동안
  리네임하면 §1.1의 "리네임 시 컴포넌트 소실"과 같은 병을 오브젝트 노드에서 재현하게
  된다. E6가 K1-b의 첫 소비자이자 실증이 된다.
- **Verse 언어 도입·트랜잭션 메모리·분산 실행** — 언어는 들이지 않는다. Verse가
  UE에서 맡는 역할(씬 그래프의 저작·시뮬레이션 스크립팅 계층)은 C#/CoreCLR이
  담당한다(§2.2·트랙 L). STM·분산은 계속 비범위.
- **외부 ECS 라이브러리(EnTT/Flecs) 채택** — 전면이든 부분이든 없다(사용자 결정
  2026-08-16, 구계획의 D 게이트 폐지). 데이터 지향이 필요한 곳은 자가 스토어
  (SoA 관례)로 푼다 — UE도 아키타입 ECS를 범용 오브젝트 모델로 쓰지 않는다(§2.2).
- **TransformStore의 소유권까지 컴포넌트로 내리는 것** — 컴포넌트화(트랙 S)는
  저작 표면의 통일이지 데이터 이동이 아니다. 행렬·dirty·계층 데이터가 컴포넌트
  객체 안으로 들어가면 SoA·변경분 커밋(S-4)이 무너진다. 컴포넌트는 끝까지 스토어
  슬롯의 핸들 뷰다.
- **뼈 Transform의 포즈 정본 이동** — 뼈 GameObject가 포즈를 저장하느냐 비추기만
  하느냐는 **`AnimationSchedulerPlan` PHASE 13 S3.6 소관**이다(트랙 E 각주 참조).
  이 계획은 뼈가 씬 오브젝트라는 것(유니티 노선)과 `BoneComponent`가 그 마커라는
  것까지만 정하고, "매 프레임 쓰느냐"는 애니메이션 평가 엔진의 결정에 맡긴다 —
  포즈의 생산자가 거기이므로. 반대로 S3.6은 뼈를 씬에서 걷어내지 않는다(그 안은
  PHASE 13에서 명시 기각).
- **기존 에셋 일괄 변환** — 변환 스크립트는 그 자체가 데이터 손상 경로.
- **베리언트(프리팹 상속)** — P4의 중첩까지만. 에디터 UX 결정이 얽힌다.
- ~~GameObjectType enum 재설계~~ — **채택으로 전환**(사용자 결정 2026-08-16,
  트랙 E7): enum 소멸, 본성은 컴포넌트 조합으로. G5는 그 전에 죽은 쌍둥이 enum만
  먼저 지운다.
- **`SetParentID` 월드 보존 복원** — 죽은 계산이었음이 확인된 상태(A0-2). 되살리면
  기존 씬·프리팹이 틀어진다. 트랙 P 왕복 검증 기반이 선 뒤 별도 판단(승계).

## 8. 구계획 대체 매핑

| 구계획 (ObjectModelModernizationPlan) | 이 문서에서 |
|---|---|
| A0 결함 수술 (완료) | 완료 승계 — §1.1에 반영 확인 기록 |
| A1 EntityHandle · A2 shared_ptr 축소 | **트랙 E** (E1~E5, 정정 §1.3 반영) |
| B TransformStore SoA | **트랙 S** (S1~S4 — S2 lazy pull·S3 상호배타 확장 + **Transform 컴포넌트화**(사용자 결정 2026-08-16): TransformComponent는 저작 표면, 데이터 정본은 계속 스토어) |
| C 시스템 스케줄 | **트랙 C** (변경 없음) |
| K 컴포넌트 컨테이너·타입 이층 | **트랙 K** (변경 없음) |
| P0 지혈 (완료) | 완료 승계 — 사각 2건(이중 해제)은 G2가 닫는다 |
| P1~P3 | **트랙 P** (변경 없음) |
| P4 중첩·베리언트 (선택) | **P4 중첩은 핵심 승격**(UE 3원칙), 베리언트는 계속 선택 |
| D EnTT/Flecs 평가 게이트 | **폐지** — 외부 ECS 채택 없음(사용자 결정 2026-08-16). 대량 개체 수요는 자가 스토어 확장으로 |
| (없음) | **트랙 G 신설** — 신규 결함 15건(§1.2)의 수술 |
| (없음 — 구계획은 훅 표면 불변이 전제) | **트랙 L 신설** — Component 중심 생명주기(ScenePhase·catch-up·opt-in 틱·SimulationScope), Verse의 역할을 C#/CoreCLR이 승계(사용자 결정 2026-08-16, 같은 날 전면 확장) |

타 문서에서 구계획을 참조하던 곳은 이 문서로 이관됐다:
`UISystemRedesignPlan.md`(프리팹 트랙 P·instanceID), `MaterialPipelinePlan.md`
(M5↔트랙 P 왕복 회귀), `UtilityFrameworkModernizationPlan.md`(H4↔트랙 E 조회 수렴,
`GetGameObject` 자료구조는 트랙 E 소관).
