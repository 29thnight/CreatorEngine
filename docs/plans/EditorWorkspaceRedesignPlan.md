# 에디터 워크스페이스 · 도킹 · ViewportHost 재설계 (PHASE 21)

- 수립일: 2026-08-24
- 사전 정찰 갱신: 2026-08-30 (§1 기준선 전수 재실측 · §3.2/§7.1/§8.3/§9/§11/§12 정정)
- 재정찰: 2026-09-10 — §1 17항 재대조(15 그대로 · 2 정정) · 신규 발견 9건 · 메뉴 표면 감사.
  전문은 [EditorMenuSurfaceAndPhase21Preflight.md](../analysis/EditorMenuSurfaceAndPhase21Preflight.md).
  이 문서에는 W0/W1/W3/W5의 정정 지점과 **부록 A(메뉴 등록 배선 계약 `editor::`)**를 반영했다.
- 셸 크롬 착지: 2026-09-10 — s&box 배치·ImGui 제목표시줄·다크 스킨이 W3보다 앞서 섰다.
  기록과 남은 창 선언 계약은 **부록 B**. 이 착지가 §1.1·§1.2·§1.4의 일부를 닫았고
  §1.3의 표시 상태 저장소 수를 정정했다.
- 상태: 부록 A M0 착지(progress) · 부록 B M3 착지(done) · W0~W8 구현 미착수
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

**(2026-09-10 부분 해소, 부록 B)** `ApplyEditorStyle`을 s&box 계열 다크 스킨으로 다시 썼다.
위에 적은 수치는 전부 바뀌었다 — `WindowRounding=0`, `FrameRounding=3`, `ItemSpacing=(8,5)`,
`WindowBg`≈`#242426`, `FrameBg`≈`#191A1B`. 메뉴 팝업이 밝은 회색(0.95)이라 항목마다 검은 글씨를
눌러 담던 우회로도 걷었고, 그 행의 `PushStyleColor/PopStyleColor`가 **0/0**이 됐다.
**W1이 하려던 일은 줄지 않았다.** 값이 바뀌었을 뿐 여전히 semantic token이 아니고, 창별 예외
91건 중 메뉴 행 몫만 빠졌다. 스케일 경로와 폰트 하드코딩도 그대로다 — W1의 inventory 기준값을
**다시 세어야 한다.**

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

**(2026-09-10 부분 해소, 부록 B)** 두 분기를 한 배치로 합쳤다. 오른쪽 열이 전체 높이를 쓰고
(Hierarchy 위 / Inspector 아래), 남은 왼쪽을 아래로 갈라 자산 브라우저 계열을 넣고, 가운데는
뷰포트가 탭으로 공유한다. `ContentsBrowserStyle`은 이제 **Content Browser를 도크할지 말지
한 줄에만** 남는다 — Tile에서 그 창은 팝업 드로어라 도크할 자리가 없다. AssetBundle은 두
경우 모두 배치받는다. `imgui.ini` 재생성 경로가 0이던 것도 Window > Reset Layout으로 열었다.
**남은 것:** `ContentsBrowserStyle`이 배치에 남긴 마지막 한 줄을 없애는 일은 W3다 — 창 역할이
선언되면 드로어 여부는 그 선언이 답한다.

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

**(2026-09-10 정정)** 창 표시 상태 저장소는 둘이 아니라 **셋**이다. 8-30 정찰과 9-10 재정찰이
모두 둘로 셌다.

| 저장소 | 개수 | 영속 |
|---|---:|---|
| `MenuBarWindow` 멤버 bool | 10 | 안 됨 |
| `ImGuiRenderContext::m_opened` | 10 | 안 됨 |
| `ImGui::Begin`에 직접 넘기는 `p_open` bool | 11 | 안 됨 |

직접 `Begin` 15곳 중 **11곳이 자기 bool을 `p_open`으로 넘겨** 닫기 버튼을 스스로 처리한다.
그리고 `MenuBarWindow`의 7곳은 두 번째와 세 번째를 **겸용한다** — 같은 창의 열림 여부가 두
자리에 있다. W3의 "close/reopen 계약"은 둘이 아니라 셋을 합치는 일이고, 부록 B의 창 선언(M4)이
그 합치는 자리다.

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

**(2026-09-10 부분 해소, 부록 B)** 공백 2 vs 1 불일치 자체는 닫았다 — 도크 빌더와 Window 메뉴와
창 본문이 `EditorWindowNames.h` 한 곳의 상수를 부른다. 리터럴을 각자 적는 한 같은 일이 또
생기므로 식별자의 출처를 하나로 만든 것이다.
**남은 것은 그대로다.** 이름 뒤 정렬 공백은 기존 `imgui.ini`의 도크 항목을 지키느라 남겼고,
표시 문자열이 여전히 persistence ID다. 아래 `###` 규칙으로 표시 이름과 안정 식별자를 가르는
일은 W3의 몫이며, 그때 `EditorWindowNames.h`가 그 표의 씨앗이 된다.

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

여섯째로 `editor.menu`가 붙는다 — 조립된 상단·팝업 메뉴 트리의 TSV 덤프다. 이것은 부록 A의 M2가
자기 게이트를 위해 만드는 것이라 **W0이 따로 짜지 않는다.** 대신 W0의 window inventory 단정이
`editor.windows`와 `editor.menu` 둘을 같이 읽어 "열 수 있는 창"과 "실제 등록된 창"의 차집합을
드러내야 한다. 지금 AssetBundle 창이 어느 메뉴에도 없어 열 수단이 없는 것이 그 차집합의 실물이다.

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
2026-09-10 재정찰에서 **부록 A의 메뉴 등록 배선 M0~M2(3.5일)**를 범위에 넣어 총 **28.5일**이 됐다.
같은 날 **부록 B의 셸 크롬 M3(1.5일)**가 먼저 착지했고 **창 선언 M4(3일)**를 범위에 더해
총 **33일**이 됐다. 착수 순서는 §10의 권장 순서 표를 따른다.

M3은 W3보다 앞서 섰다. 순서를 바꾼 이유는 §10에 적었다 — 요약하면, 제목표시줄과 배치와 스킨은
서로를 전제하므로 한 번에 세우는 편이 같은 파일을 세 번 헤집는 것보다 싸고, 창 본문을 건드리지
않아 W3의 대상이 줄지 않는다.

### W0 — 관측 표면 · 기준선 · 실패 게이트 (P0, 2일 · 정찰 뒤 1일→2일)

**추정 정정 근거:** §1.9대로 에디터 chrome을 밖에서 볼 CLI 표면이 0이다. 관측 커맨드를 만들지
않으면 canary가 "창이 떴다"밖에 단정하지 못한다. 원래의 1일은 측정·캡처만 센 값이다.

- `editor.layout` / `editor.windows` / `editor.dock` / `editor.theme` / `editor.viewport` 관측
  커맨드를 신설한다(§1.9). 이번 슬라이스에서는 **관측만** 하고 설정·저작은 W3/W6에 둔다.
  **(9-10 재정찰)** `editor.viewport`는 `ScenePhase`를 실어야 한다 — 현행 `play.state`는
  gameStart·paused·pending만 내서 스냅샷 실패 뒤의 상태가 성공과 구분되지 않는다
  (`SceneObjectCommands.cpp:1034-1057`). 관측 대상 상태의 정본은 재정찰 문서 §1.4의
  "GUI에만 있는 동작" 표다. screenshot은 기존 `Tools/regression/capture-window.ps1`을 재사용한다.
- `ImGuiRegister`의 창 순회를 결정적 순서로 바꾼다(§1.3-4). golden을 뜨기 **전에** 한다.
- 현재 `imgui.ini` 4벌을 fixture로 고정한다(§1.4) — 정본 2, 유물 2. Content Browser 이중 entry가
  들어 있는 실물을 그대로 쓴다.
- 핵심 window title/flags, open state inventory를 고정한다. `PushStyleColor/Var` 91건의 위치
  목록을 W1 입력으로 남긴다.
- 1920×1080/2560×1440, user scale 100%/150%의 shell screenshot을 캡처한다. **DPI 경로가 없으므로
  (§3.2) 이 캡처는 "user scale"임을 명시하고, 진짜 DPI 캡처는 W1 이후로 미룬다.**
- Editor UI CPU, ImGui vertices/indices/draw commands, target별 GPU ms를 기록한다.
- `verify-editor-workspace.ps1` canary를 만든다.

