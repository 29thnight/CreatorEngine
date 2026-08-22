# UI 시스템 재설계 — Scene 소유 UI Runtime과 `weak_ptr` 없는 참조 모델 (PHASE 16)

> 최초 작성: 2026-08-19
> 전면 개정: 2026-08-21
> 연계 문서: `SceneGraphRedesignPlan.md`
> 범위: UI 소유권·생명주기·참조·입력·레이아웃·렌더 제출·에디터·회귀 검증

---

## 0. 최종 목표와 이번 개정 결론

최종 목표는 다음 한 문장으로 고정한다.

> **UI 오브젝트는 Scene이 소유하고, UI 런타임은 `weak_ptr`나 장기 보관 raw pointer 없이 Scene-qualified generation handle로 참조하며, 렌더러에는 값 타입의 불변 `UIDrawSnapshot`만 전달한다.**

CreatorEngine은 Unity의 저작 모델과 Unreal의 런타임 분리 원칙을 선택적으로 결합한다.

- Unity에서 유지할 것
  - `GameObject + Component` 저작 모델
  - `RectTransform`, anchor/pivot/offset 기반 배치
  - `Canvas`와 C# 컴포넌트 API
  - Scene/Prefab/YAML/Reflection 파이프라인
- Unreal에서 가져올 원칙
  - World(Scene) 단위 UI 컨텍스트와 명시적 mount/unmount
  - 입력 라우팅, hit-test, focus/capture의 한 시스템화
  - 레이아웃 무효화와 부분 갱신
  - 게임 오브젝트 수명과 렌더 제출 데이터 수명의 분리
- 가져오지 않을 것
  - 별도 Widget 객체 그래프를 SceneGraph와 병렬로 구축하는 것
  - `SWidget`과 유사한 참조 카운팅 UI 트리를 새로 만드는 것
  - 기존 `UIComponent`와 별개인 `UIElement` 타입 계층

### 0.1 이번 개정에서 추가한 내용

1. `Scene::UISceneContext`를 UI 런타임 상태의 유일한 소유자로 정의한다.
2. `EntityHandle{sceneId,index,generation}`을 UI 오브젝트 참조의 기본 단위로 사용한다.
3. 컴포넌트 참조는 `UIVisualRef{owner, role}`로 표현한다.
4. hierarchy 변경과 DDOL 이전을 독립 생명주기 이벤트로 명시한다.
5. `RectTransform` 좌표 계약을 `AnchoredPosition / WorldPivotPosition / ScreenPosition`으로 분리한다.
6. 렌더 스레드 경계에 값 타입 `UIDrawSnapshot`을 둔다.
7. 기존 native game script는 마이그레이션 대상에서 제외하고 C#과 에디터 저작 경로만 지원한다.

### 0.2 기존 계획에서 제거한 내용

| 제거 대상 | 제거 이유 | 대체안 |
|---|---|---|
| `Canvas::WidgetRegistry : vector<weak_ptr<UIElement>>` | 최종 목표와 직접 충돌하고 Canvas에 런타임 소유권을 다시 넣음 | `UISceneContext::CanvasBucket`의 handle 목록 |
| `Button::targetGraphic : weak_ptr<UIElement>` | 컴포넌트 수명과 엔티티 수명을 혼합 | 동일 엔티티의 `UIVisualRole` 또는 직렬화 가능한 route |
| 기존 `UIComponent`와 병렬인 새 `UIElement` 계층 | UUID/YAML/C# 호환 비용과 이중 런타임 발생 | 기존 `UIComponent`를 제자리에서 정비 |
| ProxyCommand/`shared_ptr<UIRenderProxy>`를 최종 구조로 유지 | 업데이트 스레드 객체 수명이 렌더 경계를 넘음 | 값 복사 `UIDrawSnapshot` |
| `verify-authored-rects` 기반 검증 | 현재 저작 자산과 검증 계약에서 퇴역 | `ui_layout_golden`과 resolution sweep |
| 과거 UI 자산 218개 일괄 변환 | 현재 트리에 해당 자산이 없으며 근거 없는 범위 | 현재 C# UI probe와 신규 저작 자산만 검증 |
| Dynamic native game script 호환/변환 | 명시적으로 미호환이며 지원 대상 아님 | 엔진 컴포넌트·C# API만 유지 |
| `U0.5 UIElement 도입` 단계 | 불필요한 타입 이원화 | U1~U2에서 기존 컴포넌트 등록 구조 교체 |

