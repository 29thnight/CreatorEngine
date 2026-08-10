# 오브젝트 모델 현대화 — 핸들 기반 데이터 지향 코어 전환

작성: 2026-08-10 · 계기: GameObject/Object 기반 설계 평가 + "유니티를 연상시키는 구조에서
벗어나되 최신의 발전된 구조를 취하고 싶다"는 요구.
관련 문서: `Phase4CouplingPlan.md`(간선 절단 — 방향 동일, 상호 보강),
`BuildPipelinePlan.md`(트랙 구조·게이트 관례를 승계),
`MultiCameraRenderPlan.md`(뷰별 시간축 상태 — B단계의 소비자).

---

## 0. 전략 요약

**표면과 코어를 분리해서 판단한다.** "유니티스러움"의 실체는 둘이다:

- **스크립팅 API 표면** (Awake/Update 훅, GetComponent, Instantiate) — 이것은 자산이다.
  사용자가 바로 익히고, C# 스크립트·에디터·직렬화가 전부 이 모양에 묶여 있다. 유지한다.
- **내부 아키텍처** (상속 기반 Object 그래프 + shared_ptr 소유 + 4중 정체성) — 이것이
  낡은 부분이다. 여기를 데이터 지향으로 갈아끼운다. Godot(서버/RID)도 Unreal(Mass)도
  이 방식으로 차별화했다 — 표면은 익숙하게, 코어는 데이터 지향으로.

**핵심 관찰: 이 엔진에는 이미 세 개의 씨앗이 있다.** 이 계획은 새 개념을 발명하지 않고
있는 씨앗을 정본(canonical)으로 승격한다:

| 씨앗 | 현재 위치 | 현재 신분 | 승격 후 |
|---|---|---|---|
| 세대 핸들 (index+generation, UAF 구조적 차단) | `ScriptObjectRegistry.h` | C# 경계 전용 옆 테이블 | **엔진 전체의 정체성 정본** (A) |
| 판정/디스패치 분리 (타입당 비트마스크) | `LifecycleRegistry.h` | 생명주기 훅 판정표 | 시스템 쿼리 등록의 원형 (C) |
| 페이즈별 평탄 리스트 (`m_updateList` 등) | `Scene.cpp:745-748` | 컴포넌트 포인터 리스트 | 명시적 시스템 스케줄 (C) |

단계는 A(정체성) → B(Transform SoA) → C(시스템 스케줄) → D(선택: 데이터 코어 교체 평가).
각 단계는 독립적으로 가치가 있고, 어디서 멈춰도 엔진은 그 시점의 개선을 유지한다.

## 1. 실측 — 지금 어디에 있나

### 1.1 정체성 4중 (2026-08-10 평가에서 확정)

GameObject 하나가 갖는 식별자와 그 용도:

| 식별자 | 타입 | 용도 | 문제 |
|---|---|---|---|
| `m_instanceID` | HashedGuid | 직렬화·전역 조회 | 기본 생성자와 ID 지정 생성자의 GUID 등록이 비대칭 |
| `m_index` | uint32 (씬 벡터 슬롯) | 계층·순회·Transform 부모 | DDOL 이동 후에도 옛 씬 슬롯 번호 유지, 인덱스 0 = 루트 매직 |
| `m_name` | HashingString | Find 조회 | 유일성은 `GenerateUniqueGameObjectName`이 보장 시도 |
| `m_attachedSoketID` / `m_prefabFileGuid` | HashedGuid / FileGuid | 소켓·프리팹 연결 | 별도 조회 API (`FindAttachedID`) |

조회 API도 `Find` / `FindIndex` / `FindInstanceID` / `FindAttachedID` 4갈래.

### 1.2 마이그레이션 표면 (grep 실측)

| 패턴 | 규모 | 밀집 지점 |
|---|---|---|
| `shared_ptr<GameObject>` | **99곳 / 24파일** | Scene 34, UIManager 26, ModelSceneBridge 10, HierarchyWindow 6 |
| `GameObject::Index` / `GameObjectIndex` | **94곳 / 25파일** | Scene 29, GameObjectCommand 14, GameObject 17 |
| `GetGameObject(` 호출 | **60곳 / 14파일** | Scene 15, ConsoleCommandSystem 14, HierarchyWindow 7 |