**판정:** 빈 측정·빈 screenshot으로 통과하지 않는다. **canary는 불변식을 단정하고 현재 개수를 단정하지
않는다** — "상단 메뉴가 셋"처럼 착수 전 상태를 지키는 단정은 부록 A의 M1이 Tools/Window를 세우는 순간
정당하게 빨개져 게이트가 제 일을 못 한다. 대신 "등록됐는데 그려지지 않는 고아 0", "DockBuilder 이름과
`Begin` 이름 불일치 0"처럼 **어느 시점에도 참이어야 하는 것**을 단정한다.
canary는 **변이로 이빨을 증명한다** —
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
  **(9-10 재정찰 정정)** 프로세스는 매니페스트로 이미 `permonitorv2`를 선언한다
  (`Editor/EngineEntry/Academy_4Q.exe.manifest:26`). 따라서 문제는 "인식을 켤 것인가"가 아니라
  **"이미 인식 중인데 보정이 0"**이다 — OS 가상화 없이 150% 모니터에서 물리 픽셀 1:1로 그린다.
  최소 경로는 `WM_DPICHANGED` 처리 + `style.FontScaleDpi`/`io.ConfigDpiScaleFonts` 채택이며,
  "채택하지 않는다"는 선택은 그 1:1 렌더를 유지한다는 뜻임을 판정문에 명시한다. ImGui 1.92.8은
  ThirdParty가 아니라 vcpkg에서 온다. `Fonts->Build()`(`EditorRenderer.cpp:84`)도 1.92 동적
  아틀라스 기준 legacy라 함께 걷는다.

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
  **(9-10 재정찰)** Window 메뉴 자체가 없고(상단 메뉴는 File·Edit·Settings 셋), 메뉴를 표로 그리는
  기구도 0이다. 하드코딩 메뉴를 하나 더 만들지 않으려면 **메뉴 등록 배선이 W3의 선행 부품**이다.
  설계는 **부록 A**에 있다(`editor::` 별도 계통 · `for_editor()` 선언 · 표면을 닫힌 집합으로 ·
  M0~M2 3.5일. 그중 **M1이 이 슬라이스의 선행**). 창 표시 상태 저장소도 둘로 갈라져 있다
  (`MenuBarWindow` bool 10개 vs `ImGuiRenderContext::m_opened`)라 같은 슬라이스에서 하나로 합친다.
  `GetContext(name)`의 `operator[]`(`ImGuiRegisterClass.h:95-98`)는 오타 이름을 유령 창으로
  영구 삽입하므로 `find`로 바꾼다.
- `BuildInitialDockLayout`의 `Tile` 분기 누락(§1.2)을 청산한다. **(9-10)** Tile이 기본값이라 새
  설치에서 Content Browser·AssetBundle은 항상 떠 있고, dock 지정에 없는 창이 15개 이상이다. ini는
  부재 시에만 생성되고 재생성 경로가 없으므로(`EditorRenderer.cpp:238-246`) Reset Layout과 layout
  version 키가 선택이 아니라 필수다.
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
  **(9-10 재정찰)** 파급이 더 넓다. `PlayModeEvent.Broadcast(true)`가 `CaptureSceneSnapshot()`
  **앞**에서 발화해(`SceneManager.cpp:1538-1548`) 구독자(`EditorPlayModeController.cpp:25-52`)가
  Undo 스택을 비우고 game mode로 바꾼 뒤에야 스냅샷이 실패한다. 순서를 **Snapshot → Phase →
  Broadcast**로 바꾸지 않으면 committed 신호를 만들어도 Undo는 먼저 죽는다.
  `m_isEditorSceneLoaded`도 실패 여부와 무관하게 true가 된다(`:347-357`).
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

M0 → M1 → M2          (부록 A. W0과 파일을 공유하지 않아 병행 가능)
M1 ──────→ W3         (Window 메뉴 재열기 계약의 선행)
M2 ──────→ W0 canary  (여섯째 관측 커맨드 editor.menu를 승계한다)

M3 ──────→ M4 → W3    (부록 B. M3 착지 완료. M4가 창 선언을 세우고 W3이 그 위에 ID를 얹는다)
M3 ──────→ W0 golden  (기준선을 새 chrome으로 뜬다 — 옛 배치의 골든은 뜨자마자 폐기된다)
M3 ──────→ W1 재계수  (창별 예외 91건에서 메뉴 행 몫이 빠졌다. 기준값을 다시 센다)
```

**권장 착수 순서(2026-09-10).** 의존만 보면 여러 배열이 가능하지만, 아래 순서가 같은 파일을 두 번
헤집지 않고 golden을 한 번만 뜬다.

| 순 | 슬라이스 | 이 자리인 이유 |
|---|---|---|
| 0 | **M3 — 셸 크롬 (착지 완료)** | 제목표시줄·배치·스킨이 서로를 전제한다. 제목을 가운데 두려면 재생 컨트롤이 그 행을 비워야 하고, 그러려면 툴바 행이 생겨야 하고, 그러면 독스페이스의 '행이 둘' 상수가 깨진다. 셋을 따로 하면 같은 파일을 세 번 헤집는다. 창 본문은 건드리지 않아 W3의 대상 수는 줄지 않는다 |
| 1 | **W0 전반** — 관측 커맨드 5종 · 창 순회 결정화 | `editor.*` 가족의 **출력 형식을 여기서 정한다.** M2의 여섯째가 그 관례를 따르게 하려면 다섯이 먼저다. 순회 결정화는 모든 golden의 선행 |
| 2 | **M0** — 선언 어휘와 목록 배관 | 새 폴더뿐이라 동작 변화 0. 1과 파일을 공유하지 않아 **병행 가능** |
| 3 | **M1** — registry와 그리기 배선 | 메뉴 구조를 바꾸는 마지막 슬라이스. 빈 뿌리를 그리지 않으므로(부록 A.6) 픽셀 중립이고, **W1보다 먼저** 두어야 `MenuBarWindow.cpp` 2,716줄을 구조와 토큰으로 두 번 헤집지 않는다 |
| 4 | **M2** — 게이트와 `editor.menu` | M1의 표가 있어야 덤프할 것이 생긴다. W0 canary가 쓸 여섯째 커맨드를 여기서 낸다 |
| 5 | **W0 후반** — ini fixture 4벌 · inventory · screenshot · 성능 기준선 · canary | chrome이 최종 구조가 된 뒤 **golden을 한 번만 뜬다.** canary가 dock 불변식과 메뉴 불변식을 함께 단정할 수 있다 |
| 5.5 | **M4** — 창 선언(부록 B.3) | M3이 셸에 프레임 소유를 준 뒤라야 선언에 담을 것이 정해진다. W3보다 **먼저**여야 한다 — W3의 안정 ID는 선언의 한 필드가 되고, 표시 상태 저장소 셋을 합치는 자리도 여기다. W0 후반 골든 뒤에 두어 골든을 두 번 뜨지 않는다 |
| 6~ | W1 → W2, W3 → W4 → W5·W6, W7, W8 | §9·§10의 기존 의존 그대로. W3의 선행인 M1·M4는 이미 끝나 있다 |

두 가지를 주의한다. `CommandDescriptorSeeds.cpp`는 1과 4가 모두 건드리는 유일한 공유 파일이고 지금
**다른 세션이 수정 중**이다. 그리고 3은 기존 19개 상단 항목을 이관하지 않는다 — 배선만 세우고 신규만
태운다(부록 A.7).

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
| **메뉴 동작을 선언했는데 조용히 사라짐** | Editor는 StaticLibrary이고 `/WHOLEARCHIVE`가 없어 참조 없는 TU가 탈락한다. 중앙 `EDITOR_MENU_LIST` 하나 + 기동 누락 게이트(부록 A.2·A.5) |
| **팝업 문맥 타입을 혼동해 엉뚱한 대상에 동작을 붙임** | 표면을 닫힌 열거형으로, 문맥 타입을 호스트 특성으로 못 박아 **컴파일 오류**로 만든다(부록 A.3·A.4) |
| **메뉴 동작이 Presentation 스레드에서 씬을 직접 만짐** | `EnqueueStructured`로 게임 스레드에 넘기고 completion을 반드시 넘겨 배치 종료 코드 오염을 막는다(부록 A.6) |

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
14. 메뉴 등록 배선이 서 있다 — 동작 하나를 상단·팝업의 원하는 카테고리에 붙이는 데 필요한 편집이
    **선언 한 줄**이고, 표면 오타와 문맥 타입 불일치가 컴파일 오류이며, 목록 누락은 기동 게이트가
    잡는다(부록 A). `editor.menu` 덤프가 골든과 함께 CI에 있다.
15. PHASE 21 완료 표시는 위 구현·runtime gate 뒤에만 갱신한다. 이 문서 작성만으로는 0%다.
---

## 부록 A. 메뉴 등록 배선 계약 (`editor::`)

- 추가: 2026-09-10. W3의 "Window 메뉴 재열기"와 §1.3의 표시 상태 이원화를 풀기 위한 선행 부품.
- 실측 근거는 [EditorMenuSurfaceAndPhase21Preflight.md](../analysis/EditorMenuSurfaceAndPhase21Preflight.md) §2.
  이 부록은 그 실측 위에 세운 **계약**이다. 구현 미착수.

### A.1 `meta::`에 얹지 않는다

리플렉션의 `meta::method`에 메뉴 속성을 더하는 안을 검토했고 **기각한다.**

| 근거 | 실측 |
|---|---|
| 계층 오염 | `meta::`는 `Engine/Utility_Framework`에 있고 `Reflection.hpp`를 타고 런타임 전역에 퍼진다. Player도 같은 것을 링크한다. 에디터 전용 어휘를 `method_info`·`Meta::Method`에 넣으면 CT3이 끊은 전파를 되돌린다 |
| 골든 사정권 | `verify-reflection-golden.ps1`이 76타입 diff 0을 단정한다. `Meta::Method`(`ReflectionType.h:69-73`)를 건드리면 메뉴 작업이 이유 없이 그 게이트 안에 들어간다 |
| 인스턴스 제약 | `Meta::MakeMethod`는 `Ret(ClassT::*)(Args...)` **포인터 투 멤버만** 받는다(`ReflectionFunction.h:277`). 전역 동작을 원리적으로 표현할 수 없다 |

따라서 `editor::`는 **별도 계통**이다. `meta::`를 읽지도, 확장하지도 않는다.

단 두 관례는 그대로 물려받는다. **파일명은 PascalCase, 네임스페이스 안 식별자는 snake_case**다
(`MetaSchema.h`가 `meta::field`·`schema_of`·`field_info`를 담는 것과 같은 분리). 선언 함수 이름은
리플렉션의 `static consteval auto reflect()`(`SoundComponent.h:15`)에 대응해 **`for_editor()`**로 둔다.

### A.2 형태를 결정하는 링크 제약 (실측)

| 항목 | 값 |
|---|---|
| `Editor.vcxproj` ConfigurationType | StaticLibrary, 네 구성 전부(`:226,232,239,247`) |
| `CreatorEditor.vcxproj` ConfigurationType | Application(`:32,38,45,53`) |
| `WholeArchive` / `/WHOLEARCHIVE` | Editor·Engine 전 vcxproj에 **0건** |

정적 라이브러리의 오브젝트 파일은 외부에서 심볼을 참조할 때만 링커가 끌어온다. 따라서 독립 `.cpp`에
네임스페이스 스코프 자기 등록자를 두면 **조용히 사라진다.** 이 저장소가 흩어진 정적 등록자를 은퇴시키고
명시 등록 진입점으로 간 이유가 그것이며, 등록 한 줄이 빠지면 명령이 말없이 없어진다는 경고가
`Commands/CommandRegistrar.h:14-18`·`:58`에 이미 적혀 있다.

리플렉션이 이 문제를 푼 형태를 그대로 쓴다. 선언은 지역에 두고,
[RegisterReflectManual.h](../../Engine/SceneRuntime/RegisterReflectManual.h)가 타입 헤더 전부를
include하면서 `REFLECT_TYPE_LIST(X)`로 열거한다(`:86`, 소비 `:169`). **include가 인스턴스화를 링크되는
TU 안으로 끌어오고, 열거가 등록을 돌린다.** 목록 하나가 두 일을 한다. 누락은 기동 검사가 잡는다.

`editor::`도 같은 계약이다. **선언은 지역, 목록은 중앙 하나, 누락은 게이트.** "선언만 하면 알아서
나타난다"는 형태는 이 링크 조건에서 성립하지 않으므로 채택하지 않는다.

### A.3 표면을 문자열이 아니라 닫힌 집합으로 둔다

상단 메뉴와 팝업 메뉴는 **경로 문자열의 뿌리가 서로 다른 집합**이다. 상단은 카테고리
(File·Edit·Settings·Tools·Window·Help)이고, 팝업은 그리는 자리(Hierarchy·Content Browser·Inspector 등)다.
둘을 한 문자열 필드에 담으면 뿌리 오타가 **그려지지 않는 고아 항목**을 만든다. 이 저장소가 반복해서
겪은 조용한 소멸이다. 그래서 표면은 열거형이고, 뿌리 뒤의 하위 경로만 문자열이다.

```cpp
namespace editor
{
    enum class top_menu_root { file, edit, settings, tools, window, help };

