# 씬 그래프 재설계 — Entity·Component·Prefab (UE 로드맵 반영)

작성: 2026-08-16 · 계기: GameObject/Object 기반 설계 재평가 + "기존 보완계획을 제거하고
Scene Graph·Entity·Component·Prefab에 대한 UE 로드맵의 내용을 반영한 신규 구조
재설계안"을 세우라는 요구.

**이 문서는 `ObjectModelModernizationPlan.md`(2026-08-10)를 대체한다.** 그 계획에서
완료된 것(A0 결함 수술 3건 · P0 프리팹 지혈)과 실측·게이트 관례 중 유효한 것은 이
문서가 승계하고(§8 대체 매핑표), 원문은 워킹트리에서 제거했다 — git 이력으로 조회
가능하다(`git show HEAD:ObjectModelModernizationPlan.md`).

관련 문서: `Phase4CouplingPlan.md`(간선 절단 — 방향 동일),
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

- ⬜ **G1 — 파괴 단일점의 씨앗**: `GameObject::Destroy`가 base를 타든 자체적으로든
  `EraseGUID`를 반드시 지나게(N-1) + 파괴 경로에서 `ScriptObjectRegistry::Unregister`
  호출(N-4). 임시 배선이다 — E1에서 슬롯 해제 지점으로 수렴하며 자연 소멸. 판정:
  파괴 1k회 반복 후 `g_guids` 크기·레지스트리 LiveCount 불변 확인 콘솔 명령.
- ⬜ **G2 — 프리팹 소유권 이중 해제 2건**(N-2·N-3): 호출부 `delete` 제거 +
  `CreatePrefab`/`LoadPrefabFullPath` 반환을 non-owning으로 명시(주석이 아니라
  타입 또는 명명으로). `SavePrefab`의 캐시 무효화 순서 재검토 포함.
- ⬜ **G3 — Scene 소유 통일**(N-5): `m_scenes`를 `unique_ptr<Scene>`로, CreateScene
  경로의 delete 누락 구조적 해소.
- ⬜ **G4 — 소잔챙이 일괄**: DDOL 태그→레이어 복붙(N-11) · SaveScene return(N-12) ·
  `LayoutUINode`의 순환 가드를 `UpdateModelRecursive`로 이식(N-7, S-2에서 정본
  API로 수렴할 때까지의 방어) · Instantiate의 활성 씬 하드코딩을 `m_ownerScene`
  으로(N-9) · instanceID 재할당 시 이전 GUID erase(N-10).
- ⬜ **G5 — 죽은 코드 제거**(N-15): nested `GameObject::Type` ·
  `StaticGameObjectType` · `m_inverseMatrix`(+`GetInverseMatrix`) ·
  `GetComponentByTypeID` · `gameObjNode` 잔해. 참조 0 재확인 후 삭제(데이터 파일
  제외 전수 검색 — §6).

### 트랙 E — Entity 정체성 (구 A1·A2 승계, 정정 반영)

- ⬜ **E1 — EntityHandle + 진짜 슬롯맵**: `ScriptObjectHandle` 구조를 코어로 승격.
  `m_SceneObjects`를 세대 슬롯 테이블로 — **파괴 시 전원 재인덱싱(N-6) 폐지**,
  슬롯 재사용+세대 증가로 대체. 파괴 단일점이 이 슬라이스에서 완성된다(슬롯 해제 =
  세대 증가+GUID erase+관리 무효화, G1의 임시 배선 회수). `GetGameObject(Index)`는
  내부 resolve 위임으로 당분간 유지(호출 60곳/14파일 보호), N-13의 루트 폴백은
  이때 제거 — 무효 핸들은 nullptr이 정답이 된다.
- ⬜ **E2 — 계층 필드 수렴**: §1.3의 정정된 인벤토리(`m_childrenIndices` 6파일
  13곳, `m_rootIndex` 4파일 6곳)에서 시작해 쓰기를 SceneGraph 스토어 API로 수렴.
  "이미 한 곳"이라는 구계획 전제는 틀렸다 — 인벤토리부터가 작업이다.
- ⬜ **E3 — 조회 수렴**: 9갈래 → `Resolve(EntityHandle)` + GUID 조회. OwnerScene
  중복 4종은 전역 4종과 로직이 같으므로 씬 인자 하나로 합친다. 매직 인덱스 0은
  `Scene::RootHandle()`로. `GameObjectIndex = int`의 부호 문제는 핸들 전환으로
  자연 소멸.
- ⬜ **E4 — ScriptObjectRegistry 통합**: C#에 넘기는 핸들이 곧 엔진 핸들.
  배치(uint32×2) 동일 조건 유지, ScriptApiTable Version은 배치가 변하면 올린다.
  옆 테이블·역방향 map·mutex 소멸. N-4가 구조적으로 재발 불가가 되는 지점.
- ⬜ **E5 — shared_ptr 축소**: 소유는 슬롯 `unique_ptr` 하나. 보관성 참조 99곳/
  23파일을 핸들로. `enable_shared_from_this` 제거. DDOL은 재등록 방식(구계획 A2
  결정 승계 — 핸들 무효화가 명시적이라 "옛 인덱스 유령"이 사라진다).