Dynamic_CPP(게임 프로젝트) 쪽 사용은 소수(C++ 레거시 스크립트 잔재) — 9-4 은퇴
트랙과 겹치므로 마이그레이션 대상에서 제외 가능.

### 1.3 현재 구조의 확인된 결함 (같은 날 평가에서)

- **계층 불변식 4필드**(`m_parentIndex`·`m_childrenIndices`·`m_rootIndex`·
  `m_transform.parentID`)를 최소 4개 호출처가 수동 유지: `GameObject.cpp:237`(AddChild),
  `Object.cpp:81`(DDOL), `Prefab.cpp:169`, `Object.cpp:145`(Instantiate).
  그리고 **`Object.cpp:146`은 실제로 틀렸다** — 자식 클론의 Transform 부모를
  자기 자신 인덱스로 설정(다른 모든 호출처는 부모 인덱스를 넘긴다).
- `m_isEnabled`가 public이라 PHASE 9-2의 전이 기반 OnEnable/OnDisable을 우회 가능.
- `m_typeID` 초기화 관용구 3종 혼재(GENERATED_BODY / 수동 대입 / type_guid).
- `Scene.cpp:104` — `const_cast<GameObject::Index&>`로 인덱스 대입. 인덱스 안정성은
  tombstone(null 슬롯)으로 처리 중 — **slot map의 절반을 이미 손으로 구현한 상태다.**
- `Object.cpp`가 말단 컴포넌트 10종을 include(기반 TU → 잎 의존).

### 1.4 Transform의 현재 형태 (`Transform.h`)

GameObject의 **값 멤버**(`m_transform`). 내부: 소유자 포인터 + `m_parentID`(uint32,
씬 인덱스) + 행렬 3개(local/world/inverse) + dirty 플래그 + 분해된 월드 성분.
per-object 산재 배치라 계층 갱신이 포인터 추적 순회. RectTransform은 별도 컴포넌트로
공존(지난 세션에서 성능·메모리 이중 부담 확인).

## 2. 목표 구조

```
[스크립팅 표면]   C# Behaviour(Awake/Update…) · GameObject/Component 파사드 API
                        │ 표면 계약 불변 — 스크립트·에디터·직렬화가 보는 모양
[오브젝트 모델]   GameObject = { EntityHandle + 컴포넌트 목록 + 이름/태그 }  ← 얇아진다
                        │ 정체성·수명 질의는 전부 핸들 경유
[데이터 코어]     Scene슬롯테이블(slot map: 소유+세대) · TransformStore(SoA)
                  시스템 스케줄(페이즈 리스트의 승격) · LifecycleRegistry(유지)
[경계]            ScriptObjectRegistry → 코어 핸들과 통합 (옆 테이블 소멸)
                  RenderScene 프록시 · DX12 일괄 기록 경로 → TransformStore 직결
```

규칙 셋:
1. **정체성은 핸들 하나.** `EntityHandle{index, generation}`이 런타임 정본.
   GUID는 직렬화·에셋 참조 전용, 이름은 조회 편의로 격하. `FindIndex`류 4갈래 API는
   핸들 resolve 하나로 수렴.
2. **소유는 씬 슬롯 테이블 하나.** shared_ptr 그래프 소멸. raw 포인터는 "이번
   프레임 안 지역 변수"로만 허용 — 보관은 핸들로만.
3. **핫 데이터는 SoA, 로직은 시스템.** Transform부터. 가상함수 개별 디스패치는
   네이티브 컴포넌트에서 단계적으로 걷어내되, C# 훅 표면은 ScriptSystem이 보존.

## 3. 실행 계획

각 슬라이스는 독립 커밋. 판정은 항상 ① CreatorEngine.sln + GameBuild.sln 전체 빌드
그린 ② `Tools/regression/run-all.ps1` ③ 성능 관련 슬라이스는 **측정 첨부**(DX12
실측 관례 — 주장 말고 숫자).