### 0.3 기존 계획에서 변경한 핵심

| 이전 제안 | 변경 후 |
|---|---|
| Canvas가 하위 UI 목록을 보유 | Scene 컨텍스트가 Canvas별 bucket을 보유; Canvas는 설정 컴포넌트 |
| 등록부가 pointer/`weak_ptr`를 보유 | 등록부는 handle 또는 `{handle, role}`만 보유 |
| navigation 직렬화 값과 runtime `weak_ptr` 병존 | route가 영속 원본, handle은 재해석 가능한 캐시 |
| UIManager가 전역 UI 상태 보유 | UIManager는 현재 Scene으로 전달하는 임시 facade만 허용 |
| RectTransform 수학은 변경 불필요 | 공개 좌표 의미를 분리하고 canonical 식과 golden으로 고정 |
| UI 렌더 proxy가 컴포넌트 객체를 간접 참조 | Scene commit 시 값 타입 draw item 생성 |

---

## 1. 현재 코드 기준선

이 절은 의도 문서가 아니라 2026-08-21 소스에서 확인한 현재 상태다.

### 1.1 이미 반영된 기반

- `EntityHandle`은 `sceneId + index + generation` 구조다.
- `Scene::Resolve`는 Scene 일치와 generation을 함께 검증한다.
- `Scene::Canvases`, `CanvasMap`, `UIManager::CurCanvas`, `UIManager::SelectUI`는 이미 `EntityHandle`을 사용한다.
- Scene은 `shared_ptr<Entity>` 목록으로 엔티티를 소유한다.
- UI 생명주기에는 `OnAddedToScene`, `OnRemovingFromScene`, enable/disable 및 tick lane 등록 경로가 있다.
- navigation의 영속 표현은 `parentHops + childOrdinals` route로 옮겨졌다.
- `RectTransform`은 현재 canonical anchor/pivot 식, `ScreenPosition`, reparent scale 보존 수정이 반영돼 있다.
- 버튼 hit-test에는 0 크기 방어와 top-left 입력을 centered layout 좌표로 바꾸는 경로가 있다.

### 1.2 `weak_ptr` 제거의 현재 상태

앞선 변경으로 실제 UI 참조 세 경로는 이미 Scene-qualified handle로 바뀌었다.

| 위치 | 현재 상태 | 다음 단계 |
|---|---|---|
| `Canvas::UIObjs` | `vector<EntityHandle>` | U1 shadow 검증 후 `CanvasBucket::members`로 이전 |
| `UIComponent::navigation` | route + `array<EntityHandle, NavDirectionCount>` | hierarchy/DDOL 재해석 규칙을 컨텍스트 이벤트에 연결 |
| `UIComponent::m_ownerCanvasObject` | `EntityHandle` | reparent 시 갱신을 보장하거나 컨텍스트 조회로 단순화 |

현재 UI 수명/등록 코드에는 실제 `weak_ptr` 선언이 남아 있지 않고 과거 설계를 설명하는 주석만 남아 있다. 따라서 **`weak_ptr` 제거 자체는 완료된 기반**이며, 다음 병목은 handle 등록부의 분산과 장기 보관 component pointer다. 이 handle들을 raw pointer로 되돌리는 중간 단계는 허용하지 않는다.

### 1.3 장기 보관 raw pointer 경로

| 위치 | 문제 |
|---|---|
| `UIManager::Images/Texts/SpriteSheets` | 컴포넌트 주소를 전역 컨테이너에 장기 보관 |
| UITickSystem lane | 컴포넌트 pointer의 등록/해제 순서에 안전성이 의존 |
| `Scene::CommitRenderProxies` 입력 | UIManager의 pointer 목록을 스냅샷해 렌더 갱신 명령 생성 |