    enum class popup_host {
        hierarchy, content_browser_folder, content_browser_asset,
        inspector_component, scene_view, behavior_tree_node, animator_node,
    };
}
```

**팝업 호스트가 동작의 인자 타입을 결정한다.** Hierarchy 팝업의 문맥은 클릭된 엔티티고, Content Browser
자산 팝업의 문맥은 파일 경로다. 이 차이를 `void*`나 `std::any`로 뭉개면 CT6-a가 걷어낸 이중 타입소거를
새 소비자로 되살린다. 그래서 호스트별 문맥 타입을 특성으로 못 박고, 표기 단계에서 검사한다.

```cpp
namespace editor
{
    struct entity_target    { std::string identity; };            // @scene:index:generation
    struct asset_target     { std::filesystem::path path; };
    struct folder_target    { std::filesystem::path path; };
    struct component_target { std::string entity_identity; std::string component; };

    template<popup_host H> struct popup_context;
    template<> struct popup_context<popup_host::hierarchy>             { using type = entity_target; };
    template<> struct popup_context<popup_host::content_browser_asset> { using type = asset_target; };
    template<> struct popup_context<popup_host::inspector_component>   { using type = component_target; };
    // ...

    template<popup_host H> using popup_context_t = typename popup_context<H>::type;
}
```

대상 타입이 원시 포인터가 아니라 **신원을 든 값**인 것이 요점이다. `entity_target`이
`@scene:index:generation`을 들기 때문에(§A.6의 스레드 규약) 큐를 한 번 거쳐도 핸들이 죽지 않는다.

### A.4 선언 표기 — 서명이 결속을 선언한다

별도 `target(...)` 어휘를 두지 않는다. **함수 서명이 대상 결속을 말한다.**

| 서명 | 뜻 |
|---|---|
| `void f()` | 전역 동작. 항상 활성 |
| `void f(editor::entity_target)` | 선택 대상. 선택이 없으면 registry가 자동 비활성 |
| 팝업의 `void f(popup_context_t<H>)` | 그 호스트의 문맥. 타입이 어긋나면 **컴파일 오류** |

표기는 표면이 이름에 드러나는 짧은 별칭을 정본으로 쓴다. 뿌리를 값으로 넘기는 두 원형
(`top_menu_item<Root, Fn>` · `popup_item<Host, Fn>`)은 그 아래 원시 표기로 남긴다.
`meta::field<&Self::x>`가 `field_info` 위의 짧은 표기인 것과 같은 층 구성이다.

```cpp
struct asset_menus
{
    static consteval auto for_editor()
    {
        return editor::menu_set(
            editor::in_tools<&reimport_all>("Assets/Reimport all")
                   .shortcut("Ctrl+R"),

            editor::in_content_asset<&reimport_one>("Reimport")
                   .enabled(&is_model_asset),

            editor::in_hierarchy<&make_prefab>("Create/Prefab"),

            editor::in_content_asset<&delete_asset>("Delete")
                   .confirm("이 자산을 지운다. 되돌릴 수 없다."));
    }
};
```

수정자는 `field_info::with`와 같은 연쇄 형태로 둔다. `enabled`는 대상 있는 동작이면 같은 문맥 타입을 받고,
`shortcut`은 단축키 registry가 서기 전까지 **표시 전용**이며, `order`는 같은 하위 메뉴 안 안정 정렬,
`confirm`은 파괴적 동작 전용이다. `confirm`을 어휘에 넣는 근거는 실측이다. 지금 Content Browser의
Delete 두 곳이 확인도 Undo도 없이 `file::remove`를 부른다(`ContentsBrowserWindow.cpp:458,:531`).

동작 이름은 약칭을 쓰지 않는다. `content_browser_asset`이고 `cb_asset`이 아니다.

### A.5 중앙 목록과 누락 게이트

```cpp
// Editor/EditorMenu/RegisterEditorMenuManual.h — RegisterReflectManual.h와 같은 모양
#include "AssetMenus.h"
#include "SceneMenus.h"

#define EDITOR_MENU_LIST(X) \
    X(asset_menus) \
    X(scene_menus) \