### A0 — 선행 정리: 확인된 결함 수술 (핸들 도입 전 바닥 다지기)

전환 대상 코드가 틀린 채로 이관되지 않게 먼저 고친다.

- ⬜ `Object.cpp:146` SetParentID 버그 수정(자식 자신 → 부모 인덱스) + 인접
  `dynamic_cast` null 검사. **회귀 확인: 계층 있는 프리팹 Instantiate 후 자식 월드
  위치가 원본과 일치하는지.**
- ⬜ 계층 4필드 갱신을 `GameObject::SetParent(Index parentIdx)` 단일 메서드로 캡슐화.
  AddChild·DDOL(`Object.cpp`)·`Prefab.cpp`·Instantiate 4곳이 전부 이걸 부르게 하고,
  4필드는 private으로. — A1에서 "핸들로 바꿀 자리"가 이 한 점이 된다.
- ⬜ `m_isEnabled` private화(리플렉션은 getter/setter 경유). PHASE 9-2 전이 훅의
  정합성 구멍 봉쇄.
- ⬜ `m_typeID` 초기화를 GENERATED_BODY로 통일(수동 대입 잔존: Animator·
  BoxCollider·CapsuleCollider·Canvas·ImageComponent·MeshRenderer·Prefab 등).
- ⬜ Object 복사 생성자 삭제(또는 새 GUID 발급 명시 구현) + `IObject`에 가상 소멸자.

### A1 — EntityHandle 도입: ScriptObjectRegistry의 일반화

**설계.** `ScriptObjectHandle`의 구조(uint32 index + uint32 generation, 0 = 무효)를
그대로 코어로 가져온다. 이름은 `EntityHandle`. Scene의 `m_SceneObjects`
(`vector<shared_ptr<GameObject>>` + tombstone)를 슬롯 테이블로 바꾼다:

```
struct Slot { std::unique_ptr<GameObject> object; uint32 generation; };
// tombstone(null 대입) → 슬롯 해제 + 세대 증가로 대체. 재사용 시 기존 핸들 자동 무효.
```

- 슬라이스 순서 (각각 독립 커밋):
  - ⬜ A1-1: `EntityHandle` 타입 + Scene 슬롯 테이블 도입. `GetGameObject(Index)`는
    당분간 유지(내부에서 핸들 resolve로 위임) — 60곳 호출처가 한 번에 안 깨지게.
  - ⬜ A1-2: `GameObject::Index` 보관처를 핸들로 교체 — `m_parentIndex`·
    `m_childrenIndices`·`m_rootIndex`·`Transform::m_parentID`. A0의 SetParent
    단일점 덕에 갱신 지점은 이미 한 곳이다.
  - ⬜ A1-3: 조회 API 수렴 — `FindIndex`/`FindInstanceID`/`FindAttachedID`를
    `Resolve(EntityHandle)` + GUID 조회(직렬화용) 둘로. 매직 인덱스 0(루트)은
    `Scene::RootHandle()` 명시 API로.
  - ⬜ A1-4: `ScriptObjectRegistry`를 코어 핸들로 통합 — C#에 넘기는 핸들이
    곧 엔진 핸들이 된다(배치 동일 조건 유지: uint32 두 개). 옆 테이블·역방향
    map·mutex 소멸.
- **직렬화 호환이 핵심 제약**: §4 참조. 파일 포맷은 바꾸지 않는다.
- 게이트: `shared_ptr<GameObject>`·`GameObject::Index` 잔존 수를
  `Phase4CouplingPlan`의 래칫 방식으로 주간 측정(99·94에서 단조 감소만 허용).

### A2 — shared_ptr 그래프 축소

- ⬜ 소유를 Scene 슬롯의 `unique_ptr` 하나로. `enable_shared_from_this` 제거.
- ⬜ 보관성 참조(UIManager 26곳, Canvas의 `weak_ptr` 목록, ModelSceneBridge 10곳,
  에디터 HierarchyWindow/SceneViewWindow)를 핸들로 교체. weak_ptr의 `lock()` 관용구는
  `scene->Resolve(handle)`로 1:1 치환된다 — 의미가 같다(죽었으면 null).