U2 완료 조건은 이미 달성한 `weak_ptr` 0을 유지하면서 이 장기 보관 pointer까지 제거하는 것이다. 프레임 내부의 resolve 결과를 지역 변수로 사용하는 것은 허용한다.

### 1.4 현재 결함과 계획 반영 상태

| ID | 현상 | 판정 |
|---|---|---|
| C1 | GameView viewport 위치/크기가 생산자에서 주입되지 않음 | 0 나눗셈·NaN은 방어됐지만 실제 hit-test는 아직 비활성 상태가 될 수 있음 |
| C2 | Canvas 등록이 버튼 중심 경로에 치우침 | 모든 UIComponent 공통 mount로 교체 필요 |
| C3 | `navigation` 플래그가 mouse 상호작용까지 차단 | input capability를 분리해야 함 |
| C4 | Text/SpriteSheet가 렌더 제출 소비 경로와 불일치 | draw snapshot 전환에서 해소 |
| C5 | 입력 우선순위와 렌더 순서가 서로 다른 기준 사용 | 공통 sort key 필요 |
| C6 | reparent 시 Canvas membership이 갱신되지 않을 수 있음 | `OnHierarchyChanged` 필요 |
| C7 | 매 프레임 전체 layout 순회 | dirty root 기반 갱신 필요 |
| C8 | create/update proxy 매핑이 분리돼 누락 가능 | 하나의 draw item builder로 통합 |
| C9 | Canvas 설정의 Inspector 노출이 불완전 | U6에서 보완 |
| C10 | SpriteSheet C# component map이 불완전 | U0/U6에서 계약 확정 후 추가 |

### 1.5 제거 후보 dead path

실사용을 다시 확인한 뒤 아래 항목은 U0에서 삭제하거나 deprecate한다.

- `ICollision2D`
- 미사용 `UItype`
- 오탈자/미사용 `MaxOreder`
- UI 경로의 불필요한 `SpriteFont` include
- 과거 input queue와 중복 factory helper

### 1.6 현재 회귀 기준선

- `Tools/regression/verify-ui-layout-golden.ps1`
  - Rect 14개와 button hitbox 1개
  - `ScreenPinned`의 screen 좌표 `(320, 180)` 포함
- `Tools/regression/verify-ui-navigation-local.ps1`
- `Tools/regression/verify-ddol-canvas.ps1`
- `Tools/regression/ui_regression.txt`
- `Tools/regression/verify-resolution-sweep.ps1`
  - 현재 2/7 해상도, 12 assertions까지 통과 후 알려진 D3D12 resize 손상 `0x0000087D`가 별도 차단점

---

## 2. 설계 원칙

### 2.1 단일 소유권

Scene이 UI 엔티티와 UI 런타임 컨텍스트를 함께 소유한다. Canvas, UIManager, renderer가 UI 엔티티의 수명을 소유하거나 연장하지 않는다.

```text
Scene
├─ Entity storage                 // shared ownership의 유일한 런타임 근원
├─ HierarchyStore / SceneGraph
└─ UISceneContext
   ├─ Canvas registry
   ├─ UI component registry
   ├─ InputRouter state
   ├─ LayoutInvalidation state
   └─ frame-local UIDrawSnapshot builder
```

### 2.2 단일 hierarchy

UI는 별도 Widget tree를 만들지 않는다. SceneGraph parent/child가 transform, 활성화, Canvas 상속, input path의 원본이다.

- UI 엔티티: `RectTransformComponent` 사용
- Canvas 엔티티: `RectTransformComponent` 사용
- WorldSpace Canvas: 월드 배치를 위해 `Transform`을 추가로 허용
- 일반 UI 자식에 `Transform + RectTransform` 중복 부착 금지

### 2.3 명시적 mount/unmount

컨테이너 destructor나 만료된 `weak_ptr` 청소에 정확성을 맡기지 않는다. UI 등록은 생명주기 이벤트로 대칭 처리한다.

```text
component attached + entity in scene
    -> MountUIComponent

disable / component removal / entity removal / scene transfer
    -> UnmountUIComponent
```

