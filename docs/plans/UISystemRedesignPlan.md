# UI 시스템 재설계 — Scene 소유 UI Runtime과 값 타입 렌더 제출 (PHASE 16)

> 최초 작성: 2026-08-19
> 1차 개정: 2026-08-21
> **전면 개정: 2026-08-30 — 현재 소스 전수 실측 + 인접 계획 경계 재확정**
> 연계 문서: `SceneGraphRedesignPlan.md`, `RhiBoundaryPlan.md`, `MathematicsMigrationPlan.md`,
> `ReflectionRedesignPlan.md`, `EditorAutomationCLIPlan.md`, `MultiCameraRenderPlan.md`
> 범위: UI 소유권·생명주기·참조·입력·레이아웃·렌더 제출·텍스트 렌더·에디터·회귀 검증

---

## 0. 이번 전면 개정의 사유

1차 개정(08-21) 이후 아홉 날 동안 **이 계획서 밑의 지반이 세 번 바뀌었다.**

| 착지한 것 | 이 계획서에 미친 영향 |
|---|---|
| E7 소스 트리 재배치 (`6af4df2b`, `2c3bd764`) | UI 런타임은 `Engine/SceneRuntime/`, 프록시 타입은 `Engine/RenderEngine/`로 갈렸다. 경계가 계획서가 쓰인 때와 다르다 |
| 수학 이주 S6-C (`3903b695`, `7b1e60f3`) | UI 경계의 정본 타입이 `math::vector2/rect/color`다. §8.1의 `RectF/Float4/Color` 스케치는 **그대로 쓰면 안 되는 유령 타입**이 됐다 |
| 리플렉션 재설계 PHASE 18 | 매크로 0종, `static consteval auto reflect()` + `meta::schema`가 정본. U6의 "reflection golden 갱신"은 다른 기계를 가리키게 됐다 |

그리고 이번 실측에서 **계획서에 아예 없던 결함 세 건**이 나왔다(§1.4). 그중 하나는
"UI 텍스트를 그리는 코드가 엔진 어디에도 없다"이고, 이건 단계 하나를 고치는 문제가
아니라 **트랙 하나가 통째로 빠져 있었다**는 뜻이다.

부분 수정으로는 계획서 내부가 어긋난다고 판단해 전면 개정한다.

### 0.1 최종 목표 (유지)

> **UI 오브젝트는 Scene이 소유하고, UI 런타임은 `weak_ptr`나 장기 보관 raw pointer 없이
> Scene-qualified generation handle로 참조하며, 렌더러에는 값 타입의 불변
> `UIDrawSnapshot`만 전달한다.**

이 문장은 바꾸지 않는다. 이번 개정은 **거기까지 가는 길과 그 길에 없던 구간**을 고친다.

### 0.2 채택 모델 (유지)

- Unity에서 유지 — `GameObject + Component` 저작, `RectTransform` anchor/pivot/offset,
  `Canvas`와 C# 컴포넌트 API, Scene/Prefab/YAML/Reflection 파이프라인
- Unreal에서 가져오는 원칙 — World(Scene) 단위 UI 컨텍스트와 명시적 mount/unmount,
  입력 라우팅·hit-test·focus/capture의 한 시스템화, 레이아웃 무효화와 부분 갱신,
  게임 오브젝트 수명과 렌더 제출 데이터 수명의 분리
- **가져오지 않는 것** — SceneGraph와 병렬인 별도 Widget 객체 그래프, `SWidget` 유사
  참조 카운팅 UI 트리, 기존 `UIComponent`와 별개인 `UIElement` 타입 계층

### 0.3 인접 계획과의 경계

| 계획 | 이 계획이 거는 것 / 넘기는 것 |
|---|---|
| `SceneGraphRedesignPlan` | **거는 것**: `EntityHandle`(sceneId+index+generation), `Scene::Resolve`, DDOL Detach/Attach, 프리팹 instanceID 재발급(N-14 해소). **넘기는 것**: hierarchy 이벤트 훅 신설은 이 계획이 요구하고 SceneGraph가 제공한다 |
| `RhiBoundaryPlan` | **거는 것**: 해상도 스윕을 2/7에서 끊는 `0x0000087D` swapchain resize 결함은 **RHI 계층 소유다**(SceneGraphRedesignPlan §329도 같은 판정). 이 계획의 해상도 검증 행렬은 그 결함에 걸린 선행 의존이다. **제약**: 클리핑은 스텐실이 아니라 `clipRect` + discard — 멀티 백엔드 비용 0 |
| `MathematicsMigrationPlan` | `math::vector2/rect/color/matrix4x4`가 UI 경계의 정본 타입. 새 구조체를 만들 때 `Float4`류를 새로 도입하지 않는다 |
| `ReflectionRedesignPlan` | `reflect()` consteval 스키마가 직렬화·인스펙터·CLI `object.property`의 **공통 정본**이다. UI 필드를 늘리면 세 소비자가 동시에 따라온다 |
| `EditorAutomationCLIPlan` | `ui.*` 저작·관측 명령의 소유. 이 계획의 U0-a가 그 표면에 명령 3종을 더한다 |
| `MultiCameraRenderPlan` | Overlay UI는 `EnhancedLiveViewFlags::ScreenSpaceUI`를 가진 뷰에만 간다. Camera/World Space Canvas는 뷰별 평면으로 간다. **뷰별 UI 상태(hover/focus)는 이 계획의 U3가 정한다** |
| `MaterialPipelinePlan` | **경계 정정**: 폰트/SDF는 이 계획이 조사한 결과 **어느 계획도 소유하지 않았다**. 트랙 T로 이 계획이 받는다(§0.4) |

### 0.4 이번 개정에서 **신설**하는 것 — 트랙 T (텍스트 렌더)

실측: 엔진에 **텍스트를 화면에 그리는 코드가 없다.**

- DX11 `SpriteFont` 로딩은 은퇴했다 — `DataSystem.cpp:949` *"LoadSFont(DirectXTK SpriteFont)를 걷었다"*
- DX12 UI 패스는 텍스트를 **명시적으로 건너뛴다** — `EnhancedUIPass.cpp:80-86`,
  그리고 헤더가 사유를 적어 뒀다: `EnhancedUIPass.h:59` *"텍스트(SpriteFont)는 이 슬라이스에 넣지 않는다"*
- 폰트 아틀라스·글리프 배치 구현은 저장소에 0건 (`grep -i sdf|glyph|FontAtlas` → 엔진 측 히트 0)

기존 계획서의 U5b는 `clip stack / CanvasGroup / Overlay·Camera·World 일관성 / batch key`만
적고 **폰트 래스터화를 works에 배정하지 않았다.** 이건 심판이 지적했던 "리스크 서술로만
남기고 works에 배정 안 하기"와 같은 양식의 계획서 내부 모순이다. 트랙 T로 분리한다.

### 0.5 기존 계획에서 **제거**하는 항목