- ⬜ `Instantiate` 재설계: 시그니처를 GameObject로 좁히고 핸들 반환. 비-GameObject
  경로의 누수(평가 §5) 자연 소멸.
- 함정: DDOL 버킷(`SceneManagers->AddDontDestroyOnLoad`)은 씬을 넘는 소유라
  슬롯 테이블 바깥 — DDOL 전용 테이블을 두고 핸들에 씬 구분 비트를 넣을지, 이송
  시 재등록할지 이 슬라이스에서 결정(권장: 재등록 — 핸들 무효화가 명시적이라
  "옛 씬 슬롯 번호를 계속 들고 있는" 현재 버그 급의 모호함이 사라진다).

### B — TransformStore: 핫 데이터 SoA 적출

**설계.** position/rotation/scale/local·world 행렬/dirty/부모핸들을 씬 슬롯 인덱스와
평행한 배열들로. `Transform`은 데이터를 소유하지 않는 **뷰 타입**이 된다(핸들 +
스토어 참조). 기존 메서드 시그니처(`SetPosition`, `GetWorldMatrix`…)는 전부 유지 —
호출처와 리플렉션 표면 불변.

- ⬜ B-1: TransformStore 도입 + Transform을 뷰로 전환. inverse 행렬은 온디맨드
  계산으로 강등(지난 세션 결론: 구버전 스크립트 API 편의였음 — 64바이트/객체 회수).
- ⬜ B-2: 계층 갱신을 평탄 순회로 — 부모-우선 정렬 인덱스 배열을 유지하고
  dirty 전파를 배열 한 번 훑기로. **측정 필수**: 갱신 시간 before/after
  (오브젝트 1k/10k 씬, 지난 세션에 쓴 측정 하네스 재사용).
- ⬜ B-3: RectTransform 통합 — UI도 같은 스토어를 쓰고 앵커/피벗 해석만 별도
  시스템으로(지난 세션의 "Transform·RectTransform 공존 메모리 불리" 결론의 해소).
- ⬜ B-4: 소비자 직결 — RenderScene 프록시 수집과 DX12 일괄 기록 경로가
  TransformStore 배열을 직접 읽게. **소비자 없는 출력 금지**(라이브 배선 교훈):
  B-4까지가 한 묶음이고, B-1~3만 하고 멈추면 미완성으로 간주한다.

### C — 시스템 스케줄 승격

- ⬜ C-1: 페이즈 리스트를 `SystemSchedule`로 명명·구조화 — 실행 순서가 코드에
  명시되는 목록(FixedUpdate→Update→LateUpdate→커밋). 지금의 암묵 순서를 문서화하는
  수준의 얇은 슬라이스.
- ⬜ C-2: 구조 변경 지연 커밋 — AddComponent/Destroy를 프레임 중 즉시 반영하지 않고
  커맨드 버퍼에 쌓아 페이즈 경계에서 커밋. 현재 `m_pendingAwake/Start`가 절반을
  이미 하고 있다 — 파괴·부착까지 확장. (이터레이션 중 리스트 변형 사고의 구조적 차단)
- ⬜ C-3: 네이티브 컴포넌트의 가상 Update를 시스템 함수로 이관 — 컴포넌트당
  독립 슬라이스(Animator부터: 인스턴스 수가 많고 데이터 지역성 이득이 가장 큼).
  C# 스크립트는 건드리지 않는다 — ScriptComponent 일괄 호출이 하나의 시스템.
- LifecycleRegistry는 그대로 — 판정표는 이미 올바른 모양이다.

### D (선택) — 데이터 코어 외부화 평가 게이트

B·C 완료 시점에만 연다. 자가 구현(슬롯 테이블 + SoA 스토어)과 EnTT를 벤치로 비교:

- 채택 조건: ① 갱신·조회 벤치에서 자가 구현 대비 유의미한 우위 ② 리플렉션·직렬화
  통합 비용이 슬라이스 2개 이하 ③ MSVC 유니티 빌드에서 심볼 충돌 없음(기존 함정).