### 2.4 resolve는 사용 지점에서 짧게

handle을 resolve해 얻은 pointer는 프레임이나 콜백을 넘어 저장하지 않는다.

```cpp
if (UIComponent* ui = scene.ResolveUI(ref))
{
    ui->HandleInput(event); // 이 scope 안에서만 사용
}
```

### 2.5 저작 데이터와 런타임 캐시 분리

- Prefab/YAML/C#: route, anchor, pivot, offset, style, resource key
- Runtime: handle, canvas membership, sort key, dirty flag
- Render: rect, color, UV, clip, resource handle 같은 값 데이터

---

## 3. 목표 참조 모델

### 3.1 엔티티 참조

```cpp
struct UINodeRef
{
    EntityHandle entity;
};
```

`EntityHandle` 자체에 Scene ID와 generation이 있으므로 별도의 global ID나 `weak_ptr<GameObject>`는 필요하지 않다.

### 3.2 UI 컴포넌트 참조

현재 일반 컴포넌트 경로는 동일 concrete type의 중복 부착을 기본 허용하지 않는다. 따라서 컴포넌트 주소 대신 소유 엔티티와 역할로 참조한다.

```cpp
enum class UIVisualRole : uint8_t
{
    Image,
    Text,
    SpriteSheet,
    Button,
    Custom
};

struct UIVisualRef
{
    EntityHandle owner;
    UIVisualRole role;
};
```

향후 동일 role 다중 부착을 허용할 때에만 `componentSlotGeneration`을 추가한다. 지금부터 범용 ComponentHandle 체계를 선행 설계하지 않는다.

### 3.3 Canvas membership

```cpp
struct CanvasBucket
{
    EntityHandle canvas;
    std::vector<UIVisualRef> members;
    bool sortDirty = true;
    bool layoutDirty = true;
};

class UISceneContext
{
public:
    void Mount(const UIVisualRef& ref);
    void Unmount(const UIVisualRef& ref);
    void OnHierarchyChanged(EntityHandle entity);
    void OnEntityTransferred(EntityHandle oldHandle, EntityHandle newHandle);
};
```

Canvas는 render mode, order, scaling 같은 저작 설정만 가진다. `Canvas::UIObjs`는 제거한다.

### 3.4 Navigation 참조

영속 원본은 현재 도입된 route다.

```cpp
struct UIObjectRoute
{
    uint32_t parentHops;
    std::vector<uint32_t> childOrdinals;
};

struct UINavigationTarget
{
    UIObjectRoute authoredRoute;
    EntityHandle resolved; // optional cache; 실패 시 invalid
};
```

규칙:

1. 저장과 복제는 route만 신뢰한다.
2. mount 또는 hierarchy 변경 시 현재 SceneGraph에서 handle을 재해석한다.
3. handle resolve가 실패하면 route로 한 번 재해석하고, 실패 상태를 안전하게 유지한다.
4. DDOL 이전 때 old Scene handle을 그대로 복사하지 않는다.
5. 대상 파괴는 generation 불일치로 검출하며 `weak_ptr::expired()` sweep은 사용하지 않는다.

### 3.5 target graphic

버튼의 target graphic은 보통 동일 엔티티의 `Image`다.

- 기본: `UIVisualRef{buttonOwner, Image}`
- 외부 대상: 직렬화 가능한 `UIObjectRoute + UIVisualRole`
- 금지: `weak_ptr<UIElement>`, 장기 `Image*`

---

## 4. 생명주기 계약

### 4.1 이벤트별 동작

| 이벤트 | 필수 동작 |
|---|---|
| component가 Scene 내 entity에 추가 | UI role 확인 → Canvas 탐색 → mount → dirty |
| entity가 Scene에 추가 | 부착 UI component 전체 mount |
| component 제거 전 | input/focus 해제 → registry unmount → renderer dirty |
| entity 제거 전 | 자손 포함 unmount → handle slot release |
| enable | 활성 registry에 참가, layout/input/render dirty |
| disable | focus/capture 해제, 활성 registry에서 제외 |
| same-scene reparent | 이전 Canvas unmount → 새 Canvas 탐색/mount → route 재해석 |
| DDOL Scene 이전 | old Scene에서 unmount → 새 handle 발급 → new Scene에서 mount |
| Scene teardown | input 중단 → snapshot 생산 중단 → context clear → entities 파괴 |