| 제거 대상 | 사유 |
|---|---|
| U0 *"`ui.click` local command를 handle 기반으로 정리"* | **그런 명령이 없다.** `ConsoleCommandSystem.cpp`에 `click` 문자열 0건. "정리"가 아니라 "신설"이며 U0-a로 옮긴다 |
| §8.1 `RectF/Float4/Color` 구조체 스케치 | S6-C 이후 유령 타입. `math::` 정본으로 다시 씀 |
| U0.5 WorkerPools 레이스 스파이크 | 파이프라인 자체가 철거됐다(`Scene.cpp:844-855`). 판정할 대상이 없다 |
| *"navigations 로드마다 2배"* (구 CRITICAL ②) | 해소. typed 직렬화 이관 때 수동 복원 루프를 의도적으로 미이식했다 — `ComponentFactory.cpp:171`, `ImageComponent.cpp:200`, `TextComponent.cpp:144` |
| *"UI 프리팹 instanceID 재발급 스킵"* (구 CRITICAL ③) | 해소. `verify-ui-navigation-local`의 **"ID 재발급 PASS"**가 살아 있는 판정이다 |
| §1.6의 "현재 회귀 기준선" 서술 | 오늘 실측값으로 교체(§1.6) |

---

## 1. 현재 코드 기준선 — 2026-08-30 실측

이 절은 의도 문서가 아니라 **오늘 소스와 실행으로 확인한 상태**다.

### 1.1 구조 지도

```
저작 ── object.create UI|Canvas · component.add · ui.anchor/size/pos/screenpos
         Inspector · Prefab/YAML(reflect 스키마)
   │
Scene (Engine/SceneRuntime)
   ├─ m_Entities            엔티티 소유 (shared_ptr)
   ├─ Canvases              vector<EntityHandle>
   ├─ UpdateUILayout()      ★ 씬 루트 전체 순회 · 프레임당 3회
   └─ CommitRenderProxies() ★ UIManager의 raw pointer 목록을 스냅샷
   │
UIManager (전역 싱글톤)          UITickSystem (전역 싱글톤)
   ├─ Images/Texts/SpriteSheets  ├─ Image/Text/SpriteSheet/Button/Canvas 레인
   │   ★ 장기 보관 Component*    │   ★ 장기 보관 Component*
   ├─ CurCanvas/SelectUI (핸들)  └─ TickLayout 호출
   └─ CheckInput()  ★ 단일 캔버스 · 삽입 순서 첫 히트
   │
RenderScene (RenderSceneBridge.cpp)
   └─ ProxyCommand::CreateUI/DestroyUI/Image·Text·SpriteSheetUpdate
        → shared_ptr<UIRenderProxy>   variant<ImageData, TextData, SpriteSheetData>
   │
EnhancedSceneRenderer
   ├─ BuildRectsFromQueue()   ← ImageData **만**, Overlay **만**
   └─ world sprite 평면       ← ImageData **만**, Camera/World Space
        ✗ TextData 소비자 0       ✗ SpriteSheetData 소비자 0
```

### 1.2 이미 닫힌 기반 (재확인)

- `EntityHandle`은 `sceneId + index + generation`이고 `Scene::Resolve`가 씬 일치를
  세대 검사보다 먼저 거른다.
- **UI 소유/등록 목적의 `weak_ptr` 0.** UI 런타임 소스 전체에 선언 0건이고 과거 설계를
  설명하는 주석 1건만 남아 있다(`UIManager.h:64`).
- `Canvas::UIObjs`, `UIComponent::m_ownerCanvasObject`, `UIComponent::navigation`은
  전부 `EntityHandle`이다(`Canvas.h:93`, `UIComponent.h:102`, `UIComponent.h:90`).
- navigation 영속 표현은 `parentHops + childOrdinals` route이고 구 `navObject` 파일은
  로드 시 1회 승격된다(`Prefab.cpp:69-100`, `UIComponent.cpp:240`).
- `RectTransform`은 canonical anchor/pivot 식, `ScreenPosition`, reparent scale 보존,
  캔버스 루트 직접 구동(`DriveAsCanvasRoot`)과 World Space 루트(`DriveAsWorldCanvasRoot`)를
  모두 갖고 있다.
- **부분 레이아웃 진입점이 이미 있다** — `Scene::LayoutUISubtree`(`Scene.cpp:2472`),
  호출자 9곳(UIManager 5 · SceneViewWindow 3). U4a는 백지가 아니다.
- 워커 UI 푸시 파이프라인은 철거됐다(`Scene.cpp:844-855`).

### 1.3 살아 있는 결함 — 구 C1~C10 재판정

| ID | 현상 | 2026-08-30 판정 | 근거 |
|---|---|---|---|
| C1 | GameView viewport 위치/크기 미주입 | ❌ **열림 · 최악** | `InputManager.h:98-99`의 두 필드에 **쓰기가 저장소 전체에 0건**. 읽기는 `UIButton.cpp:25-26`뿐이고 0-크기 방어에 걸려 `CheckClick`이 **항상 false**다 |
| C2 | Canvas 등록이 버튼 중심 경로에 치우침 | ❌ 열림 | 공통 mount 없음. `Canvas::AddUIObject`가 호출자별로 흩어져 있다 |
| C3 | `navigation` 플래그가 mouse까지 차단 | ❌ 열림 | `UIManager.cpp:404` `if (!isEnableUINavigation) return;`이 **마우스 스캔보다 위**에 있다 |
| C4 | Text/SpriteSheet가 렌더 소비 경로와 불일치 | ❌ **열림 · 확대 판정** | 불일치가 아니라 **소비자 0**이다. §1.4-① 참조 |
| C5 | 입력 우선순위와 렌더 순서가 다른 기준 | ❌ 열림 | 입력은 `CurCanvas` 하나만 · `UIObjs` 삽입 순서 첫 히트(`UIManager.cpp:413-421`), 렌더는 `(canvasOrder, layerOrder)` 안정 정렬 |
| C6 | reparent 시 Canvas membership 미갱신 | ❌ 열림 | `Entity::SetParentIndex`(`Entity.cpp:420-429`) → `HierarchyStore::SetParent`에 UI 훅이 없다 |
| C7 | 매 프레임 전체 layout 순회 | ❌ **열림 · 확대 판정** | §1.4-② 참조 |
| C8 | create/update proxy 매핑 분리 | ❌ 열림 | `UIProxyBridge.cpp`(create)와 `ProxyCommand.cpp:192/223/264`(update)가 **필드를 각각 따로 채운다** |
| C9 | Canvas 설정의 Inspector 노출 불완전 | ⚠ 완화 | `Canvas::reflect()`에 8필드가 있어 CLI `object.property`로는 전부 설정된다. Inspector 전용 UX만 남았다 |
| C10 | SpriteSheet C# component map 불완전 | ❌ 열림 | `ScriptCore/UIComponents.cs`에 SpriteSheet 타입 자체가 없다. **D-1 판정 대상**(§9.0) |

### 1.4 새로 드러난 것 (기존 계획서에 없음)

#### ① 텍스트·스프라이트시트는 소비자가 0이다 — "불일치"가 아니라 "생산만"

```
ImageComponent  → UIRenderProxy::ImageData       → BuildRectsFromQueue ✓ · world plane ✓
TextComponent   → UIRenderProxy::TextData        → 소비자 0
SpriteSheetComponent → SpriteSheetData           → 소비자 0
```

