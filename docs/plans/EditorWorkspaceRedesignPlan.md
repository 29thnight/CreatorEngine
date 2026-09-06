# 에디터 워크스페이스 · 도킹 · ViewportHost 재설계 (PHASE 21)

- 수립일: 2026-08-24
- 사전 정찰 갱신: 2026-08-30 (§1 기준선 전수 재실측 · §3.2/§7.1/§8.3/§9/§11/§12 정정)
- 상태: 계획 수립 · 구현 미착수
- 방향: **Dear ImGui 유지 · S&Box 테마 토큰 이식 · 소수 전용 위젯만 custom draw**
범위: Editor chrome, theme, tool window, docking, workspace, ViewportHost, Play 표시·입력 전환, 회귀 검증

관련 정본:

- [RefactoringPlanDashboard.html](../RefactoringPlanDashboard.html) — PHASE 21 진행 상태
- [EngineLayerSeparationPlan.md](EngineLayerSeparationPlan.md) — EditorUI와 Host/RHI 경계
- [MultiCameraRenderPlan.md](archive/MultiCameraRenderPlan.md) — Editor/Game 표시 타깃과 동시 표시 기반
- [RenderSceneViewPlan.md](archive/RenderSceneViewPlan.md) — RenderScene/RenderView와 기즈모 뷰 경계
- [SceneGraphRedesignPlan.md](archive/SceneGraphRedesignPlan.md) — `HierarchyStore` 단독 정본
- [UISystemRedesignPlan.md](UISystemRedesignPlan.md) — 게임 UI Runtime 정본. 이 계획과 합치지 않는다

외부 참고:

- [S&Box 공개 theme.json](https://github.com/Facepunch/sbox-public/blob/master/game/addons/tools/assets/styles/theme.json)
- [S&Box qtabbar.css](https://github.com/Facepunch/sbox-public/blob/master/game/addons/tools/assets/styles/qtabbar.css)
- [S&Box qtreeview.css](https://github.com/Facepunch/sbox-public/blob/master/game/addons/tools/assets/styles/qtreeview.css)
- [S&Box qscrollbar.css](https://github.com/Facepunch/sbox-public/blob/master/game/addons/tools/assets/styles/qscrollbar.css)
- [S&Box 2026-07-15 docking/central widget 설명](https://sbox.game/news/update-26-07-15)

---

## 0. 결정 요약

1. **Dear ImGui를 유지한다.** Qt, Qt Advanced Docking System, RmlUi, 웹 UI를 에디터
   chrome에 새로 넣지 않는다.
2. S&Box의 Qt 구현을 복제하지 않고, 공개된 **색·크기·서체 토큰과 레이아웃 원칙**을
   CreatorEngine의 `EditorTheme` 정본으로 옮긴다.
3. ImGui core를 fork하지 않는다. 공개 API를 기본으로 사용하고, 중앙 dock node의 rect처럼
   불가피한 내부 API는 `EditorDockInternals` 한 파일에 격리해 버전 게이트를 둔다.
4. 기본 배치는 S&Box식 **고정 중앙 `ViewportHost` + 주변 자유 도킹 ToolPanel**이다.
   현재처럼 Scene/Game 두 뷰와 Hierarchy/Browser/Inspector의 위치를 항상 고정하지 않는다.
5. Play를 누르면 중앙 `ViewportHost`가 `Editor` 표시 타깃에서 `Game` 표시 타깃으로
   전환한다. Stop은 Editor 타깃과 이전 편집 문서·focus를 복원한다.
6. Scene/Game 동시 비교는 없애지 않는다. 필요할 때 여는 **선택적 `Game Preview` ToolPanel**로
   보존한다.
7. custom draw는 Inspector section/property row, 축 필드, toolbar/mode button 등 소수의
   고밀도 위젯에만 쓴다. Tree, input, menu, popup, docking 자체는 ImGui 표준 동작을 유지한다.
8. 테마·도킹만으로 성능 이득을 주장하지 않는다. 성능 이득 후보는 보이지 않는 view의
   렌더 수요 억제와 Hierarchy/Browser clipping이며, W0 기준선과 W7/W8 실측으로만 판정한다.

이 계획이 따라가는 것은 S&Box 스크린샷의 고정 좌표가 아니라 다음 **제약 경계**다.

```text
Main editor OS window
└─ Editor shell
   ├─ Menu / play controls / status
   └─ DockSpace
      ├─ Central node: ViewportHost (항상 존재, 닫기·외부 도킹 불가)
      │  ├─ Scene/Prefab document tabs
      │  ├─ Scene canvas 또는 Game canvas
      │  └─ viewport toolbar / gizmo overlay
      └─ Tool panels (이동·분할·탭·닫기·다시 열기 가능)
         ├─ Hierarchy
         ├─ Inspector
         ├─ Content Browser
         ├─ Console / Profiler
         └─ optional Game Preview
```

---

## 1. 현재 소스 기준선 (2026-08-30 실측)

수립 당시의 기준선을 2026-08-30에 소스로 전수 대조했다. **이 절에서 "이미"라고 적은 것은
가설이 아니라 현재 트리에서 확인된 사실이고, 파일:행을 붙였다.** 정찰이 뒤집은 항목은
§1.4·§1.6·§3.2·§1.9 넷이다.

### 1.1 스타일은 한 함수에 있으나 semantic token이 아니다

`EngineGUIWindow/EditorRenderer.cpp`의 `ApplyEditorStyle`은 색과 spacing을 직접
`ImGuiStyle`에 쓴다.

- `WindowPadding=(5,5)`, `FramePadding=(5,5)`, `ItemSpacing=(12,8)`
- `WindowRounding=5`, `FrameRounding=4`, `ScrollbarSize=15`
- `WindowBg`는 약 `#383838`, `FrameBg`는 약 `#282828`
- font는 `C:\Windows\Fonts\Verdana.ttf` 16px을 하드코딩하고 Font Awesome을 병합한다.
- 각 창에서 `PushStyleColor/Var`로 흰 popup, 검은 text 등 예외가 다시 덮여 있다.

따라서 색을 바꾸는 것 자체는 쉽지만, 지금 상태로는 “selected surface”, “panel chrome”,
“property label” 같은 의미가 없고 창별 예외가 다시 스타일을 갈라 놓는다.

창별 예외의 규모는 실측으로 `PushStyleColor` 48건 + `PushStyleVar` 43건 = **91건**이고,
그중 48건이 `MenuBarWindow.cpp` 한 파일에 몰려 있다. W1 inventory의 작업량은 이 값을 쓴다.

스케일 경로에도 두 문제가 있다. `EditorRenderer::ApplyEditorScale`은 `style.ScaleAllSizes(scale)`와
`io.FontGlobalScale = scale`을 함께 쓰는데,

- 배율의 출처는 `EditorPreferences::GetImGuiScale()` **사용자 설정 하나**뿐이고 DPI 항이 없다(§3.2).
- `io.FontGlobalScale`은 vcpkg가 물린 **ImGui 1.92.8에서 obsolete**다
  (`imgui.h:2726`, `#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS` 블록. 1.92부터 `style.FontScaleMain`).

즉 W1은 “새 스케일 함수를 만든다”가 아니라 **obsolete API에서 1.92의 정식 경로로 이주한다**가 맞다.

### 1.2 기본 배치가 표시 방식과 결합돼 있다

`EditorRenderer::BuildInitialDockLayout`은 `ContentsBrowserStyle`을 보고 dock tree 전체를
두 종류로 다시 만든다.

- Scene과 Game을 왼쪽 50% 영역에서 위아래 50:50으로 고정한다.
- `Tree` 분기만 오른쪽을 Hierarchy/AssetBundle/Content Browser/Inspector로 4분할한다.
- `ContentsBrowserStyle::Tree/Tile`이라는 **패널 내부 표시 방식**이 전체 workspace topology를
  결정한다.
- 초기 layout은 `imgui.ini`가 없을 때 한 번만 생성된다.
  (`PathFinder::ConfigPath("imgui.ini")`. `ImGuiHost.cpp:37`의 `io.IniFilename`과 같은 경로다.)

두 분기는 대칭이 아니다. **`Tile` 분기에는 AssetBundle과 Content Browser의
`DockBuilderDockWindow` 지정이 아예 없다** — Hierarchy와 Inspector만 배치한다. 즉 Tile로
처음 뜬 사용자는 두 창을 배치받지 못한 채 시작한다. 이것도 §1.4와 함께 W3에서 청산한다.

표시 방식과 창 배치를 분리해야 한다. Tile↔Tree 변경은 Browser 내용만 바꾸며 dock tree를
재구축하지 않아야 한다.

### 1.3 핵심 ToolPanel이 사실상 잠겨 있다

다음 창은 `ImGuiWindowFlags_NoMove`를 가진다.

- `SceneViewWindow.cpp`의 Scene
- `GameViewWindow.cpp`의 Game
- `HierarchyWindow.cpp`의 Hierarchy
- `InspectorWindow.cpp`의 Inspector

루트 DockSpace host의 `NoMove/NoResize`는 맞지만 ToolPanel의 `NoMove`는 자유 도킹을 막는다.
또한 `ImGuiRenderContext`는 기본 flag가 `AlwaysAutoResize`이고 popup일 때만 `Begin`에
`&m_opened`를 넘긴다. 일반 panel에는 tab 닫기와 Window 메뉴를 통한 재열기 계약이 없다.

실측이 더한 것 넷.

1. `SceneViewWindow.cpp:192`의 `gizmoWindowFlags`는 **`static`인데 212행이 `|=`로 `NoMove`를
   누적한다.** 초기값에 이미 `NoMove`가 있어 그 줄은 아무 일도 하지 않는 죽은 코드다. W3에서
   `NoMove`를 걷을 때 212행을 남기면 hover 한 번에 다시 잠기므로 함께 지운다.
2. Content Browser는 `isPopup=true`로 등록돼(`ContentsBrowserWindow.cpp:108`) 이미 `&m_opened`
   경로를 타고, `GetContext(kBrowserTitle).Open()/Close()`도 있다. 즉 "close/reopen 계약이 전무"가
   아니라 **popup 우회로 한 창만 갖고 있다.** W3는 이 우회를 role 계약으로 승격시켜 흡수한다.
3. 계획서가 ToolPanel로 열거한 **Console·Profiler는 독립 창이 아니다.** `MenuBarWindow.cpp`(2698줄)
   안에서 `m_bShow*` bool 9종으로 직접 `Begin`한다 — Log, FrameProfiler, Behavior Tree Editor,
   BlackBoard Editor, InputActionMaps, Build Scene Setting, RenderPass Debug 등. W3의 대상 창 수는
   §4.3이 나열한 6종이 아니라 **여기 9종 + ContextRegister 10종 + Scene/Game 직접 Begin 2종**이다.
4. `ImGuiRegister::m_contexts`가 `unordered_map`이라 **창 렌더 순서가 비결정적**이다. ImGui의
   Begin/End 자체는 순서에 관대하지만 focus 획득과 draw list 순서는 영향을 받으므로, W0의 visual
   golden을 뜨기 전에 순회를 결정적 컨테이너로 바꾼다(빈 골든보다 흔들리는 골든이 더 나쁘다).

### 1.4 표시 문자열이 persistence ID다 — 그리고 **이미 끊겼다**

`DockBuilderDockWindow`와 `ImGui::Begin`이 아이콘, 공백, 표시 이름을 포함한 문자열을 그대로
ID로 쓴다. 예를 들어 Scene/Game 이름의 뒤쪽 정렬 공백까지 같아야 한다.

이것은 장래의 위험이 아니라 **현재 트리에서 이미 실현된 결함**이다. `DockBuilderDockWindow`가
지정하는 8개 이름과 실제 `Begin`/`ContextRegister` 이름을 전수 대조하면 일곱은 일치하고 하나가
어긋난다.

| 위치 | 문자열 | 공백 |
|---|---|---:|
| `EditorRenderer.cpp:167` | `ICON_FA_HARD_DRIVE "␣␣Content Browser"` | 2 |
| `ContentsBrowserWindow.cpp:103` | `ICON_FA_HARD_DRIVE "␣Content Browser"` | 1 |

Content Browser는 초기 도크 배치에 **절대 매칭되지 않는다.** 증거는 실물 ini에 남아 있다 —
`Bin/x64-Release/Editor/Saved/Config/imgui.ini`에 Content Browser entry가 33행·53행 **두 개**로
갈려 있고, 하나는 dock node에 붙고 하나는 떠돌이다. 아이콘·간격이 ID라는 계약의 대가를
이미 치르고 있다.

또한 `imgui.ini`가 트리에 **4벌** 있다. 정본은 `Bin/x64-*/Editor/Saved/Config/`이고, 저장소
루트의 `./imgui.ini`(8-09)와 `Dynamic_CPP/Assets/Scenes/imgui.ini`(8-07)는 CWD 기준으로 저장하던
시절의 유물이다. W3 migration fixture는 이 실물 넷을 그대로 쓴다 — 합성 fixture는 이 분열을
재현하지 못한다.

모든 persistent window는 다음 규칙으로 바꾼다.

```text
표시 이름###고정 ID

Hierarchy###Editor.Tool.Hierarchy
Inspector###Editor.Tool.Inspector
Content Browser###Editor.Tool.ContentBrowser
Game Preview###Editor.Tool.GamePreview
Viewport###Editor.Central.ViewportHost
```

### 1.5 Scene과 Game은 이미 서로 다른 표시 타깃이다

- SceneView는 `EnhancedLiveDisplayTarget::Editor`의 texture를 표시한다.
- GameView는 `EnhancedLiveDisplayTarget::Game`의 snapshot/texture를 표시한다.
- 두 texture는 ImGui가 이해하는 불투명 presentation key로 소비된다.

따라서 중앙 전환 때문에 renderer나 RHI를 교체할 필요는 없다. 다만 SceneView 함수 안에는
texture 표시뿐 아니라 ImGuizmo, camera movement, picking, overlay toolbar가 섞여 있다.
`ViewportHost`가 texture ID만 바꾸면 Game 화면 위에 편집 입력과 기즈모가 남는다.

실측으로 드러난 더 성가신 사실은 **두 캔버스의 좌표·스케일 규약이 서로 다르다**는 것이다.
texture ID 교체는 이 차이를 흡수하지 못한다.

| | Scene (`SceneViewWindow.cpp:199~`) | Game (`GameViewWindow.cpp:10~`) |
|---|---|---|
| 이미지 크기 | `GetWindowWidth/Height` — **창 전체**(타이틀바·패딩 미공제) | content region에 종횡비 letterbox |
| 종횡비 | 늘림(stretch) | `ScreenResizeBus::GetAspectRatio()` 보존 |
| 좌표 보정 | `ImGuizmo::SetRect`에 `titleBarHeight`를 **수동 가산** | 없음 |
| not-ready | `##EnhancedRendererPending` 어두운 사각형 | `active`=false면 "No Camera rendering", ready 아니면 pending 사각형 |
| 공통 | 둘 다 `BringWindowToDisplayBack`을 매 프레임 강제 호출 | ← 자유 도킹과 충돌한다 |

즉 W4의 `ViewportCanvas`는 "letterbox냐 stretch냐"와 "rect의 원점이 창이냐 content냐"를 **먼저
하나로 정하고** 그 규약을 ImGuizmo·picking이 같은 출처에서 읽게 해야 한다. `BringWindowToDisplayBack`
두 곳은 central node가 생기면 불필요해지므로 W4에서 제거한다.

한편 Game의 `active`/`ready` 2단 신호는 §6.2가 요구하는 "pending background"의 절반이 **이미
구현돼 있다**는 뜻이다 — W4는 이것을 Scene 쪽과 통일해 재사용한다.

목표 분리는 다음과 같다.

```text
ViewportHost
  ├─ ViewportCanvas(target, extent)
  ├─ SceneInteraction(picking, camera, selection)
  ├─ SceneOverlay(toolbar, gizmo, view cube)
  └─ GameInputSurface(focus, cursor, capture)
```

### 1.6 Play 전이는 즉시 완료되지 않는다

메뉴 버튼은 `SceneManager::SetGameStart`로 요청을 세우지만, 실제 Begin/End transaction은
프레임 경계의 pending scene-structure change에서 처리된다.

- enter `PlayModeEvent(true)`는 scene snapshot capture보다 먼저 발생한다.
- exit `PlayModeEvent(false)`는 scene restore 뒤에 발생한다.
- 현재 `Editor::PlayModeController`는 enter 때 Undo만 비운다.

따라서 UI가 버튼 click이나 enter event 하나만 보고 곧바로 “Playing 성공”으로 확정하면 안 된다.
`ViewportModeController`는 requested/pending/committed 상태를 구분하고, authoritative play state와
simulation phase가 확정된 뒤 Game canvas와 game input을 활성화한다.

**실측: 이 어긋남은 가능성이 아니라 현재 열려 있는 경로다.**

- `SceneManager::SetGameStart`는 `m_isGameStart`(`atomic_bool`)를 **즉시** true로 세운다
  (`SceneManager.cpp:219`).
- 프레임 경계의 `BeginPlayTransaction`은 `PlayModeEvent.Broadcast(true)`를 먼저 던진 뒤
  `CaptureSceneSnapshot()`이 실패하면 **`SetSimulationPhase(Simulating)`에 닿지 못하고 조기
  return한다**(`SceneManager.cpp:1427`). 이때 `m_isGameStart`를 되돌리지 않는다.
- Play 버튼은 `SceneManagers->IsGameStart()` **단일 값만** 읽는다(`MenuBarWindow.cpp:465`).

따라서 **Stop 아이콘이 뜬 채 시뮬레이션은 진입하지 않은 상태**가 실재한다. 게다가 씬 수준
play state의 정본은 이 `atomic_bool` 하나뿐이다 — `ScenePhase`는 *엔티티*가 놓인 단계
(`Detached→Attached→InScene→Simulating`)라 씬 수준 상태 머신의 대용이 되지 못한다. W5는
읽을 committed 신호가 없는 상태에서 시작한다는 뜻이고, 그 신호를 만드는 것이 W5의 첫 작업이다.

부수 실측 둘.

- `play.state` 콘솔 커맨드가 이미 `gameStart / paused / editorSceneLoaded / pending
  (HasPendingSceneStructureChange) / entities`를 한 줄로 낸다(`ConsoleCommandSystem.cpp:6015`).
  W5 게이트의 씨앗이며, 여기에 committed(실제 Simulating 진입)와 viewport target·input owner를
  더하면 §11이 요구한 단정이 선다.
- Undo Clear가 **이중 경로**다. `PlayModeController`가 `PlayModeEvent` 구독으로 비우는데
  (`EditorPlayModeController.cpp:22`), Play 버튼도 클릭 자리에서 `ClearGameMode()`를 직접 부른다
  (`MenuBarWindow.cpp:467`). W5에서 UI 쪽 호출을 걷어 controller 단일 소유로 만든다.

### 1.7 OS multi-viewport는 아직 지원 계약이 없다

현재 ImGui는 Docking과 keyboard navigation을 켜지만 `ImGuiConfigFlags_ViewportsEnable`은 켜지
않는다. `PlatformHasViewports`만 설정돼 있고 renderer backend의 다중 OS window swapchain,
resize, DPI, present 수명 검증은 없다.

PHASE 21의 완료 기준은 **하나의 main OS window 안에서 자유 docking**이다. ToolPanel을 별도
OS window로 떼는 기능은 DX12/Vulkan presentation backend 계약과 양쪽 runtime 검증이 선 뒤의
별도 후속이다.

### 1.8 입력 소유권 신호가 매 프레임 덮여 있다

`ImGuiHost::BeginFrame`이 `ImGui::NewFrame()` 직전에 다음을 한다(`ImGuiHost.cpp:113~118`).

```cpp
io.WantCaptureKeyboard = io.WantCaptureMouse = io.WantTextInput = true;
io.ConfigFlags  |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseCursors;
```

`WantCapture*`는 ImGui가 **출력**하는 값이다. 프레임마다 true로 강제해 두면 "지금 입력을 UI가
가져갔는가"를 밖에서 물어볼 수단이 사라진다. W5가 세울 input owner 불변식은 이 신호를 읽어야
하므로, **W5의 선행 작업으로 이 세 줄을 걷고** game input 라우팅이 무엇을 근거로 판단하는지
먼저 확정한다(강제를 걷는 순간 라우팅 동작이 바뀔 수 있으므로 지혈이 아니라 계측이 먼저다).

flag 두 줄도 초기화가 아니라 매 프레임 OR이라 **런타임에 끌 수 없다.** `ViewportsEnable`은
§1.7대로 꺼져 있지만 `EndFrame`에는 이미 `UpdatePlatformWindows/RenderPlatformWindowsDefault`
분기가 서 있다(`ImGuiHost.cpp:135`) — 죽은 분기이므로 이번 범위에서 켜지 않되, W8의 canary가
"켜지지 않았음"을 단정한다.

커서 쪽은 `ImGuiConfigFlags_NoMouseCursorChange`와 `ImGuiBackendFlags_HasMouseCursors`가 동시에
서 있다. ImGui가 OS 커서를 바꾸지 않는다는 뜻이므로, §6.2의 "cursor lock/clip/visibility는
ViewportHost가 소유한다"는 **경쟁자가 없다**는 점에서 유리하고, 동시에 **아무도 복구해 주지
않는다**는 점에서 해제 누락이 그대로 남는다.

### 1.9 에디터 chrome을 밖에서 볼 수단이 없다

W0의 canary와 §11의 자동화는 전부 "밖에서 layout·target·input owner를 단정한다"를 전제한다.
그런데 콘솔 커맨드 표면을 전수 조사하면 네임스페이스 46종 중 **`editor.` / `imgui.` / `layout.` /
`dock.`이 하나도 없다.** `ui.*` 8종은 게임 UI(Canvas/RectTransform) 대상이라 에디터 chrome과
무관하고, 에디터 쪽에 있는 것은 `window.resize` / `window.info`(OS 클라이언트 크기)와
`play.state`뿐이다.

이것은 [EditorAutomationCLIPlan.md](EditorAutomationCLIPlan.md)가 남긴 교훈과 같은 자리다 —
관측·설정 커맨드가 있다고 해서 **그 표면을 저작·검사할 수 있는 것은 아니다.** 따라서 W0는
"측정하고 screenshot을 찍는다"만으로 끝나지 않고, 다음 관측 커맨드 신설을 포함한다.

```text
editor.layout        활성 workspace·preset·schema version
editor.windows       persistent window별 stable ID / open / docked node / rect
editor.dock          central node 존재·rect·유일성
editor.theme         token sample(hex)과 적용 style 값
editor.viewport      현재 mode·display target·input owner·cursor state
```

이 다섯이 없으면 `verify-editor-workspace.ps1`은 "창이 떴다"밖에 단정하지 못한다. §9의 W0
추정은 이 몫을 포함해 재산출했다.

### 1.10 재사용 가능한 기존 자산과 편집 위험

- **custom 위젯 자산이 이미 있다.** `Editor/ImGuiHelper/`에 `CustomCollapsingHeader.h`,
  `ToggleUI.h`, `TableAPIHelper.h`, `HorizontalLayout.h`, `widgets.{h,cpp}`, `drawing.{h,cpp}`,
  `BlueprintBuilder.{h,cpp}`, `NodeEditor.{h,cpp}`가 있다. §7.1의 4 family는 백지 신설이 아니라
  이 자산들과의 **승계·은퇴 관계를 먼저 정해야** 한다(§7.1에 결정표를 추가했다).
- `IconsFontAwesome4.h`와 `IconsFontAwesome6.h`가 공존한다. W1의 icon 토큰 정리 대상이다.
- **비-UTF8 소스 9개가 W1/W2 편집 사정권**에 있다: `AssetBundleWindow.cpp`,
  `DrawYamlNodeEditor.cpp`, `ICustomEditor.h`, `CustomCollapsingHeader.h`, `HorizontalLayout.h`,
  `NodeEditor.{h,cpp}`, `TableAPIHelper.h`, `ToggleUI.h`. 텍스트 편집으로 한 줄만 고쳐도 무관한
  주석이 깨진 이력이 있으므로, 이 파일들은 편집 전에 인코딩을 먼저 정리한다.
- `ImGuiContext.h`가 은퇴한 `imgui_impl_dx11.h`를 아직 include한다. W2에서 함께 걷는다.
- `ImGuiRegister.h`는 헤더 스스로 `#define EDITOR`를 하고 `#if defined(EDITOR)`로 갈라 놓아
  else 분기 전체가 죽은 코드다. W3에서 정리한다.

---

## 2. 범위와 비범위

### 2.1 이번 계획이 해결하는 것

- S&Box 계열의 dark editor theme와 density
- stable window ID와 ToolPanel open/close 정책
- 중앙 `ViewportHost`와 주변 자유 docking
- named workspace, preset, save/load/reset, schema migration
- Edit/Play/Pause/Eject의 중앙 표시·입력·overlay 전환
- 선택적 Game Preview와 visible-view demand
- Hierarchy/Browser의 큰 데이터 표시 비용
- DX12/Vulkan, DPI, layout, Play 왕복, 시각 회귀

### 2.2 의도적으로 하지 않는 것

- Qt/ADS 또는 다른 retained GUI framework 도입
- ImGui core fork
- S&Box의 코드, Qt stylesheet, docking animation을 그대로 복제
- 게임 UI의 Canvas/InputRouter/UIDrawSnapshot 재설계 — PHASE 16 정본
- Scene hierarchy의 두 번째 소유 저장소 — `HierarchyStore`가 계속 단독 정본
- Play snapshot/simulation transaction 소유권을 EditorUI로 이동
- 이 페이즈의 완료 조건으로 OS multi-viewport, pin/auto-hide, remote editor를 요구
- 모든 ImGui 위젯을 자체 wrapper/custom draw로 재작성

---

## 3. S&Box 참고 범위와 CreatorEngine 토큰

### 3.1 외부 값을 runtime 의존성으로 만들지 않는다

S&Box의 공개 `theme.json`은 디자인 입력이다. 에디터가 해당 파일이나 인터넷을 runtime에
읽지 않는다. 아래 값을 CreatorEngine semantic token으로 옮기고 출처·이식일·의도적 차이를
문서화한다.

| CreatorEngine token | S&Box 참고 값 | 용도 |
|---|---:|---|
| `Canvas` | `#181818` | 최하위 window/control 배경 |
| `Chrome` | `#2A2A2A` | menu, tab bar, sidebar, status bar |
| `Panel` | `#343434` | panel surface, selected tab surface |
| `PanelRaised` | `#484848` | hover/raised surface |
| `Selection` | `#525252` | row/property selection |
| `Border` | `#3E3E3E` | 기본 경계 |
| `BorderStrong` | `#484848` | focus/raised 경계 |
| `Primary` | `#2E70EA` | active tab, focus, link, play state |
| `Text` | `#FFFFFF` | 주요 text |
| `TextMuted` | `#9E9E9E` | 보조 label |
| `TextDisabled` | `#999999` | disabled; 실제 대비는 W1에서 판정 |
| `Positive` | `#5AEB5C` | enabled/success |
| `Warning` | `#E6DB74` | warning/folder accent |
| `Error` | `#FB5A5A` | error/destructive action |

geometry token:

| token | 기준값(logical px) | 적용 |
|---|---:|---|
| `RowHeight` | 24 | Tree/목록/Property row |
| `ControlHeight` | 24 | input/button/combobox |
| `ControlRadius` | 4 | frame/button |
| `TabHeight` | 24 | document/tool tab |
| `TabActiveMarker` | 2 | active/focused 표식 |
| `TreeIndent` | 20 | Hierarchy/Tree |
| `ScrollbarWidth` | 8 | panel scrollbar |
| `PanelGap` | 1 | dock splitter/border 시각 간격 |

### 3.2 typography와 DPI

- 기본/heading은 Inter를 Editor resource에 license와 함께 포함하는 것을 1순위로 한다.
- Font Awesome 병합은 유지하되 text font와 icon glyph의 baseline/size token을 분리한다.
- monospace는 Consolas를 1순위로 하되 시스템 font 부재 시 bundled fallback을 쓴다.
- 현재의 절대 Windows font path를 제거한다.
- 모든 geometry는 logical px 하나로 정의하고 `main viewport DPI × user scale`을 한 번만 적용한다.
- font atlas rebuild와 style scaling의 책임을 한 함수로 모아 double scaling을 금지한다.

**정찰 정정 (2026-08-30).** 위 마지막 두 줄은 자체 구현을 지시하지만, 실측 결과 전제가 둘 다
어긋난다.

1. **DPI 항이 지금 0이다.** `EnableDpiAwareness`·`GetDpiForWindow`·`SetProcessDpi*` 호출이
   코드베이스 전체에 없고, `ImGuiHost::BeginFrame`은 `io.DisplayFramebufferScale`을 `(1,1)`로
   하드코딩한다. 유일한 배율은 `EditorPreferences::GetImGuiScale()`이며 UI 슬라이더 범위가
   **0.8~1.5**다(`MenuBarWindow.cpp:451`). 즉 "150% DPI"는 지금 사용자 배율로 흉내만 낼 수 있고
   진짜 DPI 경로를 탄 적이 없다 — W1의 판정에서 이 둘을 구분해 적는다.
2. **ImGui 1.92.8이 이미 정식 경로를 제공한다.** `style.FontScaleMain`(사용자 배율),
   `style.FontScaleDpi`(모니터 contents scale), `io.ConfigDpiScaleFonts`(DPI 변화 시 `FontScaleDpi`
   자동 갱신), `style.FontSizeBase`가 그것이다. 현 코드가 쓰는 `io.FontGlobalScale`은 obsolete다.

따라서 W1의 DPI 항목은 다음으로 대체한다.

- 배율의 정본을 `FontScaleMain`(user scale) × `FontScaleDpi`(monitor DPI) 두 축으로 나눈다.
  자체 곱셈 경로를 새로 만들지 않는다.
- `io.FontGlobalScale` 사용을 제거하고, `ScaleAllSizes`는 **geometry에만** 적용한다
  (`ScaleAllSizes` 주석대로 폰트는 스케일하지 않는다 — 지금의 이중 적용이 double scaling의 원인이다).
- Win32 DPI awareness와 `ConfigDpiScaleFonts` 채택 여부를 W1에서 판정하고, 채택하면
  "`FontScaleDpi`는 ImGui가 쓴다"를 불변식으로 적는다.
- **`IMGUI_DISABLE_OBSOLETE_FUNCTIONS`를 켜는 것을 W1의 종료 조건에 넣는다.** 켜서 빌드가 서면
  obsolete 잔존이 0이라는 증명이 되고, 이후 ImGui 업그레이드에서 조용히 깨지지 않는다.

### 3.3 ImGui style mapping

`EditorTheme`는 `ImGuiStyle`의 정본 adapter를 제공한다.

- `WindowBg=Canvas`, `ChildBg/PopupBg=Panel`
- `MenuBarBg/TabBar=Chrome`
- `FrameBg=Canvas`, hover=`PanelRaised`, active=`Selection`
- `Header=Panel`, hover/active=`Selection`
- `Tab=Chrome`, selected=`Panel`, dimmed=`Chrome`
- `DockingPreview=Primary`, `NavHighlight=Primary`
- scrollbar grab은 `BorderStrong`, active는 `Primary`

panel 내부에서 임의의 literal color를 `PushStyleColor`하는 것은 semantic exception일 때만
허용한다. W1은 현재 override inventory를 만들고, 공통 값은 token으로 수렴시킨다.

---

## 4. Window와 docking 계약

### 4.1 window role

| role | 이동/도킹 | 닫기 | resize | 기본 autosize | 예시 |
|---|---|---|---|---|---|
| `ShellHost` | 불가 | 불가 | OS window를 따름 | 아니오 | Main DockSpace |
| `CentralHost` | 불가 | 불가 | central rect를 따름 | 아니오 | ViewportHost |
| `Document` | host 내부 tab | 문서 정책 | host를 따름 | 아니오 | Scene, Prefab, Behavior Tree |
| `ToolPanel` | 가능 | 가능 | 가능 | 아니오 | Hierarchy, Inspector, Browser |
| `UtilityOverlay` | 불가 | 기능별 | anchor | 내용 기반 | viewport toolbar |
| `Popup/Modal` | 불가 | 가능 | 내용 기반 | 예 | picker, import dialog |

`ImGuiRenderContext`의 `AlwaysAutoResize` 기본값을 없애고 role별 flag를 명시한다. 일반 ToolPanel도
`Begin(..., &open, flags)`를 사용해 tab 닫기와 Window 메뉴 재열기가 같은 상태를 본다.

### 4.2 central node

central node는 다음 불변식을 가진다.

1. 항상 존재한다.
2. 닫을 수 없다.
3. 다른 ToolPanel을 central content 위에 dock해 ViewportHost를 밀어낼 수 없다.
4. Scene/Game은 서로 다른 permanent top-level dock이 아니라 Host의 표시 mode다.
5. Scene/Prefab/graph editor 같은 document는 Host 내부 document tab으로 전환할 수 있다.

구현 1순위는 `NoDockingOverCentralNode`와 중앙 node rect를 사용한 borderless `ViewportHost`다.
Dear ImGui public API만으로 central rect/lock을 완결할 수 없으면 접근은
`EditorDockInternals.{h,cpp}` 하나로 제한한다. 이 adapter는 다음을 가져야 한다.

- 지원 ImGui version 범위와 compile-time assertion
- central node null/invalid 시 safe default layout 복구
- internal struct를 EditorWindow 코드에 노출하지 않는 값 API
- ImGui upgrade 시 깨지는 canary test

### 4.3 ToolPanel 자유도

- Hierarchy, Inspector, Content Browser, Console, Profiler, Game Preview에서 `NoMove`를 제거한다.
- main OS window 안에서 tab, split, floating group을 허용한다.
- minimum size는 panel role별 logical token으로 둔다. hard-coded absolute screen 좌표는 쓰지 않는다.
- focus panel은 `Primary` 1~2px marker 또는 border로 식별한다.
- popup/modal만 `AlwaysAutoResize`를 기본 허용한다.

### 4.4 기본 preset

신규 사용자의 기본값은 `S&Box Compact`다.

```text
┌───────────────────────────────┬─────────────────────┐
│                               │ Hierarchy           │
│                               │ (right upper 45%)   │
│          ViewportHost         ├─────────────────────┤
│          central              │ Inspector           │
│                               │ (right lower 55%)   │
├───────────────────────────────┴─────────────────────┤
│ Content Browser | Console | Profiler  (bottom tabs)│
└─────────────────────────────────────────────────────┘
```

초기 비율은 right 26%, bottom 24%를 출발값으로 하며 pixel 고정값이 아니다. W0에서
1920×1080, 2560×1440, 150% DPI의 최소 central content를 확인해 조정한다.

함께 제공할 preset:

- `Level Editing`: Hierarchy와 Inspector 상시, Browser bottom
- `UI Editing`: Hierarchy/Inspector + UI 관련 panel 우선
- `Rendering & Debug`: Game Preview/RenderPass/Profiler 우선
- `Legacy Unity`: 현재 Scene/Game 2-view와 우측 panel 배치를 재현하는 이행·비교용

preset은 초기값/Reset 대상이지 lock이 아니다. 적용 뒤 사용자가 자유롭게 바꿀 수 있다.

---

## 5. Workspace persistence

### 5.1 저장 단위

`EditorWorkspaceStore`가 다음을 하나의 versioned workspace로 저장한다.

- ImGui dock/window ini blob
- active preset/name
- ToolPanel open state
- central document와 Viewport mode의 편집 상태
- logical DPI bucket과 main window extent
- `workspaceSchemaVersion`, `themeVersion`, `imguiVersion`

저장은 `PathFinder::RuntimeDataPath("Editor/Workspaces")` 아래의 local/untracked 데이터로 둔다.
프로젝트 저작 정본인 `EngineSettings.asset`에는 dock 좌표·개인 창 상태를 쓰지 않는다.

### 5.2 save/load/reset 규칙

- frame의 모든 `End`가 끝난 안전 지점에서 save/load를 수행한다.
- candidate file → flush → atomic replace 순서로 저장한다.
- layout load 실패, central node 부재, off-screen rect, 0 크기는 실패로 기록하고 기본 preset으로
  fail-close한다.
- `Reset Layout`은 preset 선택 dialog를 띄우고 현재 workspace를 backup한 뒤 적용한다.
- 자동 migration은 사용자 layout을 조용히 덮어쓰지 않는다.

### 5.3 legacy migration

1. 기존 `imgui.ini`를 `.pre-workspace-v1` backup으로 보존한다.
2. known legacy title을 stable ID로 바꾸는 migration table을 둔다.
3. 알려지지 않은 창 entry는 삭제하지 않는다.
4. migration canary는 아이콘/공백이 든 현재 Scene/Game/Hierarchy/Inspector 이름을 fixture로 쓴다.
5. 실패하면 기존 파일을 보존하고 `Legacy Unity` preset을 제안한다.

---

## 6. ViewportHost와 Play UX

### 6.1 상태 모델

```text
Editing
  target: Editor
  scene input/gizmo/picking: on
  game input/cursor capture: off

EnteringPlay (pending)
  target: pending surface
  editor mutation: off
  game input: off until transaction committed

PlayingPossessed
  target: Game
  scene input/gizmo/picking: off
  game input: focused/hovered content가 소유

PlayingEjected
  simulation: 계속 진행
  target: Editor
  scene camera/picking: on, game input: off

ExitingPlay (pending)
  target: pending surface
  cursor capture release

Editing (restored)
  target: Editor
  prior document/focus/selection policy 복원
```

Pause는 `PlayingPossessed/Ejected`에 직교하는 상태다. Pause 때문에 target이나 input owner를
임의로 바꾸지 않는다.

### 6.2 전이 원칙

- Play button은 요청만 한다. transaction commit 전에는 `PlayingPossessed`로 확정하지 않는다.
- Game texture가 아직 ready가 아니면 명시적인 pending background를 표시한다. Editor texture로
  fallback해 한 프레임 잘못된 기즈모/입력을 보여주지 않는다.
- Stop은 scene restore 뒤 Editor canvas로 돌아간다.
- cursor lock/clip/visibility는 ViewportHost가 소유하며 focus loss, Eject, Stop, crash-safe shutdown에서
  반드시 해제한다.
- Play transaction과 scene snapshot은 계속 SceneManager가 소유한다. Editor controller는 관찰하고
  표시·입력 정책만 적용한다.

### 6.3 선택적 Game Preview

`Game Preview`는 일반 ToolPanel이다.

- 닫혀 있으면 central mode가 필요한 target만 요청한다.
- 열려 있으면 central Editor와 Game Preview Game을 동시에 볼 수 있다.
- `MultiCameraRenderPlan`의 두 target 기반을 재사용하며 별도 카메라 시스템을 만들지 않는다.
- panel이 보이지 않거나 collapsed/minimized면 view demand에서 제외한다.

### 6.4 viewport extent와 render demand

W4는 두 단계로 구현한다.

1. 기존 shared display texture를 중앙 content rect에 표시해 UX를 먼저 완결한다.
2. 이후 immutable `EditorViewDemandSnapshot`으로 visible target과 requested pixel extent를 frame
   boundary에 publish한다. renderer는 ImGui window를 직접 읽지 않는다.

resize thrash를 막기 위해 extent 변화에는 debounce/quantization과 generation을 둔다. DX12/Vulkan
양쪽에서 target resize, old presentation key retire, 0-size/minimized를 검증하기 전에는 renderer
target 크기를 central rect에 직접 묶지 않는다.

---

## 7. 소수 custom widget 정책

### 7.1 허용 목록

초기 custom draw surface는 네 family로 제한한다.

1. `EditorSectionHeader` — Inspector component header, enable toggle, fold, context menu
2. `EditorPropertyRow` — 고정 label column, mixed/disabled/error 상태
3. `EditorAxisField3` — X/Y/Z 색 badge와 compact numeric input
4. `EditorModeButton` — viewport toolbar와 Play/Pause/Eject의 flat icon/active marker

표준 Button, Checkbox, TreeNode, InputText, Combo, Menu, Tooltip, Popup은 theme token을 입힌 ImGui
widget을 그대로 쓴다. custom widget은 ImGui ID, nav, focus, disabled, clipping, tooltip, testability를
보존해야 하며 별도 input framework를 만들지 않는다.

**정찰 정정 — 넷은 백지 신설이 아니다.** `Editor/ImGuiHelper/`에 기존 자산이 있고(§1.10), 그
관계를 먼저 정하지 않으면 같은 역할의 위젯이 두 벌 남는다. W2의 첫 산출물은 이 표를 확정하는
것이다.

| 신규 family | 기존 자산 | 초기 판정 |
|---|---|---|
| `EditorSectionHeader` | `CustomCollapsingHeader.h` | **승계** — 기존 구현을 토큰화해 개명. 신규 작성 아님 |
| `EditorPropertyRow` | `TableAPIHelper.h`, `HorizontalLayout.h` | **부분 승계** — label column 규약만 흡수, 나머지는 존치 판정 후 결정 |
| `EditorAxisField3` | `ImGuiDrawHelperRectTransformComponent.cpp`의 축 필드 | **승격** — 창 안에 흩어진 구현을 정본으로 끌어올린다 |
| `EditorModeButton` | `ToggleUI.h`, `widgets.{h,cpp}` | **판정 필요** — `ToggleUI`의 소비자를 세고 겹치면 승계, 아니면 신설 후 은퇴 |
| — | `drawing.{h,cpp}`, `BlueprintBuilder`, `NodeEditor` | **범위 밖** — node editor 전용. 건드리지 않는다 |

판정의 근거는 소비자 수다. 소비자를 셀 때 파일 이름 부분 문자열로 세지 않는다 — 경계 없는
매치가 "소비자 0" 오판을 낸 이력이 있다. 은퇴 대상은 소비자를 실제로 끊어 본 뒤에 지운다.

### 7.2 tab과 scrollbar의 한계

Dock tab/scrollbar는 우선 `ImGuiStyle`의 색·크기·rounding으로 맞춘다. active tab의 2px marker처럼
public style로 불가능한 차이는 다음 순서로 판정한다.

1. content edge/focus border로 같은 정보 계층을 표현할 수 있으면 그것을 채택한다.
2. 시각 목표에 필수면 `EditorDockInternals`의 좁은 overlay draw로 구현한다.
3. ImGui fork나 tab bar 전체 재구현이 필요하면 기각하고 의도적 차이로 기록한다.

목표는 Qt를 픽셀 단위로 위장하는 것이 아니라, 색·density·focus hierarchy·panel rhythm이 같은
CreatorEngine editor다.

---

## 8. 성능 계약

### 8.1 기대치를 분리한다

| 변경 | 예상 | 판정 방법 |
|---|---|---|
| theme token/style | 대체로 중립 | Editor UI CPU, draw vertices/indices 비교 |
| custom draw 4종 | 작은 CPU/vertex 증가 가능 | widget별 micro scene과 steady-state alloc |
| 자유 docking | 중립 | 같은 panel 조합의 p50/p95 비교 |
| central 단일 view demand | GPU/CPU 감소 가능 | Editor+Game 동시 vs 1 target pass/submit/GPU ms |
| Hierarchy/Browser clipping | 큰 목록에서 CPU 감소 가능 | 1k/10k/50k fixture |

### 8.2 hard gate

- theme/W2 적용만으로 editor chrome p95 CPU가 기준선 대비
  `max(0.10 ms, 5%)` 이상 악화되면 원인을 기록하고 최적화 또는 rollback한다.
- warm-up 뒤 정적인 shell/custom widget 경로의 frame당 heap allocation은 0을 목표로 한다.
- custom draw의 vertex/index 수, draw command 수를 W0 기준선과 함께 기록한다.
- central에서 Game만 보이고 Game Preview가 닫혔을 때 Editor target을 계속 생산하면 “single view
  최적화 완료”로 판정하지 않는다.
- view demand 최적화의 목표 수치는 W0의 GPU capture 뒤 정하며 추정치로 완료 처리하지 않는다.

### 8.3 Hierarchy/Browser

- `ImGuiListClipper` 또는 동등한 visible-row clipping을 사용한다.
  **정찰: 현재 `ImGuiListClipper` 사용은 코드베이스 전체에서 0건이다** — 전량 신규다.
- **순서 제약: flatten cache가 clipping의 선행조건이다.** Hierarchy는 `TreeNodeEx` 재귀로
  그려지고(`HierarchyWindow.cpp:494, 585, 610`) 자식은 `GetChildrenIndices()`로 내려간다.
  clipper는 "인덱스로 접근 가능한 평탄 목록"을 요구하므로, `EntityHandle + depth + expanded`의
  파생 목록을 먼저 만들지 않으면 clipper를 끼울 자리 자체가 없다. W7 안에서 cache는 clipping과
  병렬 항목이 아니라 그 앞 단계다.
- 현재 Hierarchy는 `scene->m_Entities`를 인덱스로 두 번 순회한다(`HierarchyWindow.cpp:395, 406`).
  flatten cache를 만들 때 이 경로가 `HierarchyStore` 정본과 어긋나지 않는지 W7 착수 시 먼저 확인한다.
- Hierarchy presentation cache는 `EntityHandle + depth + expanded state` 같은 파생값만 가진다.
- parent/children/occupied의 정본을 복제하지 않는다.
- `HierarchyStore` mutation/revision 또는 명시적 scene event로 cache를 무효화한다.
- invalidation 근거가 없으면 매 frame 전체 cache를 믿지 말고 fail-safe rebuild한다.

---

## 9. 실행 계획

모든 상태는 최초 `todo`다. 문서 작성은 구현 진행으로 세지 않는다. 총 초기 추정은 23일이었고,
정찰 뒤 **25일**로 조정했다(W0 +1, W5 +1. 근거는 각 슬라이스에 적었다).

### W0 — 관측 표면 · 기준선 · 실패 게이트 (P0, 2일 · 정찰 뒤 1일→2일)

**추정 정정 근거:** §1.9대로 에디터 chrome을 밖에서 볼 CLI 표면이 0이다. 관측 커맨드를 만들지
않으면 canary가 "창이 떴다"밖에 단정하지 못한다. 원래의 1일은 측정·캡처만 센 값이다.

- `editor.layout` / `editor.windows` / `editor.dock` / `editor.theme` / `editor.viewport` 관측
  커맨드를 신설한다(§1.9). 이번 슬라이스에서는 **관측만** 하고 설정·저작은 W3/W6에 둔다.
- `ImGuiRegister`의 창 순회를 결정적 순서로 바꾼다(§1.3-4). golden을 뜨기 **전에** 한다.
- 현재 `imgui.ini` 4벌을 fixture로 고정한다(§1.4) — 정본 2, 유물 2. Content Browser 이중 entry가
  들어 있는 실물을 그대로 쓴다.
- 핵심 window title/flags, open state inventory를 고정한다. `PushStyleColor/Var` 91건의 위치
  목록을 W1 입력으로 남긴다.
- 1920×1080/2560×1440, user scale 100%/150%의 shell screenshot을 캡처한다. **DPI 경로가 없으므로
  (§3.2) 이 캡처는 "user scale"임을 명시하고, 진짜 DPI 캡처는 W1 이후로 미룬다.**
- Editor UI CPU, ImGui vertices/indices/draw commands, target별 GPU ms를 기록한다.
- `verify-editor-workspace.ps1` canary를 만든다.

**판정:** 빈 측정·빈 screenshot으로 통과하지 않는다. canary는 **변이로 이빨을 증명한다** —
Content Browser 이름을 한 글자 바꾸거나 central node를 지운 fixture를 넣었을 때 정확히 그 단정만
빨개져야 하고, 첫 실행부터 전부 초록이면 통과로 세지 않는다. legacy layout과 performance
baseline이 이 문서 또는 별도 analysis 산출물에 기록된다.

### W1 — Theme token · font/icon · DPI 정본 (P1, 2일)

- `EditorThemeTokens`와 `ApplyEditorTheme`를 추가한다.
- Inter/font fallback과 Font Awesome atlas build를 editor resource로 옮긴다.
  `IconsFontAwesome4.h`/`6.h` 공존을 정리한다(§1.10).
- literal `PushStyleColor/Var` inventory **91건**을 semantic token 또는 명시적 exception으로
  정리한다. 48건이 `MenuBarWindow.cpp`에 몰려 있으므로 그 파일을 먼저 친다.
- **`io.FontGlobalScale`을 걷고 `style.FontScaleMain` / `FontScaleDpi`로 이주한다**(§3.2).
  `ScaleAllSizes`는 geometry에만 적용해 현재의 이중 적용을 끊는다.
- Win32 DPI awareness와 `io.ConfigDpiScaleFonts` 채택 여부를 판정하고, 채택하면
  `DisplayFramebufferScale` 하드코딩 `(1,1)`을 함께 걷는다.

**판정:** token sample은 기준 hex와 일치하고, font file 부재로 editor가 뜨지 않는 경로가 없다.
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS`를 켠 상태로 Editor가 빌드·기동된다(obsolete 잔존 0의 증명).
user scale 100↔150% 왕복과 **실제 DPI 100↔150% 왕복**을 따로 판정하며, 후자는 DPI 경로를
채택했을 때만 통과 조건에 넣는다 — 채택하지 않으면 "이번 범위에서 하지 않았다"를 명시한다.

### W2 — 공통 styled primitive와 custom draw 4종 (P1, 3일)

- **§7.1의 승계 결정표를 먼저 확정한다.** 기존 `ImGuiHelper` 자산과의 관계를 정하지 않고
  구현을 시작하면 같은 역할의 위젯이 두 벌 남는다.
- 편집 대상에 비-UTF8 파일이 있으면(§1.10) **인코딩을 먼저 정리한 뒤** 내용을 고친다.
- §7의 네 family를 구현해 Inspector/toolbar의 대표 지점부터 이관한다.
- hover/active/focus/nav/disabled/mixed/error 상태 matrix를 고정한다.
- standard widget을 재작성하지 않는 lint/review 목록을 둔다.
- `ImGuiContext.h`의 죽은 `imgui_impl_dx11.h` include를 걷는다.

**판정:** keyboard navigation과 clipping이 유지되고, visual golden 및 §8 성능 gate를 통과한다.

### W3 — stable ID · 자유 docking · workspace 저장/복구 (P0, 3일)

- **선행 지혈: Content Browser 이름 불일치(§1.4)를 먼저 고친다.** stable ID 이주 전에 고쳐 두면
  "고친 것"과 "ID 체계가 세운 것"을 구분해 판정할 수 있다.
- persistent window ID를 `###Editor.*` 상수로 통일한다. 대상은 §1.3-3의 실측 수
  (MenuBar 직접 `Begin` 9 + `ContextRegister` 10 + Scene/Game 2)를 기준으로 센다.
- ToolPanel의 `NoMove`와 implicit `AlwaysAutoResize`를 제거한다. `SceneViewWindow.cpp:212`의
  static flag 누적(§1.3-1)을 함께 지운다.
- 일반 panel close/open과 Window 메뉴 재열기를 연결한다. Content Browser의 popup 우회(§1.3-2)를
  role 계약으로 흡수한다.
- `BuildInitialDockLayout`의 `Tile` 분기 누락(§1.2)을 청산한다.
- `ImGuiRegister.h`의 자기 `#define EDITOR`와 죽은 else 분기를 정리한다.
- `EditorWorkspaceStore`, legacy migration, backup, Reset Layout을 구현한다.

**판정:** title/icon을 바꿔도 dock 위치가 유지되고, save→restart→load가 동일하며, 손상된 layout은
사용자 파일을 잃지 않고 기본 preset으로 복구된다. migration canary는 §1.4의 실물 ini 4벌 —
Content Browser 이중 entry가 든 것 포함 — 을 fixture로 통과해야 한다.

### W4 — 중앙 ViewportHost · canvas 분리 · view demand (P0, 4일)

- central non-closable Host를 만든다.
- **canvas 규약을 하나로 정한다(§1.5).** letterbox냐 stretch냐, rect 원점이 창이냐 content냐를
  먼저 확정하고 ImGuizmo·picking이 같은 출처를 읽게 한다. 지금은 Scene이 창 전체 + titleBar
  수동 보정, Game이 content + letterbox로 갈려 있다.
- Scene image, scene interaction, overlay, game input surface를 분리한다.
- 기존 Editor/Game presentation key를 mode별로 소비한다. Game의 `active`/`ready` 2단 신호는
  이미 있으므로 Scene 쪽과 통일해 재사용한다.
- Scene/Game 두 곳의 `BringWindowToDisplayBack` 강제 호출을 제거한다.
- optional Game Preview와 visible target demand를 연결한다. **현재 UI 쪽에서 view demand를
  제어하는 경로는 없다** — 표시 타깃 선택은 전부 렌더러 내부에서 결정되므로 2단계는 전량 신규다.
- extent 기반 resize는 두 backend의 generation/retire 검증 뒤에만 활성화한다.

**판정:** central Host를 닫거나 ToolPanel로 대체할 수 없고, Game Preview가 닫힌 single-view 상태에서
불필요 target 생산 여부가 계측된다.

### W5 — Play/Pause/Eject 표시·입력 상태 머신 (P0, 4일 · 정찰 뒤 3일→4일)

**추정 정정 근거:** 읽을 committed 신호와 입력 소유권 신호가 **둘 다 없다**(§1.6, §1.8).
controller를 짜기 전에 그 둘을 먼저 만들어야 한다.

- **선행 1 — committed 신호:** 씬 수준 play state 정본이 `m_isGameStart` `atomic_bool` 하나뿐이고
  스냅샷 실패 시 되돌지 않는다(§1.6). requested/pending/committed를 구분할 수 있는 신호를 만들고,
  실패 경로에서 상태가 되돌아오는지부터 단정한다.
- **선행 2 — 입력 소유권 신호:** `ImGuiHost::BeginFrame`의 `WantCapture*` 매 프레임 강제(§1.8)를
  걷는다. 걷는 순간 game input 라우팅 동작이 바뀔 수 있으므로 **계측 → 지혈 순서**를 지킨다.
- `Entering/PlayingPossessed/PlayingEjected/Exiting`을 Editor controller에 둔다.
- pending transaction, Game target not-ready, snapshot failure를 명시적으로 처리한다.
- gizmo/picking/game input/cursor/focus의 단일 owner를 만든다. 커서는 ImGui가 손대지 않으므로
  (§1.8) 경쟁자는 없지만 **해제 누락을 복구해 줄 주체도 없다** — focus loss·Eject·Stop·비정상
  종료에서 해제를 각각 단정한다.
- Undo Clear 이중 경로(§1.6)를 controller 단일 소유로 정리한다.
- Stop 뒤 prior document/focus/selection 정책을 복원한다.

**판정:** `verify-play-roundtrip.ps1`을 유지하면서 viewport target과 input owner 단정을 추가한다.
`play.state`를 committed·target·input owner까지 내도록 확장하고, **스냅샷 실패를 주입했을 때
UI가 Playing으로 보이지 않는 것**을 명시적으로 단정한다. Play 실패·Alt-Tab·Eject·Pause·Stop
반복에서 cursor/gizmo가 잘못 남지 않는다.

### W6 — preset 4종과 layout UX (P2, 2일)

- `S&Box Compact`, `Level Editing`, `UI Editing`, `Rendering & Debug`, `Legacy Unity`를 제공한다.
- Save As/Rename/Delete/Reset와 active workspace 표시를 만든다.
- small window와 DPI 변화에서 minimum central area를 보존한다.

**판정:** 각 preset을 연속 적용해도 orphan dock node와 off-screen floating panel이 없고, 사용자가
수정한 workspace를 preset update가 덮어쓰지 않는다.

### W7 — Hierarchy/Browser clipping과 presentation cache (P1, 3일)

- 1k/10k/50k hierarchy와 asset fixture를 만든다.
- **flatten presentation cache를 먼저 만들고**, 그 위에 visible-row clipping을 얹는다(§8.3의
  순서 제약). 재귀 `TreeNodeEx` 위에 clipper를 바로 끼울 자리는 없다.
- lowercase/search cache, icon/label formatting cache를 측정 기반으로 적용한다.
- `HierarchyStore` 단독 정본 불변식을 source gate로 고정한다. 현재 Hierarchy 창이
  `scene->m_Entities`를 직접 인덱스 순회하는 경로가 이 정본과 어긋나지 않는지 착수 시 확인한다.

**선행:** SceneGraph H3(현재 완료).
**판정:** 결과/선택/drag-drop 의미가 동일하고 p95 CPU·allocation 개선 수치를 기록한다. 캐시만
추가하고 실측 이득이 없으면 제거한다.

### W8 — 통합 회귀와 legacy 강제 배치 은퇴 (P0, 2일)

- DX12/Vulkan, DPI, restart, damaged ini, Play 왕복, Game Preview, preset matrix를 자동화한다.
- viewport를 제외한 chrome crop visual golden을 만든다.
- 구 `BuildInitialDockLayout`의 ContentsBrowserStyle 분기와 핵심 ToolPanel `NoMove`를 제거한다.
- ImGui internal adapter version canary를 CI에 넣는다.

**판정:** 양 backend에서 검증 레이어 오류/비정상 종료 0, layout·Play·시각·성능 gate 통과 후에만
PHASE 21을 완료로 표시한다.

---

## 10. 의존성과 병행

```text
W0 → W1 → W2
W0 → W3 → W4 → W5
          W4 → W6
H3 + W0 ─────→ W7
W2 + W5 + W6 + W7 → W8
```

- PHASE 20 Network와 독립 병행 가능하다.
- **W0 안에 순서 제약이 둘 있다.** 관측 커맨드 5종(§1.9)과 창 순회 결정화(§1.3-4)는
  기준선 캡처·canary보다 **먼저**다. 그 반대로 하면 게이트가 "창이 떴다"만 단정하고,
  golden은 흔들리는 값을 정답으로 굳힌다.
- W4는 render bridge/live display target 변경과 hot zone을 조정한다.
- W5는 SceneManager의 Play transaction을 소비하지만 소유권을 가져오지 않는다.
  단 **읽을 committed 신호와 입력 소유권 신호를 만드는 것은 W5의 몫**이다(§1.6, §1.8) —
  지금은 둘 다 없다. `WantCapture*` 강제 해제는 game input 라우팅과 hot zone이 겹친다.
- W7 안에서 flatten presentation cache는 clipping의 **선행**이다(§8.3).
- W7은 SceneGraph H3의 `HierarchyStore`를 읽는 presentation 최적화다.
- PHASE 16은 runtime/game UI를 계속 소유한다. Viewport content rect가 필요하면 W4가 immutable
  extent를 생산하고 PHASE 16 소비자가 읽는 방향만 허용한다.

---

## 11. 검증 행렬

| 축 | 최소 케이스 | 실패 조건 |
|---|---|---|
| layout | clean, legacy, custom, corrupt, missing | central 부재, panel 유실, silent overwrite |
| DPI | 100%, 125%, 150%, resize 왕복 | double scale, clipped text, 0-size |
| backend | DX12, Vulkan | presentation key 오류, validation error, crash |
| mode | Edit, Entering, Play, Pause, Eject, Stop | wrong target, gizmo/game input 동시 활성 |
| focus | click panel, Alt-Tab, modal, Game Preview | cursor lock 잔존, focus stealing |
| scale | Hierarchy/Browser 1k/10k/50k | 빈 측정, semantic mismatch, p95 회귀 |
| style | normal/hover/active/focus/disabled/error | token drift, nav/focus 정보 소실 |

필수 자동화 후보:

- `Tools/regression/verify-editor-workspace.ps1` — **신규. 선행으로 §1.9의 관측 커맨드가 필요하다.**
- `Tools/regression/verify-editor-theme-golden.ps1` — 신규
- `Tools/regression/verify-editor-viewport-mode.ps1` — 신규
- 기존 `verify-play-roundtrip.ps1`
- 기존 `verify-play-selection-undo.ps1`
- 기존 `verify-play-mode-policy-boundary.ps1`
- backend별 feature-test screenshot/capture 도구

정찰 시점(2026-08-30) 회귀 세트는 54종이며 에디터 chrome을 단정하는 것은 **0종**이다. 위 셋은
전량 신설이고, 그 전제인 관측 커맨드도 없다(§1.9).

검사는 “창이 떴다”가 아니라 다음을 직접 단정한다.

- stable ID와 expected dock node 존재
- **DockBuilder가 지정한 이름과 실제 `Begin` 이름이 전부 일치함** (§1.4가 이미 깨뜨린 단정이다.
  이 검사가 있었다면 Content Browser 불일치는 나오자마자 잡혔다)
- central rect가 0이 아니고 ViewportHost가 유일함
- mode별 selected target과 input owner
- Play/Stop transaction이 실제로 일어났음, **그리고 실패 시 UI가 Playing으로 보이지 않음**
- capture crop과 token sample이 비어 있지 않음
- 성능 fixture의 row/item 수가 기대값과 일치함
- `ViewportsEnable`이 켜지지 않았음 (§1.7 — 이번 범위 밖임을 게이트가 지킨다)

각 검사는 도입 시점에 **변이로 이빨을 증명한다.** 새로 만든 검사가 첫 실행부터 전부 초록이면
통과로 세지 않고, 대상 결함을 주입해 정확히 그 단정만 빨개지는지 확인한 뒤 통과로 센다.

---

## 12. 위험과 대응

| 위험 | 대응 |
|---|---|
| internal ImGui docking API drift | adapter 한 곳, version assertion, upgrade canary |
| stable ID 전환 시 기존 layout 유실 | known-name migration + 원본 backup + Legacy preset |
| Play click과 transaction commit 불일치 | pending/committed 분리, failure state 단정 |
| Game 화면에 gizmo/picking 잔존 | SceneInteraction과 GameInputSurface 분리, owner invariant |
| custom draw가 nav/accessibility를 깨뜨림 | 4 family 제한, ImGui ID/nav/clip 계약 test |
| theme만 바꾸고 창별 literal override가 남음 | W1 inventory와 semantic exception 목록 |
| central resize가 render target thrash 유발 | debounce/generation, 2단계 도입, 양 backend gate |
| Hierarchy cache가 두 번째 정본이 됨 | handle/depth 파생 cache만 허용, H3 source gate |
| OS multi-viewport를 flag 하나로 켬 | 이번 범위 제외, renderer swapchain/DPI 계약 선행 |
| **관측 표면이 없어 게이트가 “창이 떴다”만 단정** | W0에서 `editor.*` 관측 커맨드 5종 선행(§1.9), 변이로 이빨 증명 |
| **DPI 자체 구현이 ImGui 1.92 경로와 이중화** | `FontScaleMain/FontScaleDpi` 채택, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`로 잔존 0 증명(§3.2) |
| **`WantCapture*` 강제를 걷자 입력 라우팅이 바뀜** | W5에서 계측 먼저, 지혈은 그 뒤. 라우팅 근거를 문서화한 뒤 걷는다(§1.8) |
| **커서 해제 누락을 복구할 주체가 없음** | `NoMouseCursorChange`가 서 있어 ImGui가 되돌리지 않는다. 해제 경로 4종을 각각 단정(§1.8) |
| **custom widget이 기존 ImGuiHelper 자산과 이중화** | W2 착수 전 §7.1 승계 결정표 확정, 소비자를 끊어 본 뒤 은퇴 |
| **비-UTF8 소스 편집이 무관한 주석을 깨뜨림** | 대상 9개는 내용 수정 전에 인코딩부터 정리(§1.10) |
| **창 순회 비결정성으로 visual golden이 흔들림** | golden을 뜨기 전에 `m_contexts` 순회를 결정적 순서로(§1.3-4) |

rollback 단위는 W1 theme, W3 workspace, W4 ViewportHost, W5 mode controller를 각각 feature
flag로 분리한다. 단, 완료 뒤 영구 이중 경로를 유지하지 않고 W8에서 legacy 강제 배치를 제거한다.

---

## 13. 최종 완료 조건

1. Editor는 Dear ImGui와 기존 DX12/Vulkan ImGui presentation backend를 유지한다.
2. S&Box 참고 색·geometry·typography가 `EditorThemeTokens` 한 정본에서 적용된다.
3. custom draw는 승인된 소수 family에 한정되고 표준 ImGui interaction을 보존한다.
4. `ViewportHost`는 central에 항상 존재하며 닫거나 ToolPanel로 대체할 수 없다.
5. Hierarchy/Inspector/Browser/Console/Profiler/Game Preview는 자유롭게 dock/close/reopen된다.
6. title/icon/번역 변화가 workspace identity를 깨뜨리지 않는다. **DockBuilder 지정 이름과 실제
   `Begin` 이름의 불일치가 0이며, 게이트가 이를 직접 단정한다**(§1.4의 현존 결함 청산).
7. clean/legacy/custom/corrupt layout의 save/load/reset/migration이 자동 검증된다.
8. Play는 중앙 Game canvas로, Eject는 Editor canvas로, Stop은 편집 상태로 돌아오며 gizmo/input/cursor
   owner가 겹치지 않는다. **스냅샷 실패 시 UI가 Playing으로 보이지 않는다**(§1.6).
9. optional Game Preview는 기존 두 표시 타깃을 재사용하고 두 번째 카메라 정본을 만들지 않는다.
10. theme/docking CPU 회귀가 §8 gate 안이며, single-view/clipping 이득은 실제 수치로 기록된다.
11. DX12/Vulkan, DPI, Play 왕복, visual golden, large-data 성능 gate가 모두 통과한다.
12. `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`를 켠 상태로 Editor가 빌드·기동되고, `ViewportsEnable`은
    꺼진 채로 남아 있음을 게이트가 단정한다(§3.2, §1.7).
13. `editor.*` 관측 커맨드 5종이 서 있고, 에디터 chrome 회귀 3종이 **변이로 이빨을 증명한**
    상태로 CI에 있다(§1.9, §11).
14. PHASE 21 완료 표시는 위 구현·runtime gate 뒤에만 갱신한다. 이 문서 작성만으로는 0%다.