### 4.2 ordering 불변식

#### 제거

```text
OnRemovingFromScene
  -> UISceneContext::Unmount
  -> InputRouter::ReleaseCaptureAndFocus
  -> renderer dirty/snapshot fence
  -> component/entity destruction
  -> handle generation increment
```

#### DDOL 이전

```text
oldScene.Unmount(oldHandle)
  -> old handle slot release
  -> destination allocates new EntityHandle
  -> hierarchy restored
  -> newScene.Mount(newHandle)
  -> navigation route re-resolve
```

#### same-scene reparent

`OnAddedToScene`/`OnRemovingFromScene`이 호출되지 않아도 `SceneGraph::Reparent`가 `UISceneContext::OnHierarchyChanged`를 반드시 호출해야 한다.

### 4.3 thread 규칙

- Scene/UISceneContext mutation과 handle resolve: game thread
- `UIDrawSnapshot`: commit 이후 immutable
- render thread: Entity/Component/Canvas/UIManager 접근 금지
- input callback: dispatch 중 파괴 요청은 지연 처리하고 dispatch 종료 후 flush

---

## 5. RectTransform 좌표 계약

### 5.1 공개 의미

| API | 의미 |
|---|---|
| `AnchoredPosition` / `ui.pos` | 부모 anchor 기준 local offset |
| `WorldPivotPosition` | Scene UI layout 공간의 pivot 위치 |
| `ScreenPosition` / `ui.screenpos` | Canvas projection 이후 viewport top-left 기준 화면 좌표 |

이 세 의미를 다시 혼합하지 않는다. 특히 Inspector의 `ui.pos`는 표시 좌표가 아니라 실제 authored anchored offset이어야 한다.

### 5.2 canonical 식

부모 rect의 최소점과 크기를 `Pmin`, `Psize`, 자식의 anchor를 `Amin/Amax`, pivot을 `p`, sizeDelta를 `d`, anchored position을 `a`라 한다.

```text
anchorMinPos = Pmin + Psize * Amin
anchorMaxPos = Pmin + Psize * Amax
size         = (anchorMaxPos - anchorMinPos) + d
anchorRef    = lerp(anchorMinPos, anchorMaxPos, p)
pivotPos     = anchorRef + a
rectMin      = pivotPos - size * p
rectMax      = rectMin + size
```

root Canvas는 Canvas scaler/viewport가 제공하는 root rect에서 같은 식을 시작한다.

### 5.3 좌표 변환 규칙

```text
OS pointer (viewport top-left)
  -> Canvas projection
  -> centered UI layout space
  -> inverse parent chain if local query
```

- hit-test와 render는 같은 resolved rect와 같은 sort key를 사용한다.
- 크기 0 또는 viewport 미설정은 false/invalid 결과로 종료하고 NaN을 생성하지 않는다.
- reparent의 `worldPositionStays`는 position뿐 아니라 scale과 pivot 결과를 검증한다.

### 5.4 검증 행렬

- anchor: 4 corners, center, stretch, wide, tall
- pivot: `(0,0)`, `(0.5,0.5)`, `(1,1)`
- nesting: 3단 이상
- screen pin: 고정 viewport 좌표
- reparent: same Canvas, cross Canvas, DDOL
- canvas mode: Overlay, Camera, WorldSpace
- resolution/DPI: sweep 7종

---

## 6. 입력 아키텍처

### 6.1 `UIInputRouter`

전역 UIManager의 button scan을 아래 단계로 교체한다.

```text
Collect pointer/key events
  -> active Scene의 UISceneContext 선택
  -> Canvas projection
  -> clip-aware hit-test
  -> render와 동일한 sort key로 topmost 선택
  -> capture / focus / hover 갱신
  -> routed dispatch
```

### 6.2 capability 분리

`navigation` 하나로 mouse까지 끄지 않는다.