Create/Update/Destroy 배관은 세 종 모두 완비다(`RenderSceneBridge.cpp:126-180`,
`ProxyCommand.cpp:192/223/264`). 그런데 읽는 곳이 `ImageData` 하나뿐이다
(`EnhancedUIPass.cpp:80`, `EnhancedSceneRenderer.cpp:3172`).

즉 **매 프레임 프록시 생성·업데이트 비용을 물면서 화면 기여가 0이다.** 이 저장소가
이미 세 번 겪은 "생산만 있고 소비가 없는 파이프라인"의 네 번째 사례다
(워커 UI 푸시 · 컬링 버퍼 · `m_UIRenderQueue`에 이어).

**콘텐츠 영향 실측**: 저작 씬 `AsanLifeUI.creator`·`LifecycleUI.creator`가 각각
`TextComponent` 2개를 갖고 있다 — 그 텍스트는 **지금 화면에 나오지 않는다.**
`SpriteSheetComponent`는 저작 자산 사용 **0건**이다.

#### ② `UpdateUILayout`은 UI가 아니라 **씬 전체**를 훑고, 프레임당 3회 돈다

`Scene::UpdateUILayout`(`Scene.cpp:2442`)은

- 씬 루트의 **모든 자식**을 훑는다 — UI가 아닌 서브트리도 진입한다
- 호출마다 `std::unordered_set<Entity*> visited`를 **힙에 새로 만든다**
- `AllUpdateWorldMatrix()`가 매번 이것을 먼저 부르고, `Scene::Update`가 그
  `AllUpdateWorldMatrix()`를 **3곳**에서 부른다(`Scene.cpp:1275`, `1367`, `1435`)

구 C7 서술("매 프레임 전체 UI 순회")보다 **범위도 횟수도 크다.** 이건
`TransformExecutionGraphPlan`이 트랜스폼에서 잰 것과 같은 부류의 비용이고, 같은
자(프로파일러 `PROFILE_CPU`)로 재야 한다.

#### ③ 저작 UI 자산이 저장소에 하나도 없다

`.gitignore:504`가 `/Dynamic_CPP/Assets/Scenes/*`를 통째로 무시한다. UI가 든 씬 셋
(`AsanLifeUI`, `LifecycleUI`, `UITestScene`)은 **전부 이 기계에만 있다.**

결과: 저작 자산에 기대는 UI 게이트는 다른 기계에서 "씬이 없다"로 죽는다. DDOL 게이트가
이미 이 함정을 밟고 시나리오 저작본으로 옮겨 왔다(`ddol_canvas_probe.txt` 상단 주석).
**이 계획의 모든 신규 게이트는 CLI 저작본을 정본으로 한다**(D-3, §9.0).

#### ④ DDOL 게이트에 사각지대 — 멤버십 재구축이 무측정

오늘 실행한 `verify-ddol-canvas`의 본 판정 출력이 `DdolCanvas(0)`이다. 프로브 캔버스
밑에 UI 컴포넌트가 하나도 없어서, **캔버스 재등록만 증명하고 `UIObjs` 멤버십 재구축은
한 번도 재지 않는다.** §4.2의 DDOL 불변식 전체가 무측정 상태다.

#### ⑤ `UIManager`의 포인터 목록은 등록부가 아니라 **작업 목록**이다

`UIManager::Update`(`UIManager.cpp:501-629`)는 매 프레임 `Images/Texts/SpriteSheets`를
전부 훑으면서

1. `GetOwnerCanvas()`가 널인 UI를 **조상 계층에서 캔버스를 찾아 연결**하고(이름은 폴백),
2. `isDeserialized`가 false면 `DeserializeNavi()`로 navigation route를 재해석하고,
3. 마지막에 `CheckInput()`을 부른다.

주석이 사유를 적어 뒀다 — *"캔버스 연결의 단일 지연 지점(6-3)"*. 역직렬화 도중 연결하면
캔버스가 UI보다 늦게 복원될 때 순서 의존으로 실패하므로, **모든 오브젝트가 존재하는
프레임 경계**로 미룬 것이다.

이 사실이 U2의 순서를 정한다. **포인터 목록을 그냥 지우면 지연 연결·route 재해석이
함께 사라진다.** U1의 mount가 그 일을 받은 뒤에만 제거할 수 있다.

부수 비용도 여기 있다: 이미 연결된 UI도 매 프레임 `GetOwnerCanvas()`를 지나
`Scene::Resolve` + `GetComponent<Canvas>()`를 한 번씩 문다.

#### ⑥ 위젯 하나의 팬아웃이 34파일이다

`ImageComponent`를 참조하는 파일 전수: **엔진·에디터 28 + C#/스크립트 6 = 34.**
등록점만 세면 `ComponentTypeUUID.h` · `ComponentFactory.cpp` · `RegisterReflectManual.h` ·
`LifecycleRegistry.cpp` · `UIManager` · `UITickSystem` · `UIProxyBridge` · `ProxyCommand` ·
`RenderSceneBridge` · `RenderScene.h` · `EnhancedUIPass` · `ClrHost` · `Native.cs` ·
`UIComponents.cs` = **14곳**.

이 숫자가 U1~U2의 설계 판단 근거다. 등록부를 하나로 모으는 것의 값이 여기서 나온다.

### 1.5 제거 후보 dead path (재확인 — 전부 유효)

| 대상 | 실사용 | 위치 |
|---|---|---|
| `ICollision2D` | **구현체 0** | `UIManager.h:96-113` |
| `MaxOreder` (오탈자) | **참조 0** (정의·선언뿐) | `UIComponent.cpp:9`, `UIComponent.h:11` |
| `UItype` | **읽기 0** (쓰기 2건뿐) | `UIComponent.h:14`, `ImageComponent.cpp:20`, `TextComponent.cpp:15` |
| UI 경로의 `SpriteFont` include | D4 은퇴 잔재 | `AssetEntry.h:10`, `DataSystem.cpp:1253` |

### 1.6 회귀 기준선 — 2026-08-30 실행 결과

`Bin/x64-Debug/Editor/CreatorEditor.exe`(08-30 07:08 빌드)로 실제 실행한 값이다.

| 게이트 | 결과 | 비고 |
|---|---|---|
| `verify-ui-layout-golden` | ✅ **통과** | rect 14 · 히트박스 1 · client 1920x1080 · **골든 diff 0** |
| `verify-ui-navigation-local` | ✅ **통과** | 스키마 · 인스턴스 격리 · **ID 재발급** · 공간 조합 · 구파일 승격 |
| `verify-ddol-canvas` | ✅ 통과 | 이송 후 캔버스 1 · 계층 불일치 0. **단 멤버십 무측정**(§1.4-④) |
| `verify-resolution-sweep` | ⚠ 통과(단정 12 · 히트박스 2) | **도달 2/7** — `0x0000087D` swapchain resize 손상으로 중단. RHI 계층 소유 |
| `ui_regression.txt` | run-all 편입 | 비정상 순서 UI 생성 + 재생/정지 반복 |
| `dx12.ui` 자가 검증 | 통과 | **합성 사각형**으로 배칭·정렬만 잰다. 프록시→사각형 경로는 `EnhancedSceneRendererSelfTest.cpp:3204`가 `건너뜀` 수를 **로그로만** 남기고 단정하지 않는다 |