inline void register_editor_menus()
{
#define EDITOR_MENU_REGISTER_ONE(T) editor::register_declarer<T>();
    EDITOR_MENU_LIST(EDITOR_MENU_REGISTER_ONE)
#undef EDITOR_MENU_REGISTER_ONE
}
```

- 이미 목록에 있는 선언자에 동작을 더하는 것은 **그 헤더 한 줄**이다. 중앙 파일을 건드리지 않는다.
- 새 선언자는 include 한 줄과 목록 한 줄. 리플렉션의 규약과 동일하다.
- `editor.menu` 관측 명령이 조립된 메뉴 트리를 TSV로 낸다(표면·뿌리·경로·동작 id·enabled 유무·단축키).
  `commands.list`와 같은 골든 형태다. **이 명령 자체가 §1.9가 요구하는 `editor.*` 관측 다섯 중 하나다.**
- 게이트는 셋을 단정한다. ① 목록에서 선언자를 빼면 그 항목만 사라진 것을 잡는다. ② `popup_host`
  열거자마다 그리는 자리가 하나 이상 있고 그 역도 성립한다(배선 안 된 표면 0). ③ 같은 표면 안 경로 충돌 0.
- 변이로 이빨을 증명한다. 문맥 타입 불일치는 **컴파일 오류**이므로 런타임 게이트가 아니라 빌드 게이트로
  세고, 그 사실을 게이트 파일 머리에 적는다. 정책을 assert로만 적으면 Release에서 강제력이 0이 된다.

### A.6 그리기 배선과 스레드 규약

그리는 자리는 표면으로 물어본다. 상단 여섯, 팝업 일곱이다.

| 자리 | 위치 |
|---|---|
| 상단 File·Edit·Settings의 `EndMenu` 직전 | `MenuBarWindow.cpp:321,365,459` |
| 상단 Tools·Window·Help(신설, registry 전용) | `MenuBarWindow.cpp:523` `EndMainMenuBar` 앞 |
| Hierarchy 팝업 | `HierarchyWindow.cpp:186` `EndPopup` 앞 |
| Content Browser 팝업 네 곳 | `ContentsBrowserWindow.cpp:266,340,473,553` |

Content Browser는 같은 이름 `"Context Menu"` 팝업이 세 벌이라(`:252,:447,:527`) 폴더 호스트와 자산 호스트로
가르는 작업을 같은 슬라이스에서 한다. 기존 인라인 항목은 지우지 않고 **덧붙이기만** 하므로 이 단계의
회귀 위험은 낮다.

**빈 뿌리는 그리지 않는다.** 등록된 항목이 0인 `top_menu_root`는 `BeginMenu` 자체를 호출하지 않는다.
그래서 M1이 착지해도 항목을 태우기 전까지 **shell 픽셀이 움직이지 않고**, W0의 visual golden을
무효화하지 않는다. 팝업도 같다 — 항목 0인 호스트는 구분선조차 추가하지 않는다. 이 규칙이 M1과 W0의
순서 제약을 없앤다.

스레드 규약이 하나 있다. 메뉴 콜백은 PresentationThread에서 `m_sceneStructureMutex` 아래 돈다
(`EditorMain.cpp:320-372`의 `PresentFrame`·`OnGui`·`RenderMenuBar` 사슬). 지금 File>Save가 그 스레드에서
`SceneManagers`를 직접 만진다(`MenuBarWindow.cpp:243`). `editor::` 동작은 씬을 직접 만지지 않고
`ConsoleCommandSystem::EnqueueStructured`로 게임 스레드에 넘긴다. **이때 completion을 반드시 넘긴다.**
넘기지 않으면 배치 큐로 가서 `CommandSession::Batch()`에 누적되고 에디터 프로세스의 종료 코드를 오염시킨다
(`ConsoleCommandSystem.cpp:638,856,872`. LC5가 HTTP에서 고친 그 결함이다).

### A.7 이번 범위에서 하지 않는 것

- **단축키 바인딩.** `shortcut`은 표시 문자열이다. 지금도 `MenuItem`의 `"Ctrl+S"`는 표시일 뿐이고 실제
  핸들러가 `MenuBarWindow.cpp:747-800`에 본문째 복제돼 있다. 동작 id를 가진 이 registry는 단축키 registry의
  **선행조건**이지 그 자체가 아니다.
- **게임 코드의 메뉴 기여.** `editor::`는 `Editor/`에 산다. Dynamic_CPP 타입이 `for_editor()`를 선언하면
  런타임 헤더가 에디터 헤더를 보게 되어 L0~L4 분리를 깬다. 기여를 허용할지는
  [EngineLayerSeparationPlan.md](EngineLayerSeparationPlan.md)가 판정할 별건이다.
- **기존 19개 상단 항목·68개 팝업 항목의 이관.** 이 슬라이스는 배선을 세우고 신규만 태운다. 이관은
  W3 이후 별도 정리다.

### A.8 슬라이스와 공수

새 폴더 `Editor/EditorMenu/`에 둔다. 유니티 청크가 같은 폴더끼리만 묶이므로
(`CombineFilesOnlyFromTheSameFolder`) 기존 청크 재편을 피한다. 레지스트리 구현부는 `Commands/` 전례대로
`IncludeInUnityFile=false`로 두어 각 TU가 자기 include를 소유하는지 매 빌드가 검증하게 한다.

| 슬라이스 | 내용 | 공수 |
|---|---|---|
| M0 | `editor::` 선언 어휘(표면 열거·문맥 특성·짧은 별칭·수정자)와 중앙 목록 배관 | 1일 |
| M1 | registry와 그리기 배선 13곳, Content Browser 팝업 세 벌 통합 | 1.5일 |
| M2 | 부팅 등록, 누락·충돌·미배선 게이트, `editor.menu` 덤프와 골든, 변이 증명 | 1일 |
| | 합계 | **3.5일** |

M0은 W0과 병행 가능하고, **M1은 W3의 선행**이다. W3의 close/reopen 계약은 이 표가 서기 전에는
하드코딩 메뉴를 하나 더 만드는 것으로 끝난다.

### A.9 M0 착지 기록 (2026-09-10)

`Editor/EditorMenu/`에 `EditorMenuSurface.h`(표면·대상·문맥 특성),
`EditorMenuSchema.h`(선언 어휘), `EditorMenuRegistry.{h,cpp}`(표와 낮추기),
`RegisterEditorMenuManual.h`(중앙 목록), `EditorMenuSelfTest.{h,cpp}`가 섰다. 두 `.cpp`는
`IncludeInUnityFile=false`이고 `$(ProjectDir)EditorMenu\`를 include 경로에 더했다.

**설계와 달라진 것 넷.** 구현하며 드러난 제약이라 계약 쪽을 고쳤다.

1. **상단 저장소가 하나가 아니라 둘이다** — `global_items`와 `selection_items`. A.4는 두 서명을
   한 목록으로 접는 것처럼 읽혔는데, 접으려면 사용자 술어를 람다가 붙잡아야 한다. 항목은
   `std::string_view`를 품어 **구조적 타입이 아니라** NTTP로 넘길 수 없고, 붙잡으려면
   `std::function`이 필요해져 CT6-a가 걷어낸 타입소거가 되돌아온다. 그래서 접지 않고 나눠 든다.
   선택 해석은 그리는 자리가 프레임당 한 번 한다(M1). 서명이 결속을 선언한다는 계약은 그대로다.
2. **`action_id`는 경로가 아니라 함수 이름이다.** `__FUNCSIG__`에서 뽑는다. 메뉴 라벨을 바꿔도
   id가 살아남아야 뒷날 단축키 바인딩이 그 위에 설 수 있다. 표기 변화는 `EditorMenuRegistry.cpp`의
   카나리아 `static_assert` 둘이 잡는다(자유 함수 경로·정적 멤버 경로). MetaSchema.h가 같은 이유로
   같은 장치를 둔 것을 복제했다 — 계층 경계 때문에 공유하지 않는다.
3. **선언자 이름은 X매크로의 `#T`가 준다.** 타입 이름을 또 `__FUNCSIG__`로 뽑지 않는다. 등록 경로가
   중앙 목록 하나뿐이므로 이름의 출처도 하나면 된다.
4. **자가 검사가 표를 비운다.** `run_editor_menu_selftest`는 표가 비어 있을 때만 돌고 끝에서
   비운다. 그래서 부팅 순서는 **자가 검사 → `register_editor_menus()`**이고, 비어 있지 않으면
   거짓과 사유를 돌려준다. M2가 이것을 게이트에 이을 때 순서를 지켜야 한다.