```cpp
struct UIInputCapabilities
{
    bool pointer = true;
    bool keyboard = true;
    bool navigation = true;
    bool focusable = true;
    bool raycastTarget = true;
};
```

### 6.3 상태 소유권

hover, pressed, focused, captured는 `UISceneContext::InputRouter`가 handle로 보유한다. 대상이 disable/unmount/destroy되면 즉시 invalidation한다.

---

## 7. Layout과 invalidation

### 7.1 1차 목표

현재 전체 UI 순회를 유지하되 정확한 dirty root를 수집한다.

dirty 원인:

- RectTransform authored 값 변경
- hierarchy/reparent
- Canvas scaler/viewport 변경
- Text 내용/폰트/크기 변경
- Image/Sprite resource 변경
- enable/disable

### 7.2 dirty root 규칙

- 부모 크기 변경: 영향받는 UI 자손
- 자식 preferred size 변경: 가장 가까운 layout controller 조상
- Canvas viewport 변경: 해당 Canvas root
- 중복 root는 조상 하나로 합친다.

### 7.3 후속 기능

기초 invalidation 안정화 후에만 추가한다.

- Horizontal/Vertical/Grid LayoutGroup
- ContentSizeFitter
- CanvasGroup
- RectMask/clip stack

---

## 8. 렌더 제출 경계

### 8.1 최종 구조

```cpp
struct UIDrawItem
{
    RectF rect;
    RectF clipRect;
    Color color;
    Float4 uv;
    UIResourceHandle resource;
    uint64_t sortKey;
    UIVisualRole role;
};

struct UIDrawSnapshot
{
    uint64_t sceneId;
    uint64_t frameNumber;
    std::vector<UIDrawItem> items;
};
```

`UIDrawItem`에는 다음이 없어야 한다.

- `Entity*`
- `Component*`
- `GameObject*`
- `weak_ptr`/`shared_ptr`로 소유된 UI 객체
- 렌더 스레드에서 resolve해야 하는 Scene handle

### 8.2 전환 경로

현재 `shared_ptr<UIRenderProxy>` 경로를 한 번에 제거하지 않는다.

1. 기존 Image/Text/SpriteSheet에서 공통 `BuildUIDrawItem`을 만든다.
2. create/update 분기 모두 같은 builder를 사용한다.
3. `CommitRenderProxies`가 값 snapshot도 동시에 생성하는 shadow mode를 둔다.
4. 결과 순서/rect/resource/color를 비교한다.
5. renderer 소비를 snapshot으로 전환한다.
6. proxy와 component pointer registry를 제거한다.

### 8.3 공통 sort key

render와 input이 아래 필드를 동일하게 사용한다.

```text
Canvas order
  -> hierarchy sibling order
  -> component/local order
  -> stable tie-breaker
```

---

## 9. 단계별 실행 계획

## U0 — 기준선 정리와 기존 수정 고정

### 완료된 항목

- [x] `EntityHandle`에 Scene ID와 generation 검증
- [x] Canvas/UIManager의 주요 entity 참조를 handle로 전환
- [x] navigation 영속 route 도입
- [x] RectTransform canonical 계산과 `ScreenPosition` 추가
- [x] button 0 크기/viewport 방어
- [x] C# API v19 반영
- [x] Rect 14 + hitbox golden 반영

### 남은 항목

- [ ] GameView viewport 위치/크기의 실제 생산자와 갱신 시점을 연결
- [ ] `ui.click` local command를 handle 기반으로 정리
- [ ] pointer/keyboard/navigation/focus capability 분리
- [ ] dead path 제거
- [ ] Canvas Inspector 설정 노출
- [ ] SpriteSheet의 C# component map 지원 여부 확정 및 구현

### 완료 조건

- golden/navigation/DDOL 테스트 통과
- viewport 미설정 시 NaN/오입력 없음
- 기존 native game script를 빌드 또는 변환 대상에 넣지 않음

## U1 — `UISceneContext`와 handle registry 도입