### 1.7 저작·관측 표면 인벤토리

| 있는 것 | 성격 |
|---|---|
| `object.create <이름> UI\|Canvas` | 생성 |
| `component.add <이름> ImageComponent\|TextComponent\|Canvas\|…` | 생성 |
| `ui.anchor / ui.size / ui.pos / ui.screenpos` | 설정 |
| `ui.rect / ui.hitbox / ui.navprobe / ui.status` | 관측 |
| `object.property <obj> <comp> <field> <값>` | **리플렉션 범용 설정** — `Canvas.RenderMode`, `TextComponent.message/fontPath`, `ImageComponent.texturePaths`가 전부 이걸로 저작된다 |

| 없는 것 | 막히는 검증 |
|---|---|
| **클릭 주입** (`ui.click`) | C1·C3·C5 전부. 클릭 경로를 태울 방법이 저장소에 없다 |
| **topmost 질의** (`ui.pick`) | C5의 "시각적 위 = 입력 대상" 판정 |
| **UI 상태 덤프** (드로우 아이템 목록) | U5a의 proxy↔snapshot 대조 |

`object.property`가 리플렉션 범용이라 **저작 표면의 구멍은 생각보다 좁다** — 입력 쪽
셋뿐이다. 이 사실이 U0-a의 규모를 정한다.

---

## 2. 설계 원칙

### 2.1 단일 소유권

Scene이 UI 엔티티와 UI 런타임 컨텍스트를 함께 소유한다. Canvas, UIManager, renderer가
UI 엔티티의 수명을 소유하거나 연장하지 않는다.

```text
Scene
├─ Entity storage                 // shared ownership의 유일한 런타임 근원
├─ HierarchyStore / SceneGraph
└─ UISceneContext
   ├─ Canvas registry
   ├─ UI component registry (role별 dense list)
   ├─ InputRouter state
   ├─ LayoutInvalidation state
   └─ frame-local UIDrawSnapshot builder
```

### 2.2 단일 hierarchy

UI는 별도 Widget tree를 만들지 않는다. SceneGraph parent/child가 transform, 활성화,
Canvas 상속, input path의 원본이다.

- UI 엔티티 · Canvas 엔티티: `RectTransformComponent`
- WorldSpace Canvas: 월드 배치를 위해 `Transform`을 추가로 허용
- 일반 UI 자식에 `Transform + RectTransform` 중복 부착 금지

### 2.3 명시적 mount/unmount

컨테이너 destructor나 만료 청소에 정확성을 맡기지 않는다.

```text
component attached + entity in scene   -> MountUIComponent
disable / removal / scene transfer     -> UnmountUIComponent
```

### 2.4 resolve는 사용 지점에서 짧게

handle을 resolve해 얻은 pointer는 프레임이나 콜백을 넘어 저장하지 않는다. 프레임 내부의
지역 변수 사용은 허용한다.

### 2.5 저작 데이터와 런타임 캐시 분리

- Prefab/YAML/C#/CLI: route, anchor, pivot, offset, style, resource key — **`reflect()` 스키마가 정본**
- Runtime: handle, canvas membership, sort key, dirty flag
- Render: rect, color, UV, clip, resource handle 같은 **값** 데이터

### 2.6 타입 규약 (신설)

새로 만드는 UI 구조체는 `math::` 타입만 쓴다. `Float4`/`RectF`/`Color` 같은 로컬 별칭을
새로 도입하지 않는다 — S6-C가 이미 경계를 통일했고, 여기서 갈라지면 그 이주가 되돌아간다.

---

## 3. 목표 참조 모델

### 3.1 엔티티 참조

```cpp
struct UINodeRef
{
    EntityHandle entity;   // sceneId + index + generation
};
```

### 3.2 UI 컴포넌트 참조

동일 concrete type의 중복 부착은 기본 금지이므로, 컴포넌트 주소 대신 소유 엔티티와
역할로 참조한다.

```cpp
enum class UIVisualRole : uint8_t
{
    Image,
    Text,
    SpriteSheet,   // D-1 판정에 따라 제거될 수 있다
    Button,
    Custom
};

struct UIVisualRef
{
    EntityHandle  owner;
    UIVisualRole  role;
};
```

동일 role 다중 부착을 허용할 때에만 `componentSlotGeneration`을 추가한다. 범용
ComponentHandle 체계를 **선행 설계하지 않는다.**

### 3.3 Canvas membership

```cpp
struct CanvasBucket
{
    EntityHandle              canvas;
    std::vector<UIVisualRef>  members;
    bool sortDirty   = true;
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

영속 원본은 이미 도입된 route다.

```cpp
struct UIObjectRoute
{
    uint32_t              parentHops;
    std::vector<uint32_t> childOrdinals;
};

struct UINavigationTarget
{
    UIObjectRoute authoredRoute;   // 저장·복제의 유일한 정본
    EntityHandle  resolved;        // 재해석 가능한 캐시
};
```

규칙: ① 저장·복제는 route만 신뢰 ② mount/hierarchy 변경 시 재해석 ③ resolve 실패는
route로 1회 재해석 후 안전 상태 유지 ④ DDOL 이전에 old handle 복사 금지 ⑤ 대상 파괴는
generation 불일치로 검출.

### 3.5 target graphic

- 기본: `UIVisualRef{buttonOwner, Image}`
- 외부 대상: 직렬화 가능한 `UIObjectRoute + UIVisualRole`
- 금지: 장기 `Image*`

---

## 4. 생명주기 계약

### 4.1 이벤트별 동작

| 이벤트 | 필수 동작 |
|---|---|
| component가 Scene 내 entity에 추가 | UI role 확인 → Canvas 탐색 → mount → dirty |
| entity가 Scene에 추가 | 부착 UI component 전체 mount |
| component 제거 전 | input/focus 해제 → registry unmount → renderer dirty |
| entity 제거 전 | 자손 포함 unmount → handle slot release |
| enable | 활성 registry 참가, layout/input/render dirty |
| disable | focus/capture 해제, 활성 registry에서 제외 |
| same-scene reparent | 이전 Canvas unmount → 새 Canvas 탐색/mount → route 재해석 |
| DDOL Scene 이전 | old Scene unmount → 새 handle 발급 → new Scene mount → route 재해석 |
| Scene teardown | input 중단 → snapshot 생산 중단 → context clear → entities 파괴 |

### 4.2 ordering 불변식

```text
[제거]
OnRemovingFromScene → UISceneContext::Unmount
  → InputRouter::ReleaseCaptureAndFocus
  → renderer dirty/snapshot fence
  → component/entity destruction
  → handle generation increment

[DDOL 이전]
oldScene.Unmount(oldHandle) → old slot release
  → destination allocates new EntityHandle
  → hierarchy restored
  → newScene.Mount(newHandle)
  → navigation route re-resolve