**M0이 증명한 것과 못 한 것.** 컴파일 타임으로는 표면 열거 완전성, 호스트별 문맥 타입 일치,
이름 추출 표기를 단정한다. 런타임으로는 자가 검사가 선언이 표가 되는 전 구간을 본다 — 표면대로
갈라졌는가, `action_id`가 함수 이름을 물었는가, `invoke` 포인터가 그 함수로 이어졌는가, 문맥이
전달되는가, 수정자와 술어가 실렸는가. **아직 없는 것은 그리기다** — 표에 든 항목이 화면에 나오는지는
M1이 배선한 뒤에야 증명된다. 그때까지 이 배선은 제품 동작을 하나도 바꾸지 않는다(중앙 목록이 비어 있다).

**변이 증명 (2026-09-10).** 첫 실행부터 초록인 검사는 통과로 세지 않는다. 저장소 원본을 건드리지
않고 사본에 한 곳씩 변이를 넣어 컴파일·실행했다. 여섯 전부 **의도한 단정 자리에서** 붉어졌다.

| 변이 | 결과 | 붉어진 자리 |
|---|---|---|
| 대조군(변이 없음) | RUN_PASS | 잔여 top 0 · popup 0 |
| asset 함수를 hierarchy 호스트에 붙임 | COMPILE_FAIL | `EditorMenuSchema.h:183` 문맥 불일치 단정 |
| `action_name_raw` 오프셋 `+2`를 `+1`로 | COMPILE_FAIL | `EditorMenuRegistry.cpp:31,36` 이름 카나리아 둘 |
| 선언에서 `.order(7)` 제거 | RUN_FAIL | "order 수정자가 실리지 않았다" |
| 상단 전역 항목의 `invoke` 람다 본문 비움 | RUN_FAIL | "invoke 포인터가 선언한 함수로 이어지지 않았다" |
| `popup_host` 열거자만 더함 | COMPILE_FAIL | `EditorMenuSurface.h:87` 배열 완전성 단정 |
| `top_context_t`의 전역·선택 판정 뒤집음 | COMPILE_FAIL | `EditorMenuRegistry.h:118,127` 낮추기 자리 |

★ 하네스가 먼저 자기 증명을 하게 만든 것이 결정적이었다. 첫 시도에서 컴파일러가 **아예 실행되지
않았는데**(경로 인용 파손) 결과는 전부 `COMPILE_FAIL`로 나왔다 — 대조군이 없었으면 "여섯 변이를 다
잡았다"는 정반대 결론을 낼 뻔했다. 변이 하네스에는 변이 없는 대조군이 반드시 함께 있어야 한다.

---

## 부록 B. 셸 크롬과 창 선언 계약 (`editor::`)

- 추가: 2026-09-10. 부록 A와 같은 `editor::` 계통이고 같은 관례를 쓴다 — 파일명 PascalCase,
  식별자 snake_case, 선언 함수 `for_editor()`, 중앙 목록 하나가 인스턴스화와 등록을 겸한다.
- B.1~B.2는 **착지 기록**이고 B.3~B.4는 아직 서지 않은 **계약**이다.

### B.1 M3 착지 기록 — 셸이 프레임을 소유한다 (2026-09-10)

**결정한 원칙 하나.** 셸이 `Begin`/`End`와 식별자와 플래그와 표시 상태를 소유하고, 창 본문은
지금처럼 ImGui를 직접 부른다. 본문을 추상 레이어 뒤로 밀지 않는다.

이 판단의 근거는 격리 실측이다. `Engine/`의 `ImGui::` 호출은 **0건**이고(검색에 걸리는 두 건은
`RuntimeSettings.cpp:118-119`의 YAML 설정 키 문자열이다), `Player`는 `IImGuiHost` 경계로만 닿는다.
ImGui 직접 호출이 에디터 계층 밖으로 새지 않으므로 직접 호출 자체는 결함이 아니다. 결함은
**프레임을 창마다 각자 여는 것**이고, 그것만 셸로 올린다.

**착지 범위.**

| 항목 | 내용 |
|---|---|
| 제목표시줄 | `WM_NCCALCSIZE`로 상단 캡션만 제거. 좌·우·하단 프레임은 Windows 계산값 유지 |
| 배치 | 오른쪽 열 전체 높이(Hierarchy/Inspector), 하단 자산 브라우저 계열, 가운데 뷰포트 탭 |
| 스타일 | s&box 계열 다크 스킨. 밝은 팝업 우회로 제거 |
| 창 제목 | `<프로젝트 이름> - Creator Engine`. 값이 바뀔 때만 `SetWindowText` |
| 프레임 정보 | 씬뷰 우상단 오버레이(드로 리스트) |
| 버전 | Help > About |
| 백엔드 노브 | GUI에서 제거. `build.render.backend` 하나만 남음 |

**엔진은 손대지 않았다.** `WindowDesc.messageInterceptor`가 이미 열려 있어 캡션 제거는 에디터가
꽂는 가로채기 하나로 끝났다. `CoreWindow`는 창 생성 정책을 프로세스 호스트에게서 받기만 하므로
Player의 테두리 없는 전체화면 창은 영향을 받지 않는다.

**스레드 계약.** `HandleWindowMessage`는 WndProc가 도는 메인 스레드 전용,
`DrawTitleBarTail`은 ImGui가 도는 표시 스레드 전용이다. 둘 사이에 흐르는 값은 끌기 영역 좌표
셋뿐이라 원자값으로 닫았고 **WndProc는 ImGui 상태를 읽지 않는다**. 제목 문자열은 아예 흐르지
않는다 — 양쪽이 같은 출처에서 각자 조립하므로 어긋날 자리가 없다. 창 조작(최소화·최대화·닫기)은
표시 스레드에서 `PostMessage`로 넘겨 메인 스레드가 실행한다.

**백엔드를 코드로 못 박지 않은 이유.** "에디터는 DX12 고정"을 코드 상수로 만들면
`verify-pbr-wiring-baseline.ps1`이 눈먼다 — 그 게이트는 `EngineSettings.asset`의 `render.backend`를
직접 고쳐 써서 에디터를 Vulkan으로 몰아 제품 캡처를 받는다. 못 박으면 Vulkan 축이 초록을 유지한
채 DX12를 재게 된다. 그래서 **GUI 노브만 없애고 파일 키는 하네스 전용 재정의로 남겼다.**
저장할 때는 사람이 고른 값이 아니라 지금 돌고 있는 백엔드를 되쓴다 — 재정의가 그대로 왕복하고,
키가 없는 새 프로젝트에서도 한 번은 씨앗이 선다(그 게이트는 이 키가 정확히 한 번 나타날 것을
단정하므로 0건도 실패다).

### B.2 M3이 측정으로 드러낸 것

**테두리 없는 창의 게이트는 히트테스트 12점이다.** `SendMessage(hwnd, WM_NCHITTEST, 0, MAKELPARAM)`로
제목표시줄 빈 자리·메뉴 위·창 버튼 위·네 테두리·네 코너·본문을 찍어 기대값과 대조한다.
스크린샷은 "떴다"만 말하지만 이 게이트는 **끌 수 있는가·크기를 바꿀 수 있는가**를 말한다.

이 게이트가 결함 둘을 잡았다.

1. **위쪽으로 크기를 늘릴 수 없었다.** 캡션을 지우면 그 자리가 클라이언트가 되어 Windows가
   `HTTOP`이 아니라 `HTCLIENT`를 답한다. `HTTOP`·`HTTOPLEFT`·`HTTOPRIGHT`를 직접 되살려야 한다.
   나머지 세 테두리와 두 하단 코너는 Windows 판정을 그대로 쓰면 된다.
2. **최대화 상단에 31px 죽은 띠가 남았다.** Windows가 준 `top`을 그대로 쓰면 캡션 높이까지
   들어온다. 넘침을 막을 테두리 두께만 남기는 것이 정답이고, **클라이언트가 작업 영역과
   정확히 일치하는지**로 잰다.

**리사이즈 크래시는 이 작업의 것이 아니었다.** 창 크기를 바꾸면 세 번째에서 프로세스가 죽었다.
크롬의 메시지 가로채기만 끄고 나머지 트리는 그대로 둔 대조군 빌드가 **동일하게 세 번째에서
죽는 것**을 보이고서야 원인이 밖에 있음이 확정됐다. 실체는
`DX12DeviceResources::DrainForLifecycle`의 지름길이 `SwapChainResize`까지 태운 것이었고,
백버퍼가 셋이라 링이 한 바퀴 도는 세 번째에 터졌다. 별도 커밋으로 수정했다.

**유니티 블롭에 파일 하나를 더하면 전이 include에 기대던 파일이 깨진다.**
`EngineGUIWindow`에 `.cpp` 하나를 넣자 `ImGuiDrawHelperMeshRenderer.cpp`가 `Material` 불완전형으로
죽었다. 그 파일은 `Material.h`를 include한 적이 없고 같은 블롭의 앞선 파일이 공급하고 있었다.
**W3/W4가 창 파일을 재편할 때마다 같은 폴더 전체가 재검증 대상이다.**