- [ ] Scene이 `UISceneContext`를 직접 소유
- [ ] `UIVisualRef`와 `CanvasBucket` 구현
- [ ] 기존 pointer/weak registry와 병행하는 shadow registry 도입
- [ ] add/remove/enable/disable/hierarchy 이벤트 계측
- [ ] 두 registry의 구성과 순서를 프레임별 비교

### 완료 조건

- 생성/삭제/reparent/DDOL에서 shadow registry mismatch 0
- Scene teardown 후 registry 0

## U2 — handle 등록 통합과 장기 raw pointer 제거

- [x] `Canvas::UIObjs`를 `EntityHandle` 목록으로 전환
- [x] `UIComponent::m_ownerCanvasObject`를 `EntityHandle`로 전환
- [x] `UIComponent::navigation`을 route + `EntityHandle` 캐시로 전환
- [ ] `Canvas::UIObjs`의 membership 책임을 `UISceneContext`로 이전한 뒤 필드 제거
- [ ] owner Canvas handle을 reparent 시 갱신하거나 context query로 단순화
- [ ] `UIManager::Images/Texts/SpriteSheets` 제거
- [ ] UITickSystem lane을 handle 또는 Scene-owned stable record로 전환
- [ ] component removal/hot replacement 경로 추가

### 완료 조건

- UI 런타임 소스에서 UI 소유/등록 목적의 `weak_ptr` 0 상태 유지
- UI registry의 장기 보관 `Component*` 0
- resolve 실패는 안전한 skip이며 stale 객체 호출 0

## U3 — InputRouter 통합

- [ ] Scene별 router 도입
- [ ] viewport/Canvas projection 단일화
- [ ] clip-aware hit-test
- [ ] render와 공통 sort key 사용
- [ ] hover/pressed/focus/capture를 handle로 관리
- [ ] dispatch 중 파괴/disable 테스트

### 완료 조건

- 겹친 UI에서 시각적 topmost와 입력 대상 일치
- reparent/DDOL/Scene 전환에서 focus/capture 누수 0

## U4a — Layout dirty propagation

- [ ] dirty 원인 계측
- [ ] dirty root 병합
- [ ] 부분 subtree layout
- [ ] full layout과 결과 비교 모드

### 완료 조건

- golden 결과 동일
- 변경 없는 프레임의 layout 방문 수 0 또는 상수 수준

## U4b — Layout 컴포넌트

- [ ] Horizontal/Vertical/Grid LayoutGroup
- [ ] ContentSizeFitter
- [ ] preferred/min/flexible size 계약
- [ ] cycle 검출과 경고

## U5a — 값 타입 `UIDrawSnapshot`

- [ ] 공통 `BuildUIDrawItem`
- [ ] shadow snapshot 생성과 proxy 결과 비교
- [ ] renderer 소비 전환
- [ ] `shared_ptr<UIRenderProxy>` 제거 가능성 확인

### 완료 조건

- render thread가 Scene/Entity/Component에 접근하지 않음
- create/update 누락 없이 Image/Text/SpriteSheet가 동일 builder 사용

## U5b — 렌더 기능 완성

- [ ] clip stack / RectMask
- [ ] CanvasGroup alpha와 raycast
- [ ] Overlay/Camera/WorldSpace 일관성
- [ ] batch key와 resource lifetime 검증

## U6 — 에디터와 직렬화

- [ ] RectTransform anchor/pivot preset
- [ ] Canvas mode/order/scaler Inspector
- [ ] navigation route picker와 끊긴 route 표시
- [ ] hit rect/clip/sort/debug overlay
- [ ] C# API와 reflection golden 갱신

## U7 — 전역 UIManager 상태 퇴역

- [ ] 모든 호출자를 `Scene::GetUIContext()`로 전환
- [ ] 임시 UIManager facade는 active Scene dispatch만 수행
- [ ] 전역 선택/Canvas/component 목록 제거
- [ ] 마지막 호출자 제거 후 facade 삭제

### 최종 완료 조건

- UI 상태는 Scene 간 공유되지 않음
- Scene 종료 후 UI 상태·input state·render state가 남지 않음
- runtime UI lifetime 관리를 위한 `weak_ptr` 0

---

## 10. 삭제 및 마이그레이션 표