[same-scene reparent]
SceneGraph::Reparent → UISceneContext::OnHierarchyChanged   ★ 현재 없는 훅
```

세 번째가 C6의 정체다. `OnAddedToScene`/`OnRemovingFromScene`이 불리지 않는 경로이므로
**hierarchy 이벤트를 새로 만들어야 한다.**

### 4.3 thread 규칙

- Scene/UISceneContext mutation과 handle resolve: **game thread**
- `UIDrawSnapshot`: commit 이후 immutable
- render thread: Entity/Component/Canvas/UIManager 접근 금지
- input callback: dispatch 중 파괴 요청은 지연, dispatch 종료 후 flush

---

## 5. RectTransform 좌표 계약

### 5.1 방향 규약 (명시 — 기존 계획서 누락분)

**원점은 좌상단, y는 아래로 증가한다(y-down).** anchor 비율도 같은 방향이라
`anchorMin.y = 0`이 부모의 **위쪽** 변이고, pivot `(0,0)`도 좌상단이다
(`RectTransformComponent.h` 상단 규약 주석, PHASE 7-2 확정).

과거 주석 일부가 y-up처럼 읽혔고 구현·데이터 어디에도 근거가 없었다. 이 절이 그
규약의 계획서 측 정본이다.

### 5.2 공개 의미

| API | 의미 |
|---|---|
| `AnchoredPosition` / `ui.pos` | 부모 anchor 기준 local offset |
| `WorldPivotPosition` | Scene UI layout 공간(중심 원점)의 pivot 위치 |
| `ScreenPosition` / `ui.screenpos` | Canvas projection 이후 viewport 좌상단 기준 화면 좌표 |

이 세 의미를 다시 혼합하지 않는다.

### 5.3 canonical 식

부모 rect의 최소점과 크기를 `Pmin`, `Psize`, 자식 anchor를 `Amin/Amax`, pivot `p`,
sizeDelta `d`, anchored position `a`라 할 때:

```text
anchorMinPos = Pmin + Psize * Amin
anchorMaxPos = Pmin + Psize * Amax
size         = (anchorMaxPos - anchorMinPos) + d
anchorRef    = lerp(anchorMinPos, anchorMaxPos, p)
pivotPos     = anchorRef + a * layoutScale
rectMin      = pivotPos - size * p
rectMax      = rectMin + size
```

root Canvas는 앵커 계산을 쓰지 않고 `DriveAsCanvasRoot(screenRootRect, scale)`로
화면 rect를 직접 받는다 — uGUI와 같은 이유로, 캔버스 rect는 화면이 정하는 것이지
자기 앵커로 계산할 대상이 아니다.

### 5.4 좌표 변환 규칙

```text
OS pointer (viewport 좌상단 기준)
  → GameView viewport 오프셋/배율 보정      ★ C1이 죽어 있는 구간
  → 중심 원점 UI layout 공간
  → 필요시 부모 사슬 역변환
```

- hit-test와 render는 **같은 resolved rect와 같은 sort key**를 쓴다.
- 크기 0 또는 viewport 미설정은 **명시적 no-hit**으로 종료하고 NaN을 만들지 않는다.
- reparent의 `worldPositionStays`는 position뿐 아니라 scale과 pivot 결과를 검증한다.

### 5.5 검증 행렬

| 축 | 항목 | 현재 상태 |
|---|---|---|
| anchor | 4 corners · center · stretch(full/wide/tall) | ✅ 골든 8종 |
| pivot | (0,0) · (0.5,0.5) · (1,1) | ⚠ 골든에 pivot 축 없음 — U0-c에서 추가 |
| nesting | 3단 이상 | ✅ 골든 3단 |
| screen pin | 고정 viewport 좌표 | ✅ `ScreenPinned (320,180)` |
| reparent | same Canvas · cross Canvas · DDOL | ❌ 무측정 — U0-c |
| canvas mode | Overlay · Camera · WorldSpace | ❌ 무측정 — U5b |
| resolution/DPI | sweep 7종 | ⚠ **2/7** — RHI `0x87D` 선행 의존 |

---

## 6. 입력 아키텍처

### 6.1 `UIInputRouter`

전역 UIManager의 button scan을 아래로 교체한다.

```text
Collect pointer/key events
  → active Scene의 UISceneContext 선택
  → viewport 보정 + Canvas projection
  → clip-aware hit-test
  → 렌더와 동일한 sort key로 topmost 선택
  → capture / focus / hover 갱신
  → routed dispatch
```

현재와의 차이 셋: **모든 Canvas를 본다**(지금은 `CurCanvas` 하나), **topmost를 고른다**
(지금은 삽입 순서 첫 히트), **뷰를 안다**(멀티 카메라에서 어느 뷰의 좌표인지).

### 6.2 capability 분리

`navigation` 하나로 mouse까지 끄지 않는다.

```cpp
struct UIInputCapabilities
{
    bool pointer       = true;
    bool keyboard      = true;
    bool navigation    = true;
    bool focusable     = true;
    bool raycastTarget = true;
};
```

### 6.3 상태 소유권

hover, pressed, focused, captured는 `UISceneContext::InputRouter`가 **handle로** 보유한다.
대상이 disable/unmount/destroy되면 즉시 무효화한다.

### 6.4 멀티 뷰 규칙 (신설)

`MultiCameraRenderPlan`의 뷰별 시간축 상태 규칙과 같은 이유로, **hover/focus/capture는
뷰마다 따로 두지 않고 "입력을 받는 뷰" 하나에만 둔다.** UI 입력은 OS 포인터가 하나뿐인
단일 원천이라 뷰별 복제가 오히려 유령 hover를 만든다. 입력 뷰의 선택은 라우터가 보유한다.

---

## 7. Layout과 invalidation

### 7.1 1차 목표

전체 순회를 유지한 채 **정확한 dirty root를 수집**한다. dirty 원인:

- RectTransform authored 값 변경
- hierarchy/reparent
- Canvas scaler/viewport 변경
- Text 내용/폰트/크기 변경
- Image/Sprite resource 변경
- enable/disable

### 7.2 dirty root 규칙

- 부모 크기 변경 → 영향받는 UI 자손
- 자식 preferred size 변경 → 가장 가까운 layout controller 조상
- Canvas viewport 변경 → 해당 Canvas root
- 중복 root는 조상 하나로 합친다

### 7.3 현재 구현에서 승계할 것

`Scene::LayoutUISubtree(Entity* root)`가 이미 **부모 rect를 컴포넌트와 같은 규칙으로
찾아** 서브트리만 다시 계산한다. U4a는 이 함수를 dirty root 집합에 대해 부르는
드라이버를 만드는 일이지, 부분 레이아웃을 처음 만드는 일이 아니다.

동시에 §1.4-②의 두 낭비를 함께 없앤다.

- `visited` 집합의 프레임 할당 → 스크래치 버퍼 재사용(`CommitRenderProxies`가 이미 쓴 수법)
- 씬 루트 전체 진입 → Canvas bucket 기점 진입

### 7.4 후속 기능 (기초 안정화 후에만)

Horizontal/Vertical/Grid LayoutGroup · ContentSizeFitter · CanvasGroup · RectMask/clip stack

---

## 8. 렌더 제출 경계

### 8.1 최종 구조 (타입 정정)

```cpp
struct UIDrawItem
{
    math::rect          rect;        // 중심 원점 UI layout 공간
    math::rect          clipRect;
    math::color         color;
    math::vector4       uv;
    UIResourceHandle    resource;    // 텍스처 또는 폰트 아틀라스
    uint64_t            sortKey;
    UIVisualRole        role;
    CanvasRenderMode    renderMode;
    math::matrix4x4     canvasWorld; // Camera/World Space 평면
};