- ⬜ **E6 — GameObject → Entity 심볼 리네임** (사용자 결정 2026-08-16, **K1-b
  이후에만**): 이름이 디스크 정본인 동안 리네임하면 §1.1의 "리네임 시 컴포넌트
  소실"과 같은 병을 오브젝트 노드에서 재현한다 — K1-b(영속 UUID)가 선 뒤 E6가
  그 첫 소비자이자 실증이 된다. C++은 `using GameObject = Entity;` 전환 별칭 →
  일괄 치환 → 별칭 제거 3단계, C#은 전환기 별칭(`[Obsolete]` 위임 또는 global
  using) 후 348개 스크립트 일괄 치환. 리플렉션·씬 파일의 구 클래스명은 읽기
  별칭으로 흡수(§5). `Behaviour.GameObject` 프로퍼티도 `Entity`로, 구명은
  `[Obsolete]` 위임.
- ⬜ **E7 — GameObjectType enum 소멸** (사용자 결정 2026-08-16, K1-a·S3 이후):
  엔티티의 본성은 타입 enum이 아니라 **컴포넌트 조합**이다(UE: 빈 엔티티는 순수
  컨테이너, 타입 개념 없음). 실사용 51곳/19파일(그중 Dynamic_CPP 레거시 6) —
  판정 분기는 K1-a 타입마스크로(`HasComponent<Canvas>()` 등), UI 분기
  (`Scene.cpp:1806-1813` 등)는 공간 컴포넌트 종류(S3의 Transform/RectTransform
  상호배타)로 대체. 생성자의 타입별 컴포넌트 부착 분기(`GameObject.cpp:36-58`)는
  "컴포넌트 셋을 받는 생성"으로 — 프리팹(트랙 P)이 그 정식 공급자다. 직렬화된
  `m_gameObjectType` 필드는 읽되 컴포넌트 구성 검증용으로만 쓰고 저장은 중단
  (읽기 호환 — §5).
- 게이트: `shared_ptr<GameObject>`·`GameObject::Index` 보관 잔존 수 래칫(99·94에서
  단조 감소). 새 코드의 Index 보관 금지(리뷰 항목). E7 이후 `GameObjectType::`
  잔존 수 래칫(51에서 단조 감소).

### 트랙 K — 컴포넌트 (구 K 승계, 변경 없음 — 즉시 착수 가능)

- ⬜ **K0** — `reserve(30)` 6줄 제거(생성자 3벌×2) + `GetComponent`의 임시
  shared_ptr 제거(107곳 혜택) + AddComponent 중복 검사를 맵 조회로. 측정 첨부.
- ⬜ **K1-a** — 런타임 타입 = 순차 인덱스 + uint64 마스크(30종<64).
  `HasComponent` 한 줄화, 씬 58%·프리팹 89%의 0개 오브젝트는 배열을 안 본다.
- ⬜ **K1-b** — 디스크 타입 = 수동 리터럴 UUID(이름 기반 v5는 목적 미달). 기동 시
  중복 검출. 읽기 호환: UUID 있으면 UUID, 없으면 이름+숫자 폴백, 저장 시만 UUID.
- ⬜ **K2** — 이중 구조 → 단일 슬롯 배열+SBO(4개 인라인), `unique_ptr` 전환.
  제거가 비로소 성립 — `UnregisterComponent` 동기를 같은 커밋에서(§6).
- ⬜ **K3** — 죽은 함수 정리(G5와 중복 확인 후 잔여분).

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

- ⬜ **S1 — 스토어 + TransformComponent 도입**: 계층(부모핸들/자식/루트)+position/
  rotation/scale/행렬/dirty를 슬롯 인덱스와 평행한 SoA로. `TransformComponent`는
  스토어 슬롯 핸들만 들고 기존 `Transform` 메서드 시그니처를 전부 유지.
  `GameObject::m_transform` 값 멤버 소멸 — 위 표의 보완 대상(`Component::m_pTransform`
  포함)을 같은 슬라이스에서 배선. inverse는 이미 죽어 있으므로(G5) 이관 대상 아님.
- ⬜ **S2 — 정본 순회 API + dirty push/lazy pull**: 순회 4곳(`UpdateModelRecursive`·
  `LayoutUINode`·`GetComponentsInChildren`·`DetachGameObjectHierarchy`)을 가드 내장
  API 한 벌로. 갱신은 매 프레임 전체 풀패스 2~3회 → dirty 서브트리만. Transform
  없는 엔티티는 공간 갱신 경로에서 마스크로 구조적 제외. **측정 필수**
  (1k/10k 씬 before/after — 기존 하네스 재사용).