| 현재 구조 | 목표 | 제거 시점 |
|---|---|---|
| `Canvas::UIObjs : vector<EntityHandle>` | `CanvasBucket::members` | U2 |
| `UIComponent::m_ownerCanvasObject : EntityHandle` | 검증된 cache 또는 context query | U2 |
| `UIComponent::navigation : EntityHandle[]` | 현재 route + handle cache 유지, context event 연결 | U2 |
| `UIManager::Images/Texts/SpriteSheets` | Scene context role registry | U2/U5a |
| UIManager button scan | `UIInputRouter` | U3 |
| component별 proxy create/update | `BuildUIDrawItem` | U5a |
| `shared_ptr<UIRenderProxy>` | `UIDrawSnapshot` | U5a 이후 |
| 전역 UIManager 상태 | active Scene facade 또는 제거 | U7 |
| Dynamic native game UI script | 미지원 | 변환하지 않음 |

---

## 11. 필수 불변식

1. Scene만 UI Entity의 생명주기를 소유한다.
2. UI registry는 `shared_ptr`/`weak_ptr`/장기 raw pointer로 UI 객체를 보유하지 않는다.
3. 모든 장기 엔티티 참조는 Scene-qualified generation handle이다.
4. Canvas는 authored configuration이며 UI 객체 소유자가 아니다.
5. SceneGraph가 유일한 hierarchy 원본이다.
6. reparent는 UI 생명주기 이벤트다.
7. DDOL 이전은 old unmount와 new mount 사이에서 handle을 재발급한다.
8. render와 hit-test는 동일 resolved rect와 sort key를 사용한다.
9. `AnchoredPosition`, `WorldPivotPosition`, `ScreenPosition`의 의미를 혼합하지 않는다.
10. render thread는 Scene/Entity/Component를 resolve하지 않는다.
11. 입력 dispatch 중 파괴는 지연되며 종료 후 안전하게 반영된다.
12. 기존 native game script 호환은 설계 제약이나 완료 조건이 아니다.

---

## 12. 위험과 대응

| 위험 | 대응 |
|---|---|
| handle resolve 비용 | Canvas bucket과 role별 dense list, 프레임 지역 캐시로 측정 후 최적화 |
| hierarchy event 누락 | shadow registry와 frame mismatch assert |
| DDOL에서 route/handle 불일치 | old/new Scene 두 단계 테스트와 generation assert |
| layout 부분 갱신 결과 차이 | full layout 비교 모드 유지 |
| proxy → snapshot 전환 중 시각 회귀 | 두 결과의 sort/rect/resource hash 비교 |
| viewport 연결 전 입력 오작동 | viewport invalid면 명시적 no-hit |
| Scene 전환 중 전역 facade 오염 | facade에 저장 상태 금지, 호출마다 active Scene 확인 |

---

## 13. 명시적 비범위

- Unreal 6 또는 Unity의 내부 구현을 그대로 복제하는 것
- RmlUi/Slate/uGUI 런타임을 외부 의존성으로 도입하는 것
- SceneGraph와 별도의 UI object tree를 만드는 것
- 범용 ECS/ComponentHandle 전환을 UI 개편의 선행 조건으로 삼는 것
- 존재하지 않는 과거 UI 자산의 대규모 자동 변환
- 기존 native game script의 호환, 복원, 빌드 편입

---

## 14. 구현 시작 순서

실제 코드 변경은 다음 순서를 지킨다.

1. GameView viewport 생산자 연결과 현재 회귀 기준선 완성
2. `UISceneContext` shadow registry 및 생명주기 계측
3. 분산된 Canvas/navigation/owner Canvas handle을 `UISceneContext` 생명주기에 연결
4. UIManager/UITickSystem의 장기 component pointer 제거
5. InputRouter 통합
6. dirty layout
7. 값 타입 draw snapshot
8. UIManager 전역 상태 퇴역

이 순서는 `weak_ptr`를 먼저 raw pointer로 바꾸는 임시 단계를 허용하지 않는다. 각 단계는 이전 구현과 결과를 비교할 수 있는 shadow/golden 검증을 포함해야 한다.