struct UIDrawSnapshot
{
    uint64_t                   sceneId;
    uint64_t                   frameNumber;
    std::vector<UIDrawItem>    items;
};
```

`UIDrawItem`에 있으면 안 되는 것: `Entity*`, `Component*`, `GameObject*`,
`shared_ptr`/`weak_ptr`로 소유된 UI 객체, 렌더 스레드에서 resolve해야 하는 Scene handle.

### 8.2 전환 경로

현재 `shared_ptr<UIRenderProxy>` 경로를 한 번에 제거하지 않는다.

1. Image/Text/SpriteSheet에서 공통 `BuildUIDrawItem`을 만든다.
2. create/update 분기가 **같은 builder**를 쓴다 — C8의 해소점이다.
3. `CommitRenderProxies`가 값 snapshot도 함께 만드는 shadow mode를 둔다.
4. 순서/rect/resource/color를 해시로 비교한다.
5. renderer 소비를 snapshot으로 전환한다.
6. proxy와 component pointer registry를 제거한다.

### 8.3 공통 sort key

렌더와 입력이 **같은 필드**를 쓴다.

```text
Canvas order → hierarchy sibling order → component/local order → stable tie-breaker
```

### 8.4 클리핑 (RHI 중립 제약)

스텐실이 아니라 `clipRect` + 픽셀 셰이더 `discard`로 한다. `RhiBoundaryPlan`의 멀티
백엔드 제약에서 스텐실 상태는 백엔드마다 비용이 갈리지만 `discard`는 0이다.

---

## 9. 실행 계획

### 9.0 착수 전 판정 항목 (works 배정 전에 결정한다)

| ID | 판정 | 권고 | 근거 |
|---|---|---|---|
| D-1 | `SpriteSheetComponent` 존치 / 은퇴 | **은퇴** | 저작 자산 사용 0 · C# 표면 0 · 렌더 소비자 0. 살리면 트랙 T·U5a·C# 바인딩이 전부 3종을 지고 간다 |
| D-2 | 텍스트 래스터화 방식 | **SDF 아틀라스** | 캔버스 스케일러가 폰트 크기에 배율을 곱한다(`UIProxyBridge.cpp`의 `fontSize * layoutScale`). 비트맵 아틀라스는 해상도 스윕에서 즉시 뭉개진다 |
| D-3 | 신규 게이트의 자산 원천 | **CLI 저작본만** | 저작 UI 씬 3종이 전부 gitignore(§1.4-③). 저작 자산 게이트는 다른 기계에서 죽는다 |
| D-4 | `UIManager` 전역 퇴역 시점 | **U7 유지** | 호출점 31곳/11파일인데 C# 바인딩은 2곳뿐이다(`ClrHost.cpp:1895/1904`). 진짜 이유는 `UIManager::Update`가 **지연 캔버스 연결의 단일 지점**이라는 것 — §1.4-⑤ |

---

### U0-a — 저작·측정 표면 신설 *(선행 · 다른 모든 단계보다 먼저)*

**왜 먼저인가**: C1·C3·C5를 고쳐도 **증명할 방법이 없다.** 클릭을 태울 명령이 없어
지금의 초록 게이트 넷 중 어느 것도 입력 경로를 지나지 않는다. 고침이 RED→GREEN으로
드러나려면 자가 먼저 있어야 한다.

- [ ] `ui.click <x> <y>` — viewport 좌표로 클릭을 주입하고 어떤 오브젝트가 먹었는지 출력
- [ ] `ui.pick <x> <y>` — 히트 후보를 sort key 순으로 전부 출력(topmost 판정용)
- [ ] `ui.drawitems` — 현재 프레임 draw item을 `role · rect · sortKey · resource`로 덤프
- [ ] 새 시나리오 `ui_input_probe.txt` + `verify-ui-input.ps1`

**완료 조건**
- 현재 코드 상태에서 `ui.click`이 **버튼 위를 찍어도 히트 0**을 보고한다 — 즉 C1이
  RED로 드러난다. (초록으로 나오면 자가 틀린 것이다)
- `ui.drawitems`가 Text 컴포넌트가 있는 씬에서 **Text 항목 0**을 보고한다 — §1.4-①이 RED

> ★ 이 단계의 완료는 "초록"이 아니라 **"결함이 빨갛게 보인다"**이다.

---

### U0-b — 지혈

- [ ] **C1** GameView viewport 위치/크기의 생산자 연결 (`GameViewWindow` → `InputManager`),
      Player 단독 실행 경로도 함께 — 에디터에만 넣으면 빌드된 게임에서 다시 죽는다
- [ ] **C3** `UIInputCapabilities` 도입, `isEnableUINavigation`이 pointer를 막지 않게 분리
- [ ] dead path 삭제 — `ICollision2D` · `MaxOreder` · `UItype` · SpriteFont 잔재(§1.5)
- [ ] (D-1 = 은퇴일 때) `SpriteSheetComponent` 제거 — 14 등록점 전부

**완료 조건**
- `verify-ui-input`이 **RED → GREEN**으로 뒤집힌다(U0-a의 두 RED 중 클릭 쪽)
- 골든/navigation/DDOL/sweep 기존 넷은 그대로 통과
- viewport 미설정 시 NaN 0 · 오입력 0

---

### U0-c — 게이트 보강 (눈먼 초록 제거)

- [ ] DDOL 프로브에 UI 자식을 넣고 **이송 후 `UIObjs` 멤버십**을 본 판정에 추가(§1.4-④)
- [ ] 골든에 **pivot 축 3종** 추가(§5.5의 빈칸)
- [ ] 골든에 **reparent 후 rect** 항목 추가(same Canvas / cross Canvas)
- [ ] `EnhancedSceneRendererSelfTest`의 UI `건너뜀` 수를 로그가 아니라 **단정**으로
- [ ] 변이 검사 — 각 신규 단정마다 고침을 한 줄 되돌려 정확히 그 항목만 빨개지는지 확인

**완료 조건**
- 신규 단정 각각이 **의도한 변이에서만** 빨개진다(전부 통과하는 첫 실행을 믿지 않는다)

---

### U1 — `UISceneContext`와 shadow registry

- [ ] Scene이 `UISceneContext`를 직접 소유
- [ ] `UIVisualRef`·`CanvasBucket` 구현
- [ ] 기존 pointer/handle registry와 **병행하는** shadow registry
- [ ] add/remove/enable/disable/**hierarchy** 이벤트 계측 — hierarchy 훅은
      `SceneGraph::Reparent` 쪽에 신설(§4.2, C6의 뿌리)
- [ ] 두 registry의 구성과 순서를 프레임별 비교, mismatch 시 assert

**완료 조건**
- 생성/삭제/reparent/DDOL에서 shadow mismatch **0**
- Scene teardown 후 registry **0**

**함정**: UI 필드를 늘리면 `reflect()` 스키마 · C# `Native.cs`/`UIComponents.cs` ·
Inspector가 **동시에** 따라와야 한다(§1.4-⑥의 14 등록점). 하나만 고치면 조용히 갈린다.

---

### U2 — 등록 통합과 장기 raw pointer 제거

**선행**: U1 — mount가 **지연 캔버스 연결과 route 재해석을 인수한 뒤에만** 포인터 목록을
지울 수 있다(§1.4-⑤). 이 순서를 어기면 로드 순서 의존이 되살아나고, 그 실패는 예전처럼
조용한 미연결로 나타난다.

- [x] `Canvas::UIObjs`를 `EntityHandle` 목록으로 전환
- [x] `UIComponent::m_ownerCanvasObject`를 `EntityHandle`로
- [x] `UIComponent::navigation`을 route + handle 캐시로
- [ ] `Canvas::UIObjs`의 membership 책임을 `UISceneContext`로 옮긴 뒤 **필드 제거**
- [ ] owner Canvas handle을 reparent 시 갱신하거나 context query로 단순화
- [ ] **`UIManager::Images/Texts/SpriteSheets` 제거** — 소비자 셋을 각각 옮긴 뒤에만:
      ① `UIManager::Update`의 지연 연결·route 재해석(→ U1 mount),
      ② `Scene::CommitRenderProxies`(`Scene.cpp:778-781`, → context role registry),
      ③ `ui.status`(→ context 질의)
- [ ] `UITickSystem` 레인을 handle 또는 Scene-owned stable record로
- [ ] component removal / hot replacement 경로
- [ ] 이미 연결된 UI가 매 프레임 `Resolve + GetComponent<Canvas>()`를 무는 경로 제거

**완료 조건**
- UI 런타임 소스에서 UI 소유/등록 목적 `weak_ptr` **0 유지**
- UI registry의 장기 보관 `Component*` **0**
- resolve 실패는 안전한 skip이며 stale 객체 호출 **0**
- **비정상 순서 로드 게이트(`ui_regression.txt`)가 그대로 통과** — 지연 연결을 인수한
  증거는 이 게이트다

---

### U3 — InputRouter 통합

**선행**: U0-b(C1) · U1(mount 이벤트)

- [ ] Scene별 router 도입, 모든 Canvas를 대상으로
- [ ] viewport/Canvas projection 단일화
- [ ] clip-aware hit-test
- [ ] **렌더와 공통 sort key** — C5의 해소점
- [ ] hover/pressed/focus/capture를 handle로 관리
- [ ] dispatch 중 파괴/disable 테스트

**완료 조건**
- `ui.pick`의 최상단과 `ui.click`이 먹은 대상이 **일치**
- 겹친 UI에서 시각적 topmost = 입력 대상
- reparent/DDOL/Scene 전환에서 focus/capture 누수 **0**

---

### U4a — Layout dirty propagation

**선행**: U1(hierarchy 이벤트)

- [ ] dirty 원인 계측 (§7.1)
- [ ] dirty root 병합
- [ ] `LayoutUISubtree` 기반 부분 갱신 드라이버
- [ ] `visited` 스크래치 버퍼화 · Canvas bucket 기점 진입(§7.3)
- [ ] full layout과 결과 비교 모드

**완료 조건**
- 골든 결과 **동일**
- 변경 없는 프레임의 layout 방문 수 **0 또는 상수**
- `PROFILE_CPU`로 잰 `UpdateUILayout` 프레임 비용이 착수 전 대비 감소(수치는 착수 시 실측)

### U4b — Layout 컴포넌트

- [ ] Horizontal/Vertical/Grid LayoutGroup
- [ ] ContentSizeFitter
- [ ] preferred/min/flexible size 계약
- [ ] cycle 검출과 경고

---

### 트랙 T — 텍스트 렌더 *(신설 · §0.4)*

**T0 — 폰트 자산과 아틀라스 정본**
- [ ] 폰트 자산 타입 정의(`.ttf` 임포트 → 아틀라스 + 메트릭)
- [ ] SDF 생성 경로(D-2 권고) · 아틀라스 캐시 · GUID 계약(`SerializationPlan` D2 준수)
- [ ] `TextComponent::fontPath`가 이 자산을 가리키게

**T1 — 텍스트 draw item**
- [ ] 글리프 배치(커닝·줄바꿈·정렬) → `UIDrawItem` 다중 사각형
- [ ] `BuildRectsFromQueue`의 `TextData` 분기 — 지금 `++skipped`로 버리는 자리
- [ ] `dx12.text` 자가 검증: 글리프 수 · 배치 수 · 픽셀 표본

**T2 — 스케일·해상도 대응**
- [ ] `fontSize * layoutScale`이 SDF 샘플링과 맞물리는지 해상도 스윕으로
- [ ] 한글 등 다국어 글리프 범위 (기존 코드 주석이 이미 이 함정을 겪었다:
      `TextComponent.h`의 *"한글이 안나올시…"*)

**완료 조건**
- `ui.drawitems`가 Text 항목을 **보고**하고, 저작 씬 `AsanLifeUI`/`LifecycleUI`의
  텍스트 4개가 화면에 나온다
- U0-a에서 세운 두 번째 RED가 GREEN으로

**순서 자유도**: T는 U1~U4와 독립이다. 콘텐츠 가시성 회복을 우선하면 U5a 이전에
프록시 경로로 먼저 붙이고, U5a에서 공통 builder로 흡수한다. 그 경우 T1의 코드가
한 번 옮겨지는 비용을 **의도적으로 지불하는 것**임을 기록해 둔다.

---

### U5a — 값 타입 `UIDrawSnapshot`

- [ ] 공통 `BuildUIDrawItem` (create/update 단일화 — C8)
- [ ] shadow snapshot 생성과 proxy 결과 해시 비교
- [ ] renderer 소비 전환
- [ ] `shared_ptr<UIRenderProxy>` 제거

**완료 조건**
- render thread가 Scene/Entity/Component에 접근하지 않음
- create/update 누락 없이 모든 role이 같은 builder를 지남
- `ui.drawitems` 출력이 proxy 경로와 snapshot 경로에서 **동일**

### U5b — 렌더 기능 완성

- [ ] clip stack / RectMask (`clipRect` + discard, §8.4)
- [ ] CanvasGroup alpha와 raycast
- [ ] **Overlay/Camera/WorldSpace 일관성** — 지금 Overlay만 `BuildRectsFromQueue`를 지나고
      나머지는 world sprite 경로다(`EnhancedSceneRenderer.cpp:3169-3190`). 두 경로가
      같은 draw item에서 갈라지게
- [ ] batch key와 resource lifetime 검증

---

### U6 — 에디터와 직렬화

- [ ] RectTransform anchor/pivot preset (표는 `GetAnchorPresetTable()` 한 곳)
- [ ] Canvas mode/order/scaler Inspector (C9의 잔여 — 값 경로는 이미 있다)
- [ ] navigation route picker와 끊긴 route 표시
- [ ] hit rect / clip / sort / debug overlay
- [ ] C# API와 reflect 스키마 골든 갱신 — `reflect_golden.yaml`

### U7 — 전역 `UIManager` 퇴역

- [ ] 모든 호출자를 `Scene::GetUIContext()`로
- [ ] 임시 facade는 active Scene dispatch만
- [ ] 전역 선택/Canvas/component 목록 제거
- [ ] `UIManagers->`를 지나는 호출점 **31곳/11파일** 재배선. 그중 C# 브릿지는
      `Api_UiNav_GetSelected/SetSelected` **2곳뿐**이고(`ClrHost.cpp:1895/1904`),
      UI-facing C# API 46종(Rect 10 · Image 11 · Text 11 · Ui/UiNav 7 · Canvas 5 ·
      Button 2)의 나머지는 컴포넌트를 직접 지나므로 이 단계와 무관하다 (D-4)
- [ ] 마지막 호출자 제거 후 facade 삭제

**최종 완료 조건**
- UI 상태가 Scene 간 공유되지 않음
- Scene 종료 후 UI 상태·input state·render state 잔존 0
- runtime UI lifetime 관리를 위한 `weak_ptr` 0

---

## 10. 순서 제약

```text
U0-a ─► U0-b ─► U0-c ─┬─► U1 ─► U2 ─┬─► U3 ─────────────┐
 (자 먼저)             │             │                    │
                       │             ├─► U4a ─► U4b       ├─► U6 ─► U7
                       │             │                    │
                       │             └─► U5a ─► U5b ──────┘
                       │                  ▲
   트랙 T ─ T0 ─► T1 ─►T2 ─────────────────┘  (U1~U4와 독립)