**미해결.** 리사이즈 뒤 씬 뷰포트가 검게 남는다. 프레임률은 정상이고 로그에 오류가 없다.
스왑체인이 아니라 화면 크기 종속 렌더 타깃 쪽으로 보이며, `ScreenSizedResource`를 다른 세션이
편집 중이라 건드리지 않았다. **W4의 선행이다** — 뷰포트 extent를 다루기 전에 닫혀야 한다.

### B.3 M4 계약 — 창 선언

**문제.** 창 25개가 두 기구로 갈려 있다(About 창이 원래 계수 뒤에 생겼다). `ContextRegister` 10곳은 표를 거쳐 그려지고,
직접 `ImGui::Begin` 15곳은 아무 표에도 없다(그중 하나는 셸 자신이라 창이 아니다).
표시 상태 저장소는 §1.3의 정정대로 셋이다. 그래서 지금은 "창이 몇 개인가"에조차 답할 수 없고,
W0의 `editor.windows`는 덤프할 표가 없으며, W3의 안정 ID는 붙일 자리가 없다.

**형태는 부록 A를 그대로 물려받는다.** 링크 제약이 같기 때문이다 — `Editor.vcxproj`는 네 구성
전부 StaticLibrary이고 `/WHOLEARCHIVE`가 0건이라 참조 없는 TU의 자기 등록자는 조용히 사라진다.
"선언만 하면 나타난다"는 여기서도 성립하지 않으므로 **중앙 목록의 include가 인스턴스화를 끌어오고
열거가 등록을 돌린다**.

**선언에 담을 것.** 지금 어디에도 없어서 표시 문자열에서 역산되거나, 복제되거나, 아예 없는 것들이다.

| 필드 | 지금 어디에 있나 | 왜 선언이 들어야 하나 |
|---|---|---|
| 안정 식별자 | 없음 — 표시 문자열이 겸한다 | §1.4의 `###` 규칙이 붙을 자리 |
| 표시 라벨(아이콘 포함) | `Begin` 인자 리터럴 | 라벨을 바꿔도 배치가 살아남아야 한다 |
| 역할 | 없음 | §4.1 window role. 중앙·패널·일시 표시를 가른다 |
| 기본 도크 노드 | `BuildInitialDockLayout` 하드코딩 | §4.4 preset 4종이 데이터가 된다 |
| 닫기 가능 여부 | 저장소 셋에 흩어짐 | 저장소를 하나로 모으는 자리 |
| 기본 열림 + 영속 | 없음(전부 비영속) | W3의 workspace 저장이 읽을 값 |
| 최소 크기·창 플래그 | 각 `Begin` 호출부 | 셸이 프레임을 소유하므로 셸이 알아야 한다 |
| 의존 | 없음 | 애니메이터 창 셋은 선택이 있어야 존재한다 |
| 선언 순서 | `unordered_map` 순회(비결정적) | §1.3-4의 결정적 순회가 이것으로 대체된다 |

**이관은 프레임만 옮긴다.** 본문 코드는 있던 자리에 그대로 두고 `Begin`과 `End`만 셸로 올린다.
`MenuBarWindow.cpp` 분할은 별건이다.

**부분 이관을 하지 않는다.** 열다섯이 계속 직접 `Begin`하면 "등록됐는데 그려지지 않는 고아 0"과
"도크 빌더 이름과 `Begin` 이름 불일치 0"을 전역으로 단정할 수 없다 — W0 canary가 요구하는 바로
그 불변식이 서지 않는다.

**시험대 넷.** 어휘가 충분한지는 이 넷이 판정한다. 애니메이터 창 셋(Event · Animation Controllers ·
AvatarMask)은 선택 종속이라 의존 선언을 요구하고, `EditorModelPlacement`의 "Model loading"은
`NoSavedSettings`를 의도적으로 쓰는 일시 표시라 역할 필드가 그것을 표현할 수 있어야 한다.

### B.4 슬라이스와 공수

| 슬라이스 | 내용 | 공수 |
|---|---|---|
| M3 | 셸 크롬 — 제목표시줄·배치·스킨·창 제목·프레임 오버레이·버전 메뉴·백엔드 노브 | 1.5일 **(착지)** |
| M4 | 창 선언 어휘와 셸의 프레임 소유 · 창 25개 이관 · 게이트와 `editor.windows` 덤프 · 변이 증명 | 3일 **(착지)** |
| | 합계 | **4.5일** |

M4의 3일은 내역이 있다. 선언 어휘와 셸의 프레임 소유가 1일, 창 25개 이관이 1.5일, 게이트와
덤프와 변이 증명이 0.5일이다. 이관이 절반인 이유는 창이 두 경로로 갈려 있고 그중 7개가
2,716줄 파일 안에 묻혀 있기 때문이다 — 처음 어림한 2일은 그 분열을 세기 전의 값이었다.

---

### B.5 M4 1·2단계 착지 기록 (2026-09-10)

**1단계 — 선언 어휘와 셸의 프레임 소유.** `Editor/EditorWindow/` 아홉 파일. 표면·표기·표는
std만 의존하고 ImGui를 아는 파일은 셸(`EditorWindowHost`) 하나다. 표가 비어 있어 제품 동작
변화는 0이었다. 자가 검사에 변이 여섯을 먹여 전부 붉게 만들었고 무변이·복원 대조는 초록이었다.

**2단계 — `ContextRegister` 열 곳 이관.** 선언자 둘(`editor_panel_windows` 다섯,
`editor_tool_windows` 다섯)로 내려왔다. Editor 제품 코드에 `ContextRegister` 0건,
`ContextUnregister` 0건, `GetContext` 0건이다. 본문은 창 클래스가 있던 자리에 그대로 두고
생성자가 `bind_window_body`로 건다 — 계획서가 정한 "프레임만 옮긴다"를 글자 그대로 지켰다.

**배치가 보존되는 근거는 실측이다.** 셸은 `표시 이름###안정 식별자`로 `Begin`을 부르는데,
안정 식별자를 아직 표시 이름 그대로 두면 창 id가 이관 전과 같다. 벤더 `imgui.lib`에 직접 물어
`ImHashStr("이름###이름") == ImHashStr("이름")` 임을 확인했다. 식별자를 `Editor.Hierarchy` 꼴로
바꾸며 legacy ini를 이주시키는 것은 W3의 몫이다(§5.3).

**전수 재실측이 뒤집은 것 넷.**

| 앞서 적은 것 | 실제 |
|---|---|
| `isPopup`이 팝업을 만든다 | `Begin`에 `&m_opened`를 넘길지만 가른다 — 그대로 `closable`이다 |
| 등록 기본 플래그가 곧 성질이다 | Hierarchy는 `NoMove` 하나, Inspector는 거기에 `NoBringToFrontOnFocus`와 **`NoFocusOnAppearing`**. 마지막 하나가 성질 열거에 없어 열둘→열셋으로 늘렸다 |
| 닫기 가능 여부는 정적이다 | 자산 브라우저는 본문 첫 줄이 매 프레임 `SetPopup(스타일 == 타일)`을 부른다. `closable_when(술어)`를 더했다 |
| `display_back`은 Scene·Game뿐 | Hierarchy·Inspector도 본문 첫 줄에서 부르고 있었다 |

**결함 하나를 닫았다.** `ImGuiRegister::GetContext`는 `operator[]`라 없는 이름을 읽으면 빈 항목을
영구히 삽입했고 그 항목은 매 프레임 순회에 얹혔다. 실물이 살아 있었다 — `MenuBarWindow.cpp`의
`"EffectEdit"`은 이 저장소 어디에서도 등록된 적이 없는데 Settings 메뉴가 그것을 읽는다. 새 창구
`open_window`/`close_window`/`is_window_open`은 **없는 이름에 아무 일도 하지 않는다.**

**정정 — 착지 판정을 한 번 잘못 냈다.** 2단계 확인 중에 "도킹된 패널이 화면에 보이지 않고 씬 뷰
오버레이도 사라졌다"고 적고 다른 세션의 미커밋 렌더링 작업을 원인 후보로 지목했으나, **거짓이었다.**
화면 캡처 도구가 DPI awareness를 선언하지 않아 `PrimaryScreen.Bounds`가 논리 해상도를 돌려줬고
`CopyFromScreen`이 물리 픽셀 좌상단만 잘라 담았다. 가상 화면은 3840x3360인데 2560x1440만 담겨
오른쪽 열과 하단 패널이 통째로 잘렸다. 대조군 빌드까지 돌렸지만 같은 크롭을 두 번 본 것이라
아무것도 가리지 못했다. DPI를 선언하고 `SM_XVIRTUALSCREEN`~`SM_CYVIRTUALSCREEN`으로 다시 찍자
열 창이 전부 제자리에 있었다. **관측 도구의 좌표계를 먼저 검산하지 않으면 제품 결함으로 오독한다.**
에디터 자체는 이미 per-monitor v2 매니페스트를 켜고 있어 손댈 것이 없었다.