- Flecs는 계층·프리팹이 관계로 내장되어 기존 프리팹 시스템과 개념 충돌 — 이식
  비용이 이득을 넘을 가능성이 높아 후보에서 제외(재평가 조건: 프리팹 시스템을
  갈아엎는 별도 결정이 생길 때).
- 어느 쪽이든 A~C의 구조는 그대로 유효 — D는 저장소 구현 교체일 뿐이다.

## 4. 직렬화·프리팹 호환 전략

**파일 포맷 불변이 목표.** 씬 YAML에는 지금처럼 `m_index`/`m_parentIndex`(정수)와
GUID를 기록한다. 세대는 런타임 전용 — 저장하지 않는다.

- 로드: 파일의 인덱스로 슬롯을 순서대로 채우며 세대 1로 시작 → 핸들 재구성.
  로드 경로는 지금도 인덱스를 신뢰하므로 동작 변화 없음.
- 프리팹: `m_prefabFileGuid`·FileGuid 체계는 건드리지 않는다. `PrefabUtility`의
  인덱스 재배치 로직(`Prefab.cpp:169`)은 A0의 SetParent 단일점을 쓰게 되므로
  핸들 전환 시 자동 승계.
- 리플렉션: `[[Property]]` 표면은 유지. `EntityHandle`을 Meta 타입으로 등록하되
  직렬화 시 index만 내보내는 커스텀 시리얼라이저 하나 추가(MetaGenerator 수정
  불필요 — LifecycleRegistry가 옆 표를 택한 것과 같은 이유).
- C# 세이브/로드 경계: ScriptObjectHandle 배치(uint32×2)가 유지되므로 관리 측
  코드 변경 없음(A1-4에서 검증 항목으로 포함).

## 5. 함정 (이 코드베이스에서 이미 밟았던 것들)

- **유니티 빌드 전이 include**: 헤더를 옮기면 다른 TU가 조용히 깨진다.
  슬라이스마다 전체 빌드가 판정 기준인 이유.
- **워크트리 동시 커밋**: 작업 중 사용자가 같은 트리에 커밋한다. 커밋 전 HEAD
  재확인, 래칫 allowlist `--update` 금지.
- **Scene.h ↔ GameObject.h 순환**: 핸들 도입으로 오히려 완화된다(핸들은 전방 선언
  불필요한 POD) — 단, `GameObject.inl`의 `SceneObjectAt` 우회는 핸들 resolve로
  대체할 때까지 유지.
- **소비자 없는 출력 = 미완성 패스**: B단계 명시 규칙. 스토어만 만들고 렌더 경로가
  안 읽으면 그 슬라이스는 완료가 아니다.
- **두 핸들 체계의 공존 기간**: A1-1~A1-3 동안 Index와 EntityHandle이 공존한다.
  공존은 허용하되 **새 코드가 Index를 보관하는 것은 금지**(리뷰 체크 항목) —
  래칫 카운트가 감시한다.
- **DDOL**: 씬 슬롯 바깥의 소유가 유일하게 남는 지점. A2에서 재등록 방식으로
  명시 처리(§3 A2 참고). 여기를 얼버무리면 현재의 "옛 인덱스 유령" 버그가
  핸들 시대에도 형태만 바꿔 살아남는다.

## 6. 이 계획이 의도적으로 하지 않는 것

- **스크립팅 API 표면 변경** — Awake/Update·GetComponent·Instantiate 시그니처는
  유지한다. 쿼리 기반 스크립팅 API 같은 표면 차별화는 코어 전환이 끝난 뒤의
  별도 결정이다.
- **풀 아키타입 ECS 전환** — 리플렉션·에디터·프리팹·C# 바인딩 전면 재작성을
  요구한다. D 게이트의 EnTT(희소집합)까지가 이 계획의 상한이다.
- **GameObjectType enum 재설계** — UI/Canvas가 타입 enum에 있는 문제는 별도
  결정(지난 세션 논의)이며, 이 계획과 독립적으로 진행 가능하다.