- ⬜ **S3 — 공간 컴포넌트 상호배타**: UI/Canvas는 TransformComponent 없이
  RectTransform만 갖는다(현재 UI마다 행렬 3+벡터 3이 죽은 채 실림). 계열 중복은
  `AddComponent`가 거부. 앵커/피벗 해석은 별도 시스템. `UISystemRedesignPlan`의
  레이아웃 트랙과 경계 조율(그쪽 범위 밖 표에 양방향 참조 있음).
- ⬜ **S4 — 소비자 직결·변경분 커밋**: 렌더 프록시 수집이 스토어의 dirty 인덱스만
  커밋(현행: 전 렌더러 매 프레임 2단 값복사). **소비자 없는 출력 금지** — S1~S3만
  하고 멈추면 미완성.
- C# 표면 변경(위 표 마지막 행)은 S1이 아니라 **트랙 L의 L2와 묶어** 진행한다 —
  관리 경계를 건드리는 변경을 한 창구로 모은다.

### 트랙 P — 프리팹 (구 P 승계 + P4 승격)

- ✅ **P0 — 지혈** (2026-08-10 완료, 승계). 왕복 검사·`prefab.status` 가동 중.
  G2가 P0의 소유권 전환을 마저 닫는다(이중 해제 2건은 P0의 사각이었다).
- ⬜ **P1 — 오버라이드를 명시 데이터로**: `vector<PrefabOverride>`(경로·값 —
  직렬화기가 map 미지원이므로 등록 구조체 벡터). `m_prefabOriginal`·문자열 덤프
  비교 폐기(P-b·P-d·P-e 동시 해소). UE 의미론 그대로: 오버라이드된 속성만 부모
  전파에서 제외, 나머지는 계속 받는다. 읽기 호환 — 목록 부재 = 오버라이드 없음.
- ⬜ **P2 — 인스턴스 추적을 핸들로 + 왕복 복원** (E1 의존): `FileGuid →
  vector<EntityHandle>`, 세대 검사가 죽은 인스턴스를 자동 필터(P0 임시 방어 제거).
  씬 로드 시 재연결 — 주석 블록(`SceneManager.cpp:1105-1225`)을 되살리는 게 아니라
  P1 데이터 위에서 새로 쓴다. DDOL 로드 경로(재연결을 시도조차 안 함)도 포함.
  완료 시 왕복 검사 `-Strict` 기본 승격 — **트랙 P의 완료 신호**.
- ⬜ **P3 — 갱신을 비파괴로**: Destroy 후 재생성(P-f) 대신 차집합 적용 — 유지되는
  컴포넌트는 인스턴스 보존+프로퍼티 패치. 플레이 중 갱신 성립.
- ⬜ **P4 — 중첩 프리팹 (선택 → 핵심 승격)**: 프리팹 안의 프리팹 인스턴스를
  펼치지 않고 참조 노드(`{정의 GUID + 오버라이드}`)로 저장·복원. UE 3원칙의 세
  번째 기둥이라 승격한다 — 단 순서는 불변(P1의 오버라이드 모델이 자리잡은 뒤).
  베리언트(프리팹 상속)는 P4에서도 별도 결정으로 남긴다 — 에디터 UX가 얽힌다.
- N-14(UI instanceID 미갱신)는 P2에서 함께 판정 — `UISystemRedesignPlan` C3와
  같은 결정에 묶인다.

### 트랙 C — 시스템 스케줄 (구 C 승계, L4와 합류)

- ⬜ **C1** — 페이즈 리스트 6종을 `SystemSchedule`로 명명·구조화(암묵 순서의
  명시화). **L4의 opt-in 스케줄러와 같은 것의 두 얼굴** — 구독 대상 관리(L4)와
  실행 순서 관리(C1)를 한 구조물로 세운다.
- ⬜ **C2** — 구조 변경 지연 커밋: `m_pendingAwake/Start`가 이미 절반(L1에서 phase
  비교로 정식화됨) — 파괴·부착까지 커맨드 버퍼로 확장, 페이즈 경계 커밋.
- ⬜ **C3** — 네이티브 컴포넌트 가상 Update의 시스템 이관(Animator부터, 컴포넌트당
  독립 슬라이스). **L4의 opt-in 스케줄러가 전제다** — 암묵 구독(가상 오버라이드)을
  시스템 함수로 옮기는 것이 이 슬라이스의 실체. C# 훅은 ScriptSystem 일괄 호출
  하나 — 크로싱 계약 불변.

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

- ⬜ **L1 — ScenePhase 상태 기계 + 네이티브 훅 재편**: 엔티티에 ScenePhase
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
- ⬜ **L2 — C#을 씬 그래프의 저작 언어로**: `Behaviour.cs`에 6단계 훅 추가,
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
- ⬜ **L4 — 틱 opt-in 스케줄러**: 명시 구독 API(`scope` 귀속)를 정본으로, 가상
  오버라이드 감지는 **암묵 구독**으로 병존(기존 코드 무비용). 트랙 C와 같은 것의
  두 얼굴이다 — C1(SystemSchedule 명명)과 합류하고 C3(시스템 이관)의 전제가 된다.
  암묵 구독 잔존 수를 래칫으로 측정(새 코드는 명시 구독만).
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