**남은 것.** 3단계(직접 `ImGui::Begin` 15개 이관)와 4단계(표시 상태 저장소 통합 ·
`ImGuiRegister` 렌더 루프 은퇴 · `editor.windows` 덤프 · 고아 0 게이트).

---

### B.6 M4 3단계 착지 기록 — 직접 `Begin` 열둘 (2026-09-11)

**옮긴 것 열둘.** 뷰포트 둘(Scene · Game), 저작 셋(Behavior Tree · BlackBoard ·
InputActionMaps), 진단 셋(FrameProfiler · Log · RenderPass Debug), 대화 넷(About ·
Build Scene Setting · Grid Settings · Model loading). 선언자는 넷으로 나눴다 —
표면이 다르면 `editor.windows` 덤프가 갈래로 읽힌다. 부팅 덤프에서 표에 22개가 실렸고
전부 `draw=1`, 여는 상태는 이관 전과 같은 6열림 / 16닫힘이었다.

**어휘 하나가 늘었다.** `initial_size(너비, 높이, 조건)` 이다. 열다섯을 전수로 세어 보니
`Begin` **앞에** `SetNextWindowSize`를 부르는 것이 일곱이었고 조건은 둘뿐이라
(`FirstUseEver` 여섯, About 하나가 `Appearing`) 닫힌 열거 `size_policy` 로 들어왔다.
최소 크기 제약과 다른 축이다 — 이쪽은 한 번 정하고 사용자가 옮기면 그만이고,
저쪽은 계속 강제된다. 자가 검사 ⑩ 이 그 둘이 같은 칸을 쓰지 않는지까지 본다.
변이 여섯을 먹여 전부 붉게 만들었고 무변이·복원 대조는 초록이었다.

| 변이 | 무엇을 망가뜨렸나 |
|---|---|
| `i_drop_size_policy` | 조건이 표기에서 표로 오지 않는다 |
| `j_width_from_min` | 너비가 최소 크기 칸에서 온다 |
| `k_drop_height` | 높이가 실리지 않는다 |
| `l_flip_default_policy` | 기본 조건이 `Appearing` 으로 뒤바뀐다 |
| `m_ignore_policy_arg` | 수정자가 조건 인자를 무시한다 |
| `n_swap_wh` | 너비와 높이가 뒤집힌다 |

**프레임을 셸에 넘기며 갈라야 했던 것 둘.** Behavior Tree와 BlackBoard는 `if (열림)` 의
`else` 가지에 **창이 닫힐 때 한 번 돌아야 하는 정리**를 달고 있었다 — 전자는 편집기 문맥을
파괴하고 `.bt` 파일을 쓰며, 후자는 편집 중이던 블랙보드를 비운다. 셸은 닫힌 창의 프레임을
열지 않으므로 그 가지는 본문이 될 수 없다. 그래서 각각 매 프레임 도는 정리 함수와 셸이 부르는
본문으로 쪼개되, 몸통은 하나로 두었다 — 두 경로가 같은 함수 지역 `static` 을 나눠 쓰므로
쪼개면 그래프와 편집기 문맥이 서로 다른 저장소가 된다.

**죽은 코드 넷이 드러났다.** `useWindow`(항상 참인 전역, `else` 가지가 죽어 있었다),
`editWindow`(소비자 0), `gizmoWindowFlags |= NoMove`(초기값에 이미 같은 비트),
`GizmoRenderer::EditorView`(본문이 그리드 설정 창 하나뿐이라 창이 선언으로 가자 빈 함수).
넷 다 지웠다.

**스타일 밀기는 `Begin`이 소비하는 것만 갔다.** Scene 뷰는 프레임 앞에 넷을 밀고 있었는데
창 배경과 창 여백 둘만 선언이 들고, 항목 간격과 버튼 색은 본문 항목에 걸리므로 본문에 남았다.
그래서 본문의 `PopStyleVar(2)` 는 1로, `PopStyleColor(2)` 는 1로 줄었다.

**남은 셋과 그 이유.** 애니메이터 창 셋(Event · Animation Controllers · AvatarMask)은 본문이
`ImGuiDrawHelperAnimator(Animator*)` 안에 중첩돼 호출자 지역 변수(`animator`, `animationIndex`,
`clipOverride`)에 매달려 있다. 셸이 부를 수 있으려면 그 상태가 먼저 게시된 편집 문맥이어야
하므로 3b로 분리한다.

**남은 것.** 3b(애니메이터 셋)와 4단계(표시 상태 저장소 통합 · `ImGuiRegister` 렌더 루프
은퇴 · `editor.windows` 덤프 · 고아 0 게이트).

---

### B.7 M4 3b 착지 기록 — 애니메이터 셋과 "선택 종속" 시험대 (2026-09-11)

**이관이 끝났다.** 창 스물다섯이 전부 선언에 있다. Editor 트리에 남은 `ImGui::Begin`은 셋뿐이고
전부 제자리다 — 셸(`EditorWindowHost`), 도크스페이스 숙주(`EditorRenderer`), 그리고 4단계가
은퇴시킬 옛 `ImGuiRegister` 루프(`ImGuiHelper/ImGuiContext.h`, 지금 등록 0건).

**시험대가 요구한 것은 새 어휘가 아니었다.** 계획서는 애니메이터 창 셋을 "선택 종속이라 의존
선언을 요구한다"고 적었다(§부록 B.3). 실제로 필요했던 것은 이미 있던 존재 술어였다 —
`available(&animator_selected)` 하나로 끝난다. 의존을 선언 문법으로 표현하려던 계획은 취소한다.

**호출자 지역 변수는 게시하지 않고 다시 유도했다.** 셋의 본문은
`ImGuiDrawHelperAnimator(Animator*)` 안에 있었고 그리는 대상은 그 인자였다. 인자를 어딘가에
얹어 두면 게시 시점과 그리기 시점 사이에 개체가 사라질 수 있다. 대신 창이 매 프레임 씬에
직접 묻는다 — 선택 개체의 Animator가 곧 인스펙터가 그리던 그것이므로 값이 같고 수명 틈이 없다.
게시가 필요한 것은 창 **둘을 건너는** 값 하나뿐이었다: 아바타 마스크가 여는 컨트롤러 번호.
나머지 세션 상태 아홉은 컨트롤러 창 본문의 함수 지역 `static`으로 남았다(전수로 확인했다 —
그 본문 밖에서 읽는 곳이 없다).

**중첩이 만든 종속 셋이 사라졌다.**

| 창 | 옛 자리 | 사라지던 조건 |
|---|---|---|
| Event | "animations" 접힘머리 안 | 머리를 접으면 |
| Animation Controllers | 그 아래 블록 | — |
| AvatarMask | 컨트롤러 창의 Layers 탭 안 | 부모를 닫거나 다른 탭으로 옮기면 |

이제 셋은 형제이고 사라지는 조건은 자기 대상이 없을 때뿐이다. 형제가 되면서 검사 하나가
생겼다 — 옛 코드는 컨트롤러 번호를 **쓰는 자리 바로 위에서 대입**했으므로 범위 검사가 없었다.
창이 독립하면 선택이 바뀌어 번호가 범위를 벗어난 채 남을 수 있어 존재 술어가 그것까지 본다.

**본문 저장소를 쓰지 않았다.** 앞선 스물둘은 `bind_window_body`로 본문을 걸었는데, 그것은
본문이 객체의 것이라 `this`를 물어야 할 때 필요한 장치다. 이 셋의 본문은 자유 함수라 선언이
직접 부른다 — 배선 단계가 하나 줄고 "등록은 됐는데 본문이 안 걸렸다"는 상태가 생기지 않는다.
선언은 `Editor/EditorWindow/Windows`가 하고 구현은 `EngineGUIWindow`가 한다.

**확인은 양쪽을 다 봤다.** 부팅 덤프에서 표에 25개가 실렸고 전부 `draw=1`이다. 대상이 없는
프레임(SampleScene)에서 셋 다 `avail=0`이고, `Test1`을 열어 Animator를 가진 개체
(`Gunner_F_Mythic`)를 고르자 Event와 Animation Controllers가 `avail=1`로 바뀌었다. 아바타
마스크는 컨트롤러 번호가 아직 `-1`이라 `avail=0`으로 남았다 — 술어가 대상 유무와 번호 유효성을
따로 본다는 뜻이다. 열어 둔 둘이 실제로 프레임을 열었다는 것은 `imgui.ini`에 `[Window][Event]`와
`[Window][Animation Controllers]` 항목이 새로 생긴 것으로 확인했다.