선행 의존(트랙 밖):
  RHI 0x0000087D swapchain resize  ─► §5.5 해상도 7종 검증
  SceneGraph hierarchy 이벤트 훅   ─► U1의 reparent 계측
```

**금지**: `weak_ptr`를 먼저 raw pointer로 바꾸는 중간 단계. 각 단계는 이전 구현과
결과를 비교할 수 있는 shadow/golden 검증을 포함해야 한다.

---

## 11. 삭제 및 마이그레이션 표

| 현재 구조 | 목표 | 제거 시점 |
|---|---|---|
| `Canvas::UIObjs : vector<EntityHandle>` | `CanvasBucket::members` | U2 |
| `UIComponent::m_ownerCanvasObject` | 검증된 cache 또는 context query | U2 |
| `UIManager::Images/Texts/SpriteSheets` | Scene context role registry | U2 |
| `UITickSystem` 레인의 `Component*` | handle 또는 Scene-owned record | U2 |
| `UIManager::CheckInput` button scan | `UIInputRouter` | U3 |
| `UpdateUILayout` 전체 순회 | dirty root + `LayoutUISubtree` | U4a |
| `UIRenderProxy::TextData` 소비자 0 | 트랙 T의 글리프 draw item | T1 |
| `UIRenderProxy::SpriteSheetData` | **은퇴**(D-1 권고) 또는 draw item | U0-b / U5a |
| component별 proxy create/update | `BuildUIDrawItem` | U5a |
| `shared_ptr<UIRenderProxy>` | `UIDrawSnapshot` | U5a |
| `ICollision2D` · `MaxOreder` · `UItype` | 삭제 | U0-b |
| 전역 `UIManager` 상태 | active Scene facade 또는 제거 | U7 |

---

## 12. 필수 불변식

1. Scene만 UI Entity의 생명주기를 소유한다.
2. UI registry는 `shared_ptr`/`weak_ptr`/장기 raw pointer로 UI 객체를 보유하지 않는다.
3. 모든 장기 엔티티 참조는 Scene-qualified generation handle이다.
4. Canvas는 authored configuration이며 UI 객체 소유자가 아니다.
5. SceneGraph가 유일한 hierarchy 원본이다.
6. reparent는 UI 생명주기 이벤트다.
7. DDOL 이전은 old unmount와 new mount 사이에서 handle을 재발급한다.
8. render와 hit-test는 동일 resolved rect와 sort key를 사용한다.
9. `AnchoredPosition` / `WorldPivotPosition` / `ScreenPosition`의 의미를 혼합하지 않는다.
10. UI 좌표는 y-down이고 원점은 좌상단이다(§5.1).
11. render thread는 Scene/Entity/Component를 resolve하지 않는다.
12. 입력 dispatch 중 파괴는 지연되며 종료 후 안전하게 반영된다.
13. UI 경계 타입은 `math::`가 정본이다.
14. **생산 경로를 만들면 같은 슬라이스에서 소비자를 붙이거나, 붙이지 않는 이유를
    works에 적는다.** — §1.4-①이 이 저장소에서 네 번째 재발이다.
15. 기존 native game script 호환은 설계 제약이나 완료 조건이 아니다.

---

## 13. 위험과 대응

| 위험 | 대응 |
|---|---|
| handle resolve 비용 | Canvas bucket과 role별 dense list, 프레임 지역 캐시로 **측정 후** 최적화 |
| hierarchy event 누락 | shadow registry와 frame mismatch assert (U1) |
| DDOL에서 route/handle 불일치 | old/new 두 단계 테스트 + generation assert + U0-c의 멤버십 판정 |
| layout 부분 갱신 결과 차이 | full layout 비교 모드 유지 |
| proxy → snapshot 전환 중 시각 회귀 | sort/rect/resource 해시 비교 + `ui.drawitems` 대조 |
| viewport 연결 전 입력 오작동 | viewport invalid면 **명시적 no-hit** (현재 동작 유지) |
| Scene 전환 중 전역 facade 오염 | facade에 저장 상태 금지, 호출마다 active Scene 확인 |
| **SDF 폰트가 트랙 T를 삼킨다** | T0을 "아틀라스 1종 · 라틴+한글 상용 범위"로 좁히고, 나머지는 T 이후 별건 |
| **게이트가 이 기계에서만 돈다** | D-3 — 신규 게이트는 CLI 저작본만 사용 |
| **신규 단정이 첫 실행부터 전부 초록** | U0-c의 변이 검사를 완료 조건에 넣었다 |
| 해상도 검증이 RHI 결함에 막힘 | §5.5에 선행 의존으로 명시. 2/7 상태를 **래칫**으로 고정해 후퇴만 막는다 |

---

## 14. 명시적 비범위

- Unreal 또는 Unity의 내부 구현을 그대로 복제하는 것
- RmlUi/Slate/uGUI 런타임을 외부 의존성으로 도입하는 것
- SceneGraph와 별도의 UI object tree를 만드는 것
- 범용 ECS/ComponentHandle 전환을 UI 개편의 선행 조건으로 삼는 것
- 존재하지 않는 과거 UI 자산의 대규모 자동 변환
- 기존 native game script의 호환, 복원, 빌드 편입
- **RHI swapchain resize 결함(`0x0000087D`)의 수정** — 선행 의존이지 이 계획의 works가 아니다
- 리치 텍스트(마크업·인라인 이미지)와 텍스트 입력 위젯 — 트랙 T 완료 이후 별건