**남은 것.** 4단계뿐이다 — 표시 상태 저장소 통합 · `ImGuiRegister` 렌더 루프 은퇴 ·
`editor.windows` 덤프 · 고아 0 게이트. (§B.8에서 착지했다.)

---

### B.8 M4 4단계 착지 기록 — 옛 펌프 은퇴와 배선 게이트 (2026-09-11)

**옛 펌프가 은퇴했다.** `EditorRenderer::Render`에서 `ImGuiRegister`의
`unordered_map`을 훑어 항목마다 `Render()`를 부르던 네 줄을 걷었다. 마지막까지 그것이
그리던 창이 0개가 된 뒤였다. 헤더 셋(`ImGuiRegister.h` · `ImGuiRegisterClass.h` ·
`ImGuiContext.h`)이 함께 지워졌고, 그 셋을 include하던 열넷은 전부 ImGui 우산
(`ImGuiHelper/ImGui.h`)으로 바뀌었다 — 그것들이 원한 것은 등록 창구가 아니라 ImGui였다.
빌드는 한 번에 통과했고, 그것이 곧 남은 소비자가 0이었다는 증거다. 곁가지로
DX11 백엔드 헤더(`imgui_impl_dx11.h`)와 소비자 0인 `EDITOR` 매크로도 함께 갔다.

**표시 상태 저장소가 하나가 됐다.** 정찰이 셋으로 셌던 것 중 둘은 이관이 지웠고,
남은 잔재 셋을 여기서 정리했다 — `m_bCollisionMatrixWindow`(메뉴가 켜면 다음 블록이
창을 열고 다시 끄던 한 프레임짜리 중계), `m_bShowLightMapWindow`(소비자 0),
`MenuBarWindow::ShowLightMapWindow`(호출자 0). 이제 `MenuBarWindow`에 남은 bool은
`m_bShowNewScenePopup` 하나이고 그것은 창이 아니라 팝업 중계다(`OpenPopup`을 메뉴
바깥에서 불러야 한다).

**도크 배치를 표가 정한다.** `BuildInitialDockLayout`이 이름 열 개를 손으로 적던 것을
표 순회로 바꿨다. 이름이 두 곳에 적히면 갈릴 수 있고 실제로 갈렸다 — 공백 하나가 달라
Content Browser가 도크되지 않은 적이 있다. 표를 훑으면 도크 이름과 `Begin` 이름이 같은
출처라 **갈릴 자리 자체가 없다.** 계획서가 게이트로 세려던 "도크 빌더 이름과 `Begin`
이름 불일치 0"은 단정이 아니라 구조가 됐다. 도킹 자리 열거자가 늘어나면 빌더의 노드 표가
`static_assert`로 막는다. 예외는 하나 남았다 — Tile 스타일의 Content Browser는 팝업
드로어라 도크하지 않는데, 조건이 매 프레임 갈리는 값이라 선언의 정적 자리로는 적을 수 없다.

**고아를 양방향으로 본다.** 새 `editor.windows`는 선언 표와 본문 보관소를 맞대 본다.
방향이 둘인 것이 요점이다 — 옛 `GetContext`는 `operator[]`라 없는 이름이 빈 창을
**만들어 냈고**, 새 창구는 없는 이름에 **아무 일도 하지 않는다.** 후자는 오타를
조용하게 만든다. 그래서 넷을 본다.

| 판정 | 무엇이 어긋났는가 |
|---|---|
| `orphan_bodies` | 본문은 걸렸는데 표에 그 이름이 없다 — 영영 불리지 않는다 |
| `bodyless_windows` | 표에는 있는데 본문이 걸릴 자리가 비었다 — 등록됐지만 뜨지 않는다 |
| `duplicate_ids` | 같은 안정 식별자가 두 번 — 뒤엣것이 앞엣것을 가린다 |
| `empty_dock_slots` | 아무 창도 가지 않는 도킹 자리 — 배치가 빈 노드를 만든다 |

자유 함수로 그리는 창(애니메이터 셋)은 보관소를 쓰지 않으므로 예외 목록에 **손으로**
적는다. 자동으로 알아내면 오타로 죽은 창과 구분이 되지 않는다 — 둘 다 "보관소에 없다"로
똑같이 보인다. 목록에 적지 않고 그런 창을 새로 만들면 감사가 붉어지고, 고치는 방법은
의도를 적는 것이다.

**자가 검사가 도는 세트에 들어왔다.** 창·메뉴 두 자가 검사는 "표가 비어 있을 때만"
돌 수 있었고, 그 말은 부팅 전 한순간에만 돌 수 있다는 뜻이며, 곧 **도는 세트에 넣을 수
없다**는 뜻이었다. 창 쪽은 제품 표를 옆으로 치우고 합성 선언 위에서 돌게 고쳐 살아 있는
에디터에서 부를 수 있게 했다(조기 반환 경로에도 되돌리기를 달았다). 메뉴 쪽은 목록이
비어 있어(M1 미착수) 그대로 돈다 — M1이 목록을 채우는 순간 같은 치우기·되돌리기가
필요해지고, 그때 게이트가 붉어져 알려 준다. 그것을 M1의 착수 조건에 적어 둔다.

새 게이트 `verify-editor-declaration-wiring.ps1`이 `editor.selftest`와 `editor.windows`를
한 번에 태우고 run-all에 들어갔다. **개수를 못 박지 않는다** — 창을 하나 더할 때마다
이유 없이 붉어지면 사람이 숫자만 고치고 지나간다. 대신 표가 비어 있지 않은지,
도크 자리를 쓰는 창이 실제로 있는지를 단정한다(빈 집합을 성공으로 읽는 것이 이 저장소에서
두 번 나온 실패 양식이다).

**변이 열둘.** 감사 자체에 열(고아·본문 없음·중복·빈 자리 각각의 눈멀기, 예외 목록
과다, 보관소 열거 누락, 판정에서 네 항 각각 빼기), 게이트에 둘(본문을 거는 이름의 오타,
표기 수정자가 표에 안 실림). 전부 붉었고 무변이·복원 대조는 초록이었다.

**붉은 것과 맞는 이유로 붉은 것은 다른 말이다.** 제품 변이 둘이 처음에는 게이트의 **첫**
단정에서 멈췄다 — 배치 러너가 명령 실패를 종료 코드 4로 내보내고 게이트가 그것을 맨 앞에서
보고 있었던 것이다. 게이트는 붉었지만 사람이 읽는 줄은 `exited 4` 한 줄이었고, 아래 열다섯
단정은 한 번도 돌지 않았다. 단정 열여섯 개가 붉은 경로에서는 한 개였던 셈이다. 결과 줄을
먼저 읽고 내용을 단정한 뒤 종료 코드를 마지막에 보도록 순서를 바꿨다. 종료 코드 단정은
없애지 않았다 — 내용이 전부 초록인데 프로세스가 0이 아닌 것은 그 자체로 결함이고, 이
저장소는 종료 코드를 보지 않는 게이트에 이미 한 번 데었다. 고친 뒤 다시 태우니 붉은 줄이
고칠 곳을 가리켰다.

| 변이 | 게이트가 내놓은 줄 |
|---|---|
| 본문 거는 이름에 오타 | `orphan_bodies=1 [Hierarchy_typo]` · `bodyless_windows=1 [Hierarchy]` |
| `order` 수정자가 표에 안 실림 | `창: order 수정자가 실리지 않았다` |

앞엣것이 양방향 판정의 값을 그대로 보여 준다 — 오타 하나가 **두 쪽 모두**에 흔적을 남겼고,
한쪽만 봤다면 "본문이 안 걸린 창"이 왜 그렇게 됐는지 알 수 없었다.

**하네스가 먼저 거짓말을 했다.** 판정에서 고아 항을 지우는 변이가 처음에는 잡히지
않았다. 합성 표가 결함 넷을 한꺼번에 갖고 있어서 하나를 빼먹어도 다른 셋이 가려 준
것이다. 결함을 **하나씩만** 세운 대조 넷을 더하고서야 그 변이가 붉어졌다. 큰 숫자가
좁은 커버리지를 가린다.

**M4는 여기서 닫힌다.** 다음은 W3 — 안정 식별자를 `Editor.*`로 바꾸면서 기존
`imgui.ini`의 도크 항목을 이주시키는 일이다.
