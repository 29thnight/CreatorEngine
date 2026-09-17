# 에디터 워크스페이스 · 도킹 · ViewportHost 재설계 (PHASE 21)

- 수립일: 2026-08-24
- 사전 정찰 갱신: 2026-08-30 (§1 기준선 전수 재실측 · §3.2/§7.1/§8.3/§9/§11/§12 정정)
- 재정찰: 2026-09-10 — §1 17항 재대조(15 그대로 · 2 정정) · 신규 발견 9건 · 메뉴 표면 감사.
  전문은 [EditorMenuSurfaceAndPhase21Preflight.md](../analysis/EditorMenuSurfaceAndPhase21Preflight.md).
  이 문서에는 W0/W1/W3/W5의 정정 지점과 **부록 A(메뉴 등록 배선 계약 `editor::`)**를 반영했다.
- 셸 크롬 착지: 2026-09-10 — s&box 배치·ImGui 제목표시줄·다크 스킨이 W3보다 앞서 섰다.
  기록과 남은 창 선언 계약은 **부록 B**. 이 착지가 §1.1·§1.2·§1.4의 일부를 닫았고
  §1.3의 표시 상태 저장소 수를 정정했다.
- 셸 크롬 후속: 2026-09-12 — 제목 행을 20 logical px / 글꼴 12px로 줄이고 File 왼쪽에 실행 파일의 엔진 아이콘을 표시한다.
  Play/Stop·Pause/Resume은 최소화 버튼 앞의 공통 박스에 배치하며 별도 재생 행을 제거했다. 검증: [EditorTitleBarValidation.md](../analysis/EditorTitleBarValidation.md).
- 검사 호출 규약: **2026-09-16 `run-all.ps1` 폐지.** 아래 기록의 "run-all 배선/연결" 은 폐지 전의
  역사다. 검사는 이 문서에 적힌 **이름**으로 찾고, 변경마다 닿는 것을 모아 묶어 돌린 뒤 무엇을
  돌렸는지 적는다.
- 최신 상태: **2026-09-13 현재 외관 사용자 승인·고정**. M0~M4·W0·W1(DX12)·**W3·W4**는 `done`, W2·W2-I·W2-V·W2-B는 부분 구현 `progress`다. 완료 부분과 남은 범위는 [§9.0](#phase21-current-status)을 정본으로 한다. Vulkan 대응·표시 오류는 사용자 결정으로 별도 보류한다.
- 방향: **Dear ImGui 유지 · S&Box 테마 토큰 이식 · 소수 전용 위젯만 custom draw**
- **범위: 이 페이즈는 에디터다. 에디터는 DX12 로 뜨므로 backend 는 DX12 하나다.**
  Vulkan 은 이 페이즈의 **대상이 아니다** — 미룬 일이 아니라 다른 일이다(RHI 트랙의 몫).
  2026-09-15 정정: 그 전까지 문서 곳곳이 Vulkan 을 *"별도 보류"* 로 적어 두었는데, 그
  표현은 **잔여 목록에 남아 있는 일**로 읽힌다. 실제로 그렇게 읽혀 잔여 작업을 셀 때마다
  따라 나왔다. 아래 판정문과 행렬에서 그 표현을 범위 밖으로 바로잡는다. 다만 W1 이 실제로
  Vulkan 을 돌려 본 **이력**(6기동·device loss 미수정)은 사실이므로 그대로 남긴다 —
  그것은 잔여 작업이 아니라 관측 기록이다.
- 인스펙터 후속 결정: 2026-09-11 — **W2-I 공통 속성 배치 규칙 추가 · Transform 2안 채택**.
  컴포넌트 렌더 경로 통합·상단 우선 배치·개별 비활성화 불가. 상세는 [W2-I](#w2-inspector-layout).
  2026-09-12 Inspector 스타일 슬라이스 구현·DX12 검증으로 `progress`이며 전체 완료는 아니다.
- 씬뷰 후속 계획: 2026-09-11 — **W2-V 툴바·오버레이 반응형 배치 추가**.
  동일 canvas 좌표·실측 정렬·아이콘/더보기 전환·방향 기즈모/HUD 공간 예약·입력 분리를 다룬다.
  상세는 [W2-V](#w2-viewport-overlay). 현재 툴바·기즈모·통계·crop은 구현됐고 입력/최소 높이/통합 회귀가 남아 `progress`다.
- 컨텐츠 브라우저 후속 계획: 2026-09-11 — **W2-B 내부 분할·경로/이력 탐색·New·검색/표시 도구 추가**.
  고정 디렉토리 폭과 탐색 기능의 공백을 해소한다. 상세는 [W2-B](#w2-content-browser), 2026-09-12 내부 배치·탐색 구현을 시작했다.
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

수립 당시의 기준선을 2026-08-30에 소스로 전수 대조했다. 이 절의 날짜가 붙은 기준선은
**그 당시 상태**이며 현재 구현 판정은 후속 정정과 §9를 함께 읽는다. 정찰이 뒤집은 항목은
§1.4·§1.6·§3.2·§1.9 넷이고, DPI 매니페스트의 잘못된 단정은 §3.2·부록 B.5에서 정정했다.

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
  **(2026-09-11 재확인 — 헤더가 둘이다.)** W0에서 런타임이 보고한 `IMGUI_VERSION_NUM`이 19280이라
  이 문장이 맞다. 그런데 기계에 imgui 설치본이 **둘** 있다 — 전역 classic vcpkg의
  `installed/x64-windows`는 1.91.7이고 그 판에는 `style.FontScaleMain`이 아예 없다. 컴파일에
  쓰이는 것은 저장소 매니페스트의 `vcpkg_installed/x64-windows/x64-windows`(1.92.8)다. 앞쪽을
  읽고 "계획서가 틀렸다"고 판단했다가 런타임 값에 뒤집혔다. **헤더를 눈으로 읽어 판을 정하지
  말고 `editor.theme`이 찍는 값을 보라.**

즉 W1은 “새 스케일 함수를 만든다”가 아니라 **obsolete API에서 1.92의 정식 경로로 이주한다**가 맞다.

**(2026-09-10 부분 해소, 부록 B)** `ApplyEditorStyle`을 s&box 계열 다크 스킨으로 다시 썼다.
위에 적은 수치는 전부 바뀌었다 — `WindowRounding=0`, `FrameRounding=3`, `ItemSpacing=(8,5)`,
`WindowBg`≈`#242426`, `FrameBg`≈`#191A1B`. 메뉴 팝업이 밝은 회색(0.95)이라 항목마다 검은 글씨를
눌러 담던 우회로도 걷었고, 그 행의 `PushStyleColor/PopStyleColor`가 **0/0**이 됐다.
**W1이 하려던 일은 줄지 않았다.** 값이 바뀌었을 뿐 여전히 semantic token이 아니고, 창별 예외
91건 중 메뉴 행 몫만 빠졌다. 스케일 경로와 폰트 하드코딩도 그대로다 — W1의 inventory 기준값을
**다시 세어야 한다.**

**(2026-09-11 W1 구현 반영.)** 위 스타일·폰트·배율 설명은 W1 이전 기준선이다.
현재는 semantic token, 번들 Inter/시스템 폰트 대비, `FontScaleMain`/`FontScaleDpi` 경로를
구현했다. 최종 Debug/Release 빌드와 런타임 재검증은 진행 중이며, 결과는 §9 W1과
[EditorThemeW1Validation.md](../analysis/EditorThemeW1Validation.md)에 기록한다.

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
**(2026-09-11 완전 해소.)** 마지막 한 줄을 W3까지 끌고 가지 않고 여기서 없앴다 — 방법은 "창
역할이 드로어 여부를 답하게" 가 아니라 **드로어를 없애는 것**이었다. `ContentsBrowserStyle`
자체를 걷었고(Tile 본문 + Tree 거동으로 하나), 그래서 도크 빌더 순회에 **자리 없는 창이
하나도 없다.** 스냅샷 감사의 `dock_exempt` 도 "떠 있는 창" 하나만 남았다.

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
   **(2026-09-11 해소 — M4가 먼저 했다.)** 그 펌프 자체가 은퇴했다. 지금 순회는
   `editor::window_entries_of()`의 `std::vector`이고 **선언 순서 그대로**다. `ImGuiRegister`는
   트리에서 사라졌고 남은 등장은 전부 은퇴 이력 주석이다. W0가 따로 할 일은 없다.

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

**(2026-09-11 — `###` 규칙을 창 하나에 먼저 썼다.)** Content Browser의 아이콘을 하드디스크에서
폴더로 바꿔야 했다(S&Box Asset Browser의 탭이 폴더다). 아이콘은 이름의 일부이므로 **바꾸는 순간
기존 ini의 도크 항목이 끊긴다** — 위에 적힌 바로 그 대가다. M4가 세운 `라벨###안정식별자`를
여기서 처음 썼다: 안정 식별자는 옛 하드디스크 문자열 그대로 두고 라벨만 갈았고, `ImHashStr`이
`###` 앞을 버리므로 창 id가 한 비트도 달라지지 않았다. 실측으로 확인했다 — 탭이
`<폴더>Content Browser###<하드디스크>Content Browser`로 서고 노드 6에 도크된 채이며
`dockExempt=0 undocked=0 ghost=0`이다.

그래서 지금 트리에서 **표시 이름과 안정 식별자가 갈린 창은 하나**이고 나머지 스물넷은 아직 같은
값이다. W3이 남은 스물넷을 `Editor.*`로 옮기며, 그 이주에는 legacy ini 이주가 따라붙는다(§5.3).

이 전환이 값을 한 자리가 하나 더 있다. 폰트 블롭은 서브셋이라 `IconsFontAwesome6.h`에 정의가
있다고 해서 글리프가 들어 있는 것은 아니고, 없으면 **네모 한 칸이 조용히 그려진다.** 아이콘을
고를 때마다 사람이 눈으로 확인할 일이 아니라서 `editor.theme`가 선언된 라벨 전체의 글리프 누락을
센다. 1.92의 폰트는 동적이라 `FindGlyphNoFallback`은 "아직 안 구웠다"와 "폰트에 없다"를 구분하지
못하므로 `IsGlyphInFont`를 묻는다. W1이 토큰을 갈아엎을 때 이 수가 곧바로 답한다.

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

**2026-09-14 W5 착지.** 신호 셋이 섰다 — 요청 `IsGameStart()` · 진행 `HasPendingSceneStructureChange()` ·
확정 `IsPlayCommitted()`. `BeginPlayTransaction`은 **스냅샷 → phase → 통지** 순이고 `bool`을 돌려주며,
실패하면 `ApplyPendingSceneStructureChange`가 `SetGameStart(false)`로 요청을 되돌린다(`m_isEditorSceneLoaded`도
세우지 않는다). 확정은 `Reset()`까지 끝난 뒤 맨 끝에 서고 `EndPlayTransaction`은 맨 앞에서 내린다.
실패는 `PlayFailureCount()/LastPlayFailure()`로 밖에 보이고, 실패 경로를 살아 있는 에디터에서 만들 손이
없어 `InjectPlaySnapshotFailure(n)`(CLI `play.inject_snapshot_failure`)을 검증 손잡이로 두었다.
**Undo 이중 경로는 이미 없었다** — LC6이 `MenuBarWindow`의 직접 호출을 컨트롤러로 옮겨 둔 뒤라 위 줄 번호는
낡은 인용이다. 남아 있던 것은 순서뿐이었고, 실패 주입 구간이 "실패하면 편집 이력이 산다"를 단정한다.

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

**2026-09-14 W5 착지 — 이 세 줄은 죽은 줄이었다.** 계획서대로 계측을 먼저 했다: Host 게시본
(`viewport_demand`)에 `io.WantCaptureMouse/Keyboard/TextInput`을 싣고 `editor.viewport`로 내게 한 뒤, 강제 줄을
걷기 **전**과 **후**에 같은 배치(재생·eject·possess·정지)를 태웠다. 열 표본이 전부 같았다 —
`mouse=false · keyboard=true · text=false`(keyboard는 `NavEnableKeyboard`의 `NavActive` 몫). 대입 바로 아래의
`ImGui::NewFrame()`이 `UpdateHoveredWindowAndCaptureFlags`에서 셋을 무조건 다시 계산하므로 강제는 아무것도
지우지 않고 있었다. 걷었고, 그 자리에 왜 걷었는지를 적었다. **진짜 구멍은 여기 적히지 않은 자리였다** —
`GameInput`은 창 포커스도 ImGui도 보지 않아 Alt-Tab·글자 입력·일시정지 중에도 게임 스크립트가 같은 키를
봤다. 입력 소유자는 읽을 신호가 아니라 **없던 관문**이었고, `InputManager::SetGameInputOwned`가 그 관문이다
(§W5 착지).

flag 두 줄도 초기화가 아니라 매 프레임 OR이라 **런타임에 끌 수 없다.** `ViewportsEnable`은
§1.7대로 꺼져 있지만 `EndFrame`에는 이미 `UpdatePlatformWindows/RenderPlatformWindowsDefault`
분기가 서 있다(`ImGuiHost.cpp:135`) — 죽은 분기이므로 이번 범위에서 켜지 않되, W8의 canary가
"켜지지 않았음"을 단정한다.

**2026-09-11 소스 갱신:** `ImGuiConfigFlags_NoMouseCursorChange` 설정과 `App.cpp`의
`WM_SETCURSOR` 화살표 강제를 제거했다. `ImGuiBackendFlags_HasMouseCursors`는 유지하므로
에디터 UI의 커서 모양은 ImGui backend가 갱신하는 경로를 사용한다. 빌드·실행 검증은 별도다.
이 변경으로 게임 입력의 lock/clip/visibility 복구까지 해결되는 것은 아니다. W5는 §6.2의
입력 소유권에 따라 UI 커서 갱신과 게임 캡처를 조정하고, focus loss·Eject·Stop·비정상 종료의
해제를 각각 검증한다.

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

**(2026-09-11 착지)** `editor.windows`와 `editor.menu` 둘이 섰다(M4 4단계 · M2). 앞의 다섯보다
먼저 난 셈이고, 그래서 TSV 관례를 **이 둘이 정했다** — 남은 다섯은 그 형식을 따른다. 차집합
단정은 아직 없다. 두 덤프가 다 있으므로 W0이 그것을 세우는 데 남은 것은 단정 한 줄이다.

**(2026-09-11 W0 전반 착지)** `editor.dock` · `editor.theme` · `editor.layout` 셋이 섰다. 같은
TSV 관례를 따르고, 감사가 더러우면 실패로 낸다. `editor.viewport`는 **짓지 않았다** — 근거는
아래에 적었다. 여섯 중 다섯이 선 것이고, 게이트는 `verify-editor-workspace.ps1`로 따로 세워
`run-all`에 물렸다(단정 29).

**★ 읽는 쪽과 그리는 쪽이 다른 스레드다.** `editor.windows`는 **선언 표**를 읽는다 — 부팅 때 한 번
쓰이고 그 뒤 읽기만 하므로 어느 스레드에서 읽어도 된다. 그런데 배치·스타일은 **살아 있는 ImGui
상태**이고, ImGui 프레임은 PresentationThread에서 돌고(`EditorMain::PresentFrame`) CLI 명령은
게임 스레드의 `Pump()`에서 돈다(`App.cpp:274`). 둘은 `m_sceneStructureMutex`로만 겹친다. 명령
핸들러가 `ImGui::`를 직접 부르면 `NewFrame`~`Render` 한복판의 전역 문맥을 읽는 **경합**이다.
그래서 그리는 쪽이 프레임 끝에 스냅샷을 게시하고 읽는 쪽은 사본을 본다. 뮤텍스를 들고 ImGui를
읽는 대안보다 나은 점이 하나 더 있다 — 게이트가 **한 프레임 안에서 일관된 그림**을 본다.

**★ 계측은 매 프레임 돌지 않는다.** Debug 실측으로 한 번에 0.47 ms였다(스타일 색 63개와 스칼라
28개의 이름을 문자열로 만드는 값). 진단 장치가 진단 대상보다 비싸지면 안 되므로 30프레임마다
한 번 뜨고, 명령이 읽은 뒤 다음 한 번을 요청한다. 묵은 정도는 숨기지 않는다 — 덤프가 `frame=`을
찍으므로 읽는 쪽이 언제 뜬 값인지 본다.

**★ `editor.viewport`는 W5로 넘긴다.** 그것이 실어야 할 값 셋 중 둘이 **아직 존재하지 않는다.**
committed play state는 §1.6대로 `atomic_bool` 하나뿐이고, input owner는 §1.8대로
`ImGuiHost::BeginFrame`이 `WantCapture*`를 매 프레임 true로 덮어써 밖에서 물어볼 수단이 없다.
지금 이 커맨드를 지으면 **언제나 같은 상수를 찍는 관측**이 되고, 그것은 관측이 아니라 관측
흉내다 — 이 저장소가 "저작 표면은 저작해 봐야 증명된다"로 이미 데었던 자리와 같은 모양이다.
두 신호를 만드는 것은 W5의 첫 작업으로 계획서에 이미 적혀 있다. `ScenePhase`를 싣는 일도 같이
간다. **(2026-09-14)** W4가 모드·수요를, W5가 `playState`·`playCommitted`·`inputOwner`와 UI 입력 상태
(`uiWantCapture*`·`hostFocused`·`gameCanvasClicks`)를 실었다. `ScenePhase`는 싣지 않았다 — 엔티티 단위 값이라
씬 수준 상태의 대용이 못 되고(§1.6), 확정 신호가 그 자리를 대신한다.

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
- ~~`ImGuiRegister.h`는 헤더 스스로 `#define EDITOR`를 하고 `#if defined(EDITOR)`로 갈라 놓아
  else 분기 전체가 죽은 코드다. W3에서 정리한다.~~ **(2026-09-11 해소)** M4 4단계가 그 헤더를
  걷었다. `EDITOR` 매크로가 하던 구분은 프로젝트 경계가 한다.

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
| `RowHeight` | 20 | Tree/목록/Property row (2026-09-12 밀도 보정) |
| `ControlHeight` | 20 | input/button/combobox |
| `ControlRadius` | 4 | frame/button |
| `TabHeight` | 24 | document/tool tab (기존 높이 유지) |
| `TabActiveMarker` | 2 | active/focused 표식 |
| `TreeIndent` | 20 | Hierarchy/Tree |
| `ScrollbarWidth` | 8 | panel scrollbar |
| `PanelGap` | 1 | dock splitter/border 시각 간격 |

2026-09-12 후속: 기본 글꼴 16px는 유지하면서 입력의 상하 padding을 4→2px로 줄였다.
탭 핸들은 기존 24px를 유지하며, 도킹/창 프레임/명시적 탭을 그릴 때만 4px padding을 적용한다.
컴포넌트 헤더 체크박스는 16px이며 접기 클릭 영역과 분리한다. Add Component는 실제 글꼴로 폭을 측정하고,
좁은 패널에서는 두 줄로 표시한다. 상단 메뉴의 간격은 x=10/y=6px, 팝업 여백은 8×6px로 둔다.
검증 기록: [EditorControlDensityValidation.md](../analysis/EditorControlDensityValidation.md).

추가 조정: 엔티티 태그는 이름 입력칸 끝의 Material Symbols `sell` 아이콘에서 고른다.
기존 Layer는 Physics Layer로 표시하며 드롭다운을 유지한다. Inspector의 수평 창 여백은
10 logical px로 두고, 계층 생성 버튼은 높이를 유지하면서 가로 padding만 제거한다.
제목 줄의 로고와 File 간격은 메뉴 사이 간격과 독립적으로 줄이며, Content Browser 분할선은
1px로 표시하고 8px 드래그 영역을 유지한다. 세부 검증은
[EditorEntityHeaderPolishValidation.md](../analysis/EditorEntityHeaderPolishValidation.md)에 기록한다.

2026-09-13 엔티티 헤더 후속: Microsoft Fluent Emoji 3D 이미지 9개 프리셋을 Inspector와
Hierarchy에 공통 적용했다(MIT 고지문 배포). 기본 엔티티는 Package(상자)다.
Inspector 상단 화살표는 선택 이력 탐색,
잠금 버튼은 엔티티 저작 편집을 잠근다. 아이콘·잠금은 씬 저장/복제와 Undo/Redo를 지원한다.
DX12 빌드·실제 UI 및 저작 상태 회귀 163 checks를 통과했다.
[EditorEntityNavigationValidation.md](../analysis/EditorEntityNavigationValidation.md) 참조.
Fluent Emoji 교체 및 Content Browser 이미지 검증은
[EditorFluentEmojiValidation.md](../analysis/EditorFluentEmojiValidation.md)에 따로 기록한다.
전체 W2-I 상태와 추정 공수는 유지한다.
2026-09-13 Add Component 후속: 검색·카테고리 탐색, 이미 추가된 컴포넌트 비활성화,
C# 스크립트별 Inspector와 다중 부착, 새 스크립트 생성→비동기 컴파일→원래 엔티티 자동 부착을
연결했다. 컴파일 실패 시 소스 유지/재시도, 완료 시 대상 삭제·잠금 확인, 부착 Undo/Redo를 지원한다.
검색·분류 33 checks, DX12 생성/실패/재시도/잠금/취소/삭제 회귀 415 checks와 기존 재로드·초기화
계약을 검증했다. 상세 범위·UI 증거는
[EditorAddComponentBrowserValidation.md](../analysis/EditorAddComponentBrowserValidation.md)에 둔다.
동일 후속에서 Add Component의 Fluent 이미지 아이콘, 본문 한글 fallback, SerializeField의 공통
라벨/값 열과 Float3 가로/세로 배치를 적용했다. Debug 빌드, 폰트 병합 366 checks, DX12 테마
64 checks 및 실제 한글·필드 편집·창 확대/복원 UI를 확인했다. 전체 W2-I 상태·공수는 유지한다.
Float3 후속: 축의 최소 판독 폭은 compact 표시를 기준으로 재고, 가로 복귀의 완충 폭은
각 축에 중복하지 않고 행 전체에 한 번 적용한다. 편집 정밀도와 좁은 폭의 세로 배치는 유지한다.
전체 W2-I 완료 및 전역 C# 소스 변경 감시 완료로 계산하지 않는다.

### 3.2 typography와 DPI

- 기본/heading은 Inter를 Editor resource에 license와 함께 포함하는 것을 1순위로 한다.
- Font Awesome 병합은 유지하되 text font와 icon glyph의 baseline/size token을 분리한다.
- monospace 소비자가 생기면 Consolas를 1순위로 하되 시스템 font 부재 시 bundled fallback을 쓴다.
  W1 시점에는 ImGui monospace 소비자가 없어 이 체인을 추가하지 않는다.
- 현재의 절대 Windows font path를 제거한다.
- 모든 geometry는 logical px 하나로 정의하고 `main viewport DPI × user scale`을 한 번만 적용한다.
- 폰트 크기와 geometry scaling의 출처를 통일해 double scaling을 금지한다.
  W1은 ImGui 1.92 동적 atlas를 사용하므로 배율 변경 시 atlas를 비우거나 직접 rebuild하지 않는다.

**정찰 정정 (2026-08-30).** 위 마지막 두 줄은 자체 구현을 지시하지만, 실측 결과 전제가 둘 다
어긋난다.

1. **당시 DPI 항이 0이었다.** `EnableDpiAwareness`·`GetDpiForWindow`·`SetProcessDpi*` 호출이
   코드베이스 전체에 없고, `ImGuiHost::BeginFrame`은 `io.DisplayFramebufferScale`을 `(1,1)`로
   하드코딩한다. 유일한 배율은 `EditorPreferences::GetImGuiScale()`이며 UI 슬라이더 범위가
   **0.8~1.5**였다(`MenuBarWindow.cpp:451`). 즉 당시 "150% DPI"는 사용자 배율로 흉내만 낼 수 있고
   진짜 DPI 경로를 탄 적이 없다 — W1의 판정에서 이 둘을 구분해 적는다.
2. **ImGui 1.92.8이 이미 정식 경로를 제공한다.** `style.FontScaleMain`(사용자 배율),
   `style.FontScaleDpi`(모니터 contents scale), `io.ConfigDpiScaleFonts`(DPI 변화 시 `FontScaleDpi`
   자동 갱신), `style.FontSizeBase`가 그것이다. 당시 코드가 쓰던 `io.FontGlobalScale`은 obsolete다.

**매니페스트 정정 (2026-09-11 W1).** "에디터가 이미 per-monitor v2를 선언했다"는 이전 판정은
잘못이었다. `Academy_4Q.exe.manifest`는 빌드에 배선되지 않은 파일이며, 실제 기존 EXE의
리소스 `#1`에서 추출한 manifest에는 DPI 선언이 없었다. 이번 W1에서 최소
`EngineEntry/CreatorEditor.manifest`의 `SMI/2016/WindowsSettings`·`PerMonitorV2` 선언을
`CreatorEditor.vcxproj`의 `AdditionalManifestFiles`로 연결했다. 파일 존재 확인에 그치지 않도록
실행 HWND가 `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`인지 확인하는 게이트도 추가했다.
최종 Debug/Release 빌드와 런타임 재검증 결과는 W1 검증 기록에 추가한다.

따라서 W1의 DPI 항목은 다음으로 대체한다.

- 배율의 정본을 `FontScaleMain`(user scale) × `FontScaleDpi`(monitor DPI) 두 축으로 나눈다.
  자체 곱셈 경로를 새로 만들지 않는다.
- `io.FontGlobalScale` 사용을 제거하고, `ScaleAllSizes`는 **geometry에만** 적용한다
  (`ScaleAllSizes` 주석대로 폰트는 스케일하지 않는다 — 지금의 이중 적용이 double scaling의 원인이다).
- W1은 PerMonitorV2와 `ConfigDpiScaleFonts`를 채택한다. 셸이 실제 HWND DPI를 읽어
  `NewFrame` 전에 `FontScaleDpi`를 설정하고 ImGui의 main viewport 갱신과 일치시킨다.
  geometry만 사용자 배율×DPI를 곱하고, 폰트에는 두 축을 각각 전달한다.
- ~~**`IMGUI_DISABLE_OBSOLETE_FUNCTIONS`를 켜는 것을 W1의 종료 조건에 넣는다.**~~
  **(2026-09-11 실측 정정 — 이 매크로는 제품 구성에 켤 수 없다.)** 함수만 걷는 매크로가
  아니라 **`ImGuiIO`의 레이아웃을 바꾼다**(`imgui.h`의 `struct ImGuiIO` 안 obsolete 블록에
  `FontGlobalScale`이 있다). imgui는 vcpkg가 미리 빌드한 `.lib`로 오므로 소비자 TU에만 켜면
  `sizeof(ImGuiIO)`가 갈리고 `ImGui::DebugCheckVersionAndDataLayout`(imgui.cpp:11499)의
  "Mismatched struct layout!"에서 기동이 죽는다. `Editor.vcxproj`·`HostImGuiPresentation.vcxproj`에
  켜서 **빌드 exit 0 · 기동 0x80000003**으로 실측했다. Release는 어서션이 사라져 레이아웃이
  어긋난 채 도는 쪽이라 더 나쁘다. 켜려면 imgui 자체를 같은 매크로로 다시 빌드하는
  overlay port가 필요하고 그 매크로는 Player를 포함한 **모든 소비자**에 걸려야 한다 —
  W1의 값어치에 비해 큰 변경이라 하지 않는다.
- 대신 **그 매크로가 막아 주었을 호출을 소스로 센다**:
  `Tools/regression/verify-imgui-obsolete-surface.ps1`(run-all 소속). 패턴 목록의 출처는
  매크로를 켜고 한 번 컴파일해 컴파일러에게 받은 22자리다. 인자 순서가 문제인 세 API
  (`AddRect`·`AddPolyline`·`PathStroke`)는 인자를 갈라 "flags가 마지막이 아닌" 경우만 잡으며,
  `flags` 자리에 맨 정수 리터럴을 쓴 호출은 **잡지 못한다**(게이트 머리에 적었다).

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
2. `EditorPropertyRow` — 공통 label column, mixed/disabled/error 상태. 열 폭 상한·좁은 폭 줄 전환은 [W2-I](#w2-inspector-layout)를 따른다.
3. `EditorAxisField3` — X/Y/Z 색 badge와 compact numeric input
4. `EditorModeButton` — viewport toolbar와 Play/Pause/Eject의 flat icon/active marker

씬뷰의 버튼 묶음·폭별 표시 전환·오버레이 공간 예약은 [W2-V](#w2-viewport-overlay)의 공통
배치 계층이 담당한다. 기존 `EditorModeButton`과 표준 Popup/Menu를 조합하며 custom draw family를 늘리지 않는다.
브라우저 내부 분할선·경로/검색 도구는 [W2-B](#w2-content-browser)를 따른다. 표준 ImGui의
resize·Table·Input·Menu 조합을 우선하고, 내부 API가 필요하면 기존 adapter 경계에 한정한다.

표준 Button, Checkbox, TreeNode, InputText, Combo, Menu, Tooltip, Popup은 theme token을 입힌 ImGui
widget을 그대로 쓴다. custom widget은 ImGui ID, nav, focus, disabled, clipping, tooltip, testability를
보존해야 하며 별도 input framework를 만들지 않는다.

**2026-09-15 정정 — 목록은 넷인데 아이템을 직접 등록하는 자리는 다섯이다.**
W2-1 의 소스 대조가 `ItemAdd` 를 부르는 파일을 전수로 뽑자 이렇게 갈렸다.

| 파일 | 위 목록에 | 실제 |
|---|---|---|
| `EditorSectionHeader.cpp` | 있다 | `ItemAdd` 를 부른다 |
| `EditorPropertyRow.cpp` | 있다 | `ItemAdd` 를 부른다 |
| `EditorModeButton.cpp` | 있다 | `ItemAdd` 를 부른다 |
| `EditorAxisField3` | 있다 | **부르지 않는다** — `EditorPropertyRow` 와 표준 위젯을 조합한다 |
| `EditorInspectorPanel.cpp` | **없다** | `ItemAdd` 를 부른다 — 패널 헤더의 접기 띠 |
| `ProfilerWindow.cpp` | 없다 | 부른다. PHASE 14 타임라인이라 W2 family 가 아니다(면제) |

허용 목록을 family 단위로 적은 탓에 **아이템 등록 단위와 어긋나 있었다.** 계약을 지켜야 하는
단위는 family 가 아니라 `ItemAdd` 를 부르는 자리다. 게이트는 그 자리를 소스에서 전수로 뽑아
허용 목록(넷)과 면제(하나)의 합집합과 **집합 그대로** 맞댄다 — 새 자리가 하나라도 생기면
목록 밖이라는 이유로 붉어진다. `EditorAxisField3` 를 계약 대상으로 요구하지 않는 근거도
같다: 스스로 아이템을 등록하지 않으므로 nav 커서를 그릴 주체가 아니다.

**2026-09-15 두 번째 정정 — 절마다 단위가 다르다.** W2-2 가 `clipping`/`tooltip` 절의
대상을 같은 방식으로 뽑자 **또 다른 집합**이 나왔다. 글자를 직접 그리는 자리(`RenderText`·
`AddText`)가 그 절의 강제 단위다.

| 파일 | §7.1 목록 | `ItemAdd`(nav 절) | 글자 직접 그리기(clipping 절) |
|---|---|---|---|
| `EditorSectionHeader` | 있다 | ○ | ○ |
| `EditorPropertyRow` | 있다 | ○ | ○ |
| `EditorModeButton` | 있다 | ○ | ○ |
| `EditorAxisField3` | 있다 | **×** | **○** |
| `EditorInspectorPanel` | 없다 | ○ | ○ |
| `SceneViewportOverlay` | 없다 | **×**(`InvisibleButton`) | **○** |

`SceneViewportOverlay` 는 `InvisibleButton` 위에 프레임·글자·포커스 링·tooltip 을 직접
그린다 — 실질은 custom draw 인데 `ItemAdd` 를 부르지 않아 W2-1 의 소스 축이 통째로
놓쳤다. **같은 어긋남이 기제만 바꿔 되풀이된 것이다.** 그래서 절마다 그 절의 강제 단위로
집합을 유도한다. family 하나를 여러 절이 공유해도 지켜야 할 자리는 절마다 다르다.

**`EditorModeButton` 은 소비자가 0 이다(2026-09-15 실측).** 위 표에서 이 family 는 nav·
clipping 두 절 모두의 대상인데, 저장소 어디에서도 `draw_mode_button` 을 부르지 않는다.
§7.1 은 이것을 *"viewport toolbar와 Play/Pause/Eject의 flat icon/active marker"* 로 적었지만
툴바는 위의 `SceneViewportOverlay::Button` 을 자기 안에 따로 갖고 있다. 즉 **이 family 는
제자리를 다른 구현에 내주고 죽어 있다.** 지금 지우거나 툴바를 이쪽으로 옮기는 것은
현재 외관 고정(2026-09-13 사용자 승인)과 맞물리므로 결정을 미루고, 두 게이트가 그 사실을
들고 있다 — 누가 소비자를 이으면 게이트가 붉어져 판단을 다시 요구한다.

**정찰 정정 — 넷은 백지 신설이 아니다.** 아래 승계 결정과 이관은 완료됐다.
근거는 [EditorWidgetInheritanceW2.md](../analysis/EditorWidgetInheritanceW2.md)의 실제 소비자 조사이며,
현재 외관 적용 완료와 남은 상호작용/성능 검증은 §9.0을 따른다.

| family | 기존 자산 | 확정 및 현재 상태 |
|---|---|---|
| `EditorSectionHeader` | `CustomCollapsingHeader.h` | **승계** — 기존 구현을 토큰화해 개명. 신규 작성 아님 · **착지** (2026-09-11) |
| `EditorPropertyRow` | `TableAPIHelper.h` | **승계 완료** — 원본 은퇴. `HorizontalLayout.h`는 노드 에디터 소비자 때문에 범위 밖 |
| `EditorAxisField3` | 기존 승격 대상 없음 | **신설·소비자 이관 완료** — 공통 축 색/ID·숫자 배치 |
| `EditorModeButton` | `ToggleUI.h` | **신설·ToggleUI 은퇴 완료**. `widgets.{h,cpp}`는 노드 아이콘으로 범위 밖 |
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

**2026-09-15 — 이 절은 `verify-editor-chrome-perf.ps1` 이 강제한다(W2-4 착지 참조).**
아래 세 줄 중 첫 둘은 "재는 자" 를 요구하는데, 착지 전에는 그 자가 없었다
(프레임 1.1 ms 중 이름이 붙은 것 0.015 ms).

- theme/W2 적용만으로 editor chrome p95 CPU가 기준선 대비
  `max(0.10 ms, 5%)` 이상 악화되면 원인을 기록하고 최적화 또는 rollback한다.
  → **원인을 기록할 수단이 섰다.** `editor.panelcost` 가 창 단위와 셸 구간 일곱을
  이름과 함께 내고, 게이트가 "가장 비싼 구간에 이름이 있는가" 를 단정한다.
- warm-up 뒤 정적인 shell/custom widget 경로의 frame당 heap allocation은 0을 목표로 한다.
  → **아직 자가 없다.** 이 축만 미착수로 남긴다(원문이 hard gate 가 아니라 "목표" 로
  적었으므로 W2 의 종결 조건에서 뺐다). 세우려면 프레임 경계로 구간을 가르는 할당
  계수기가 필요하고, 그것은 `editor::windows` 장부와 같은 규약으로 얹을 수 있다.
- custom draw의 vertex/index 수, draw command 수를 W0 기준선과 함께 기록한다.
  → 게이트가 W0(2026-09-11)과 나란히 출력하고 골든으로 못 박는다.
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
총 **33일**이 됐다. 2026-09-11 인스펙터 후속 **W2-I 6일(초기 추정)**로 39일,
씬뷰 후속 **W2-V 4일(초기 추정)**로 43일, 브라우저 후속 **W2-B 7일(초기 추정)**을 추가해
산정된 전체 범위는 **50일**이다.
W7 비동기 썸네일의 미산정 추가 공수는 이 합계에 포함하지 않는다.
착수 순서는 §10의 권장 순서 표를 따른다.

**(2026-09-11)** 부록 A·B의 다섯 슬라이스가 모두 착지했다 — M0~M2(3.5일) · M3(1.5일) ·
M4(3일), 합 8일. 당시 W0~W8의 산정 범위는 25일이었고, W3의 선행이던 M1·M4가 끝났다.
추가 W2-I·W2-V·W2-B는 그 25일과 별도인 6일·4일·7일이며, 이 수치는 현재 잔여 공수나 완료 실적을 뜻하지 않는다.

M3은 W3보다 앞서 섰다. 순서를 바꾼 이유는 §10에 적었다 — 요약하면, 제목표시줄과 배치와 스킨은
서로를 전제하므로 한 번에 세우는 편이 같은 파일을 세 번 헤집는 것보다 싸고, 창 본문을 건드리지
않아 W3의 대상이 줄지 않는다.

<a id="phase21-current-status"></a>

### 9.0 현재 완료·잔여 정본 (2026-09-13)

**사용자 결정: 외관은 현재 상태로 고정한다.** 색·폰트·이미지 아이콘·컨트롤 밀도·탭 크기·여백·
제목표시줄과 패널 내부 배치를 다시 시안 작업으로 돌리지 않는다. 이후 기능 이관도 이 외관을 유지한다.
잘림·입력 불능·상태 유실 같은 결함 수정과 아래에 명시된 기능 추가는 남은 범위다.
이전 날짜의 착수 전 설명보다 이 절을 우선한다. 사용자 화면 승인은 전체 타입/입력/DPI/성능 검사의 대체 증거가 아니다.

#### 완료한 적용 범위

| 범위 | 현재 완료 내용 | 근거 |
|---|---|---|
| W1 테마·아이콘·폰트 | semantic token/DPI, Material Symbols UI 아이콘, 본문 한글 fallback, 아이콘/문자 정렬, 수동 Live Code 버튼 제거 | [W1](../analysis/EditorThemeW1Validation.md), [DX12 resize](../analysis/EditorW1Dx12ResizeValidation.md), [정렬](../analysis/EditorIconAlignmentValidation.md), [한글](../analysis/EditorAddComponentBrowserValidation.md) |
| 셸·공통 외관 | 엔진 아이콘과 File 간격, 낮은 제목 행, 최소화 앞 Play/Pause 박스, 재생 전용 행 제거, 작은 입력칸·본문 여백, 원래 탭 높이 복원 | [제목표시줄](../analysis/EditorTitleBarValidation.md), [컨트롤](../analysis/EditorControlDensityValidation.md) |
| W2 공통 위젯·Hierarchy | section/property/axis/mode 4종과 기존 위젯 승계, 교차 행·선택·검색·생성 버튼·정렬 | [승계](../analysis/EditorWidgetInheritanceW2.md), [Hierarchy](../analysis/EditorHierarchyStyleValidation.md) |
| W2-I Inspector | 헤더·공통 라벨/값 열·XYZ와 Float3 복귀 기준, 태그 팝업/선택 체크, Physics Layer, 오른쪽 여백 | [Inspector 및 사용자 Float3 확인](../analysis/EditorInspectorStyleValidation.md), [엔티티 헤더](../analysis/EditorEntityHeaderPolishValidation.md) |
| 엔티티 편집 기능 | Fluent Emoji 3D 프리셋과 기본 Package 상자, 선택 이력 뒤/앞, Inspector·씬 기즈모·계층 수정/삭제 잠금, 저장·복제·Undo/Redo | [탐색·잠금 163 checks](../analysis/EditorEntityNavigationValidation.md), [Fluent Emoji](../analysis/EditorFluentEmojiValidation.md) |
| 컴포넌트·스크립트 | 검색/카테고리/아이콘, 기존 C# 부착·이름별 Inspector, 새 소스 생성→비동기 컴파일→원래 엔티티 자동 부착·실패/재시도, SerializeField 공통 배치·한글 표시 | [catalog 33 / authoring 415 / font 366 checks](../analysis/EditorAddComponentBrowserValidation.md) |
| W2-V 씬뷰 | Unreal 참조 반응형 툴바, Mathematics 기반 ImViewGuizmo의 Blender 참조 표시, Render Statistics Runtime/GPU 연결, FPS 박스 제거, 카메라를 늘이지 않는 중앙 crop·좌표 통일 | [툴바·축 조작](../analysis/EditorSceneViewportOverlayValidation.md), [crop 815 checks](../analysis/EditorSceneCropValidation.md) |
| W2-B Browser | 얇은 내부 분할선·선호 폭 저장·좁은 창 폴더 팝업, 경로/뒤·앞·상위 이동, 통합 검색/지우기, 유형/정렬/타일·목록, 새 폴더, Fluent 폴더·파일 이미지, 시작 Scene 선택 | [Browser](../analysis/EditorContentBrowserLayoutValidation.md), [Fluent Emoji](../analysis/EditorFluentEmojiValidation.md) |

완료 기능을 다시 구현 대상으로 세지 않는다. 단, 정적 유형 이미지 교체는 **비동기 에셋 썸네일 완료가 아니며**,
새 스크립트 생성 작업의 컴파일/부착은 **전역 소스 자동 감지 완료가 아니다**. 전역 감시는 별도 CoreCLR 작업이다.

#### 단계 상태와 남은 목록

| 단계 | 상태 | 남은 작업 |
|---|---|---|
| M0~M4, W0, W1 | **done** | 완료. 외관 추가 시안 없음 |
| W2 | done | 4종 구현 완료. **W2-1 키보드 탐색 · W2-2 잘라 그리기/tooltip · W2-3 상태 행렬 · W2-4 시각 기준선과 §8 성능 계약**으로 전부 착지(2026-09-15 — 네 절 모두 판정문에 자가 없었다. nav 커서는 넷 중 셋이 안 그렸고, 자르기는 넷 중 하나만 온전히 알고 있었고, 일곱 상태를 한자리에서 볼 수단은 0 이었고, §8.2 의 "원인을 기록하고" 는 프레임 1.1 ms 중 0.015 ms 만 이름이 있었다). W2-4 는 하나를 **세우지 않고 닫았다** — 시각 기준선은 W8-2 의 chrome 골든이 이미 덮고 있음을 변이로 증명했다(위젯의 테두리 한 줄을 걷자 단정 3 건으로 붉었다). 성능 쪽은 창·셸 두 층을 새로 열어 설명력 1.4% → 95.6/96.5% (게이트 `verify-editor-chrome-perf.ps1` 단정 59 · run-all 배선). ★ 그 자가 곧바로 값을 냈다 — 사용자 요청으로 Content Browser 를 기본 앞 탭으로 세우자 그 창 혼자 프레임의 82.6%(2.446 ms) 다. 원인은 §W7-5 이고 **2026-09-16 에 닫혔다**(2.446 → 0.250 ms) |
| W2-I | progress | RectTransform 최소 폭 대응, 중첩/배열 이관, **전용 드로어 15 중 12**, Import Settings(대상은 `DrawYamlNodeEditor.cpp`), 활성 정책·중복 호출·편집/저장 회귀. ★ **2026-09-16 재기준선** — 잔여를 문장에서 **이름 열둘**로 바꿨다. 그 과정에서 자를 한 번 고쳤다: `EditorPropertyRow` 는 파일 이름이고 코드가 부르는 어휘는 `editor::widgets::` 이며, 전용 드로어 열은 별도 파일이 아니라 `InspectorWindow.cpp` 안의 멤버라 **파일 단위 세기가 12 를 0 으로 읽었다.** 일반 리플렉션·C# 노출 필드 경로는 이미 정본을 지난다 |
| W2-V | progress | ★ **2026-09-16 재기준선 — 다섯 줄 중 넷이 줄었다.** 남은 넷: V2 **방향 선택 창구**(높이 축과 Stats 대체 접근은 이미 섰다 — `showGizmo`·`RenderStatistics` 팝업), V3 **resize 중 조작 취소**(기제 0 건), V0·V4 는 **구현이 아니라 판정** — V0 은 `좌표식 부재`의 소스 축, V4 는 오버레이 위치/ID/입력 변이 검출. `W4 Host canvas 통합`·`drop/terrain 관통`·`W5 소유권`·`연속 resize`·`DPI` 는 W4 후속·W5·W8-3 이 닫았다 |
| W2-B | progress | **2026-09-17 착지** — 방문 이력(검색·선택·스크롤 복원·사라진 폴더 복구)·최근/전체 가상 위치(W7 캐시 순회, 잘라 그리기로 Release 4k p95 15.9→3.25ms)·끌기 payload 전체 경로(받는 자리 17)·Volume Profile 이름 대화상자와 실패 이유. 검사 `verify-content-browser-navigation.ps1` 114 단정·변이 14 종. 프로젝트 전환(A→B→A 재기동)도 실행 검증했다. 모으기 비용도 결과 기억으로 닫았다(4k p95 3.25 → 0.053ms). 1k/10k/50k 공동 측정(`measure-panel-cost.ps1 -WithAssets`)이 대기 p95 뒤에 숨은 5 만 개 폴더의 0.5 초 멈칫을 찾아 고쳤다(max 543 → 0.41ms). 분할선은 운영체제 마우스 메시지로 끌어 네 배율에서 검증했다(`verify-content-browser-splitter.ps1` 272 단정 · 변이 4). 끌기는 타일을 Hierarchy 에 실물로 놓아 전체 경로가 받는 자리까지 가는 것을 쟀다(`verify-content-browser-drag.ps1` 14 단정 · 변이 3). 텍스처 캐시 stem 신원은 G2 로 닫았다(`verify-texture-cache-identity.ps1`). 남은 것: Volume Profile 취소(렌더 계층 개편 뒤로 보류) |
| W3 | **done** | ID·legacy 이주, 자유 dock/close/reopen, versioned save/load/reset/backup·손상 복구가 게이트 둘(169+212 checks)로 선다. Scene `no_move` 해제는 W4 단일 Host, `dock_slot` 선언화는 W6 preset 소속 |
| W4 | **done** | 닫을 수 없는 중앙 단일 ViewportHost와 모드(Scene/Game), canvas 규약 하나(crop/letterbox), 선택적 Game Preview, 가시성별 view demand가 게이트 W4-①②③으로 선다. 결함 넷(central 미표시·dock 감사 패널 면제·Game 종횡비 출처·모드 스레드 경계)을 함께 고쳤다. extent 기반 resize와 렌더 배율은 **2026-09-14 착지**(§W4 후속) — 미뤄 둔 근거였던 generation/retire 수렴을 게이트 단정으로 옮겼다 |
| W5 | **done** | 요청/진행/확정 신호 셋과 Snapshot → phase → 통지 순서, 실패 시 요청 되돌림, `Stopped/Entering/PlayingPossessed/PlayingEjected/Exiting` 컨트롤러, 게임 입력 소유 관문(포커스·글자 입력·pause·eject)과 커서 의사/적용 분리, Stop 의 문서·포커스·선택 복원이 `verify-play-roundtrip.ps1`(2 launches)·`verify-play-selection-undo.ps1` 로 선다. 기즈모 잔류는 CLI 로 못 몬다 |
| W6 | **done** | **preset 5종 + 이름 붙인 배치 착지(2026-09-15)** — 자리의 정본이 선언에서 preset 으로 옮겨 갔고(`EditorLayoutPreset.h`), 기본 preset 은 재정의 0 이라 현재 외관이 값이 아니라 **출처**로 유지된다(픽셀 392,888 중 0 차이). `dock_slot::left` 신설, 안 쓰는 자리는 노드를 만들지 않음, 패널 바닥 보존, 파일 스키마 2(v1 계속 읽음), Reset 은 **지금** preset 으로. 런타임 게이트 197 단정·변이 7 종·run-all 배선. **재지 못한 축 하나** — 최소 중앙 보존은 이 기계의 배율 2.25 에서 하한(1080x675)이 창보다 커서 제품이 늘 탈출 가지로 간다. 게이트가 그 사실을 건너뜀으로 찍는다. **W6-2(2026-09-15)** 로 Save As/Rename/Delete/목록까지 닫았다 — 이름 붙인 배치는 `<이름>.workspace` 이고 **파일 이름이 곧 이름**이다. 덮어쓰기·열기·지우기가 전부 `before-*` 백업을 남기고, 쓸 수 없는 이름은 만들 때 거절한다. 게이트 91 단정·변이 9 종 |
| W7 | progress | **W7-0~W7-3 착지(2026-09-14·15)** — 관측(`editor.panelcost`)·fixture(`scene.populate`)·측정 도구와 1k/10k/50k Release 기준선(§W7-0), Browser 스냅샷(§W7-1)으로 매 프레임 디렉터리 스캔 24 → 0, 그리고 평탄 목록+clipping(§W7-2·W7-3)으로 `hierarchy.units` 50,000 → **14** · p95 30.4 → **0.041 ms**. 실측이 두 번 판을 고쳤다: ① 브라우저가 clipping 보다 먼저였고(엔티티 1,000 에서 Hierarchy 의 5.8 배), ② W7-2 는 홀로는 이득 0 이라 W7-3 과 한 조각으로 묶어야 했다. W7-4 로 그 계약을 게이트에 걸었다(72 단정 · 변이 12 종 · run-all 배선). ★ 그런데 **W7-1 이후에도 `browser_tree` 가 avg 1.375 ms · p95 1.829 ms · max 10.40 ms 다**(2026-09-15, PHASE 14 임시 계측이 잡았다). 스캔은 0인데 비싸다 — `equivalent` 가 캐시를 경유하지 않아 W7-1 이 세운 계수에 안 잡혔다. 게임 스레드는 그 락 대기로 프레임의 **95%** 를 쓴다. **W7-5 착지(2026-09-16)** — 고친 것은 호출 하나가 아니라 **세는 단위**였다. 계약은 "프레임마다 디스크를 만지지 않는다" 인데 계수기는 "디렉터리를 훑은 횟수" 를 세어 `equivalent` 가 그 사이로 지나갔다. 축을 하나 더 열고(`probes`) 비교를 어휘로 바꾼 뒤 드래그 관문을 앞에 세웠다: `browser_tree` avg 1.375 → **0.181 ms** · p95 1.829 → **0.385 ms**, `###Editor.ContentBrowser` 2.446 → **0.250 ms**, 게임 스레드 락 대기 2.6~3.2 → **0.000 ms**. 같은 세션 A/B 라 정황이 아니라 인과다(게이트 `verify-browser-filesystem-contract.ps1` 단정 35 · 변이 8 종 · run-all 배선). ★ 그 변이 하나(M3, 옛 코드 복원)가 **런타임 축을 통과했다** — 계수기는 자기가 감싼 것만 본다. 우회 호출은 소스 축이 잡는다. 남은 것은 별도 산정인 **아이콘→비동기 썸네일 교체**·무효화/예산/퇴출/늦은 완료 처리뿐이다 **W7-6 착지(2026-09-16)** — 아이콘을 비동기 썸네일로 바꿨다. 여기서도 **판정문에 자가 없었다**: 계약이 "실제 GPU 사용 가능 시점을 따른다" 고 적어 두고도 그 값을 물을 수단이 없었다 — `RegisterTexture` 는 업로드 전에도 0 이 아닌 id 를 돌려준다. 백엔드 두 팔에 `IsTextureReady` 를 세우고(판정은 DX12 지만 **배선은 미루지 않는다**), 드러난 프라이밍 고리를 `Prime()` 으로 끊었다. 키는 계약이 적은 자산 GUID 가 아니라 경로 해시 + `revision` 이다 — GUID 는 타일마다 `.meta` 를 읽어야 해서 W7-5 가 방금 닫은 결함을 되살린다. 실측: `requests 3 · decoded 2 · failed 1 · bytes 65600`(= 128·128·4 + 4·4·4, 산술과 한 바이트도 안 어긋난다) · `deduped 1689` · `scans 0 probes 0`(W7-5 계약 유지) · `imgui-error 0`. ★ 계약의 "예산을 넘으면 버린다" 는 **자극할 수 없는 절**이었다(clipper 가 보이는 타일만 요청하므로 파일 수로는 48 MB 에 못 닿고 예산은 `constexpr` 이었다) — `editor.thumbnail budget` 으로 그 자를 열었다. 게이트 `verify-browser-thumbnail-contract.ps1` 대조 56 단정 · 변이 5 종 전부 포획 · run-all 배선. ★ 그 변이 하나(M6)는 처음에 통과했다 — 검사가 부분 문자열로 찾아 `IsTextureReadyRetired` 를 배선으로 셌다. 남은 것은 **모델 렌더 썸네일**(오프스크린 렌더) 하나뿐이다 |
| W8 | progress | **W8-1 착지(2026-09-15)** — 어댑터 판 canary와 legacy 잔재 재유입 차단. 계획서가 지목한 legacy 둘(`ContentsBrowserStyle`·ToolPanel `NoMove`)은 **이미 죽어 있었고** 남은 일은 게이트였다. 진짜 구멍은 `IMGUI_CHECKVERSION()` 이 Release 에서 힘이 0 이던 것(`IM_ASSERT`=`assert()`, NDEBUG 로 소멸, 반환값도 버림) — 이 기계엔 imgui 설치본이 둘이라 실재하는 위험이다. 반환값을 받아 던지고 `ImGui::GetVersion()`(도는 코드가 말하는 판)을 `editor.dock` 이 헤더 매크로와 함께 내보낸다. 게이트 `verify-imgui-adapter-canary.ps1` 29 단정·변이 7 종·run-all 배선. **W8-2 착지(2026-09-15)** — 3D 렌더 영역을 제외한 chrome crop visual golden. 마스킹에 필요한 제품 변경은 없었다 — W4 가 이미 `editor.sceneview` 로 image·clip 사각형과 툴바 상자 둘·기즈모 원반을 내보내고 있어, 게이트가 좌표를 한 줄도 적지 않는다. 대신 **스크롤에 닿을 길**이 없어 제품 변경 둘을 넣었다: 선택한 줄을 끌어오기(`IncludeItemByIndex` + 그 줄을 그리는 자리의 커서)와 접힌 조상 펼치기. 게이트 `verify-editor-chrome-golden.ps1` 21 단정·회차 4·변이 6 종·run-all 배선. ★ 캡처 자가 두 번째로 틀려 있었다 — `PrintWindow` 는 창 전체를 DC 원점부터 그리는데 클라이언트 크기 비트맵을 주고 있어 9~10 px 밀렸고, 그 탓에 가리기가 밀린 채 초록이었다. **W8-3 착지(2026-09-15)** — 통합 행렬. ★ 판정문 *"검증 레이어 오류 0"* 이 **출하 구성에서 잴 수 없는 것이었다**: `DrainDebugMessages` 가 통째로 `_DEBUG` 였고 라이브 호출부 둘도 같은 가드라, `CREATOR_DX12_VALIDATION=basic` 으로 레이어를 켜도 아무도 큐를 읽지 않았다(W8-1 의 `IMGUI_CHECKVERSION` 과 같은 모양). `rhi::validation` 장부를 세우고 가드를 걷고 `dx12.validation [reset]` 을 냈다. 게이트 `verify-editor-integration-matrix.ps1` 214 단정·축 12·기동 11 회·대조군 1(`=off` 에서 `layerEnabled` 가 거짓이어야 한다)·변이 4 종 전부 잡음·run-all 배선. Release·Debug 양쪽에서 12 축 모두 problems 0. **W8 네 줄 전부 닫혔다** — 이 칸이 아직 `progress` 인 것은 W8 이 선행으로 잡고 있는 W2 계열(W2·W2-I·W2-V·W2-B)이 남아 그것들이 닫힌 뒤 행렬에 축을 더해 재판정해야 하기 때문이다 |

현재 소스에서 확인한 잔여 경계:

- ~~`InspectorWindow::Render`는 공간 컴포넌트를 앞에서 그린 뒤 일반 순회에서 제외한다.~~ 2026-09-18 순회를 하나로 합쳤고
  인스턴스별 호출 수를 런타임으로 센다(§W2-I 의 "공통 순회와 편집 정책" 절).
- ~~`RectTransformTable`은 고정 90px를 없앴지만 좁은 폭에도 앵커 옆 3열 표를 유지한다.~~ 2026-09-17 표를 걷고 공통 줄로 옮겼다(§W2-I 의 "RectTransform 최소 폭" 절).
  일반 필드의 공통 열 적용만으로 RectTransform·중첩 배열·Import Settings 완료를 판정하지 않는다.
- `SceneViewportOverlay`는 공간이 부족하면 `showGizmo=false`로 숨긴다. 여섯 축 클릭은 검증됐지만
  그 상황의 방향 선택 대체 메뉴는 없다. Scene crop은 W4 기여로 기록하고 V0에서 중복 실적을 세지 않는다.
- Browser 이력은 `vector<path>`이며 `Navigate`가 검색·선택을 초기화한다. 썸네일 자리에는 현재 유형 이미지를 쓴다.
- `EditorRenderer::BuildInitialDockLayout`은 선언의 `dock_slot`을 쓰고 Tile 분기를 이미 제거했다.
  `EditorWindowNames`의 ID 이주는 W3에서 끝났고 옛 이름은 `legacy_names` 이주 표로만 남는다.
  Scene/Game을 한 Host의 표시 모드로 합치는 일은 **W4가 닫았다**(§W4 착지와 결함).
  Scene의 `no_move`는 그 Host가 가운데 노드를 떠나지 않으므로 그대로 둔다 — 걷을 이유가 사라졌다.
- `ImGuiHost::BeginFrame`의 `WantCapture*` 강제(죽은 줄)와 SceneManager의 Snapshot 이전 Play 이벤트 발행은
  **W5가 걷었다**(§W5 착지와 결함). 제목표시줄 Play/Pause 버튼은 이제 컨트롤러가 유도한 상태를 그린다.

#### 산정·검증 범위

- 산정 범위는 **50일** 유지. 닫힌 상위 단계 M0~M4·W0·W1·**W3·W4**는 **19일**이다(W3 3일 · W4 4일).
  세부 완료는 **W2-I0 0.5일**, **W2-V1 1일**만 추가 인정해 대시보드 완료 공수는 **24.5/50일(49%)**이다
  (2026-09-14 W5 4일 추가).
  W2의 자동 50% 가중치는 명시적 `earnedDays=0`으로 대체한다. 나머지 부분 구현에 임의의 실적을 배분하지 않는다.
  29.5일은 미획득 초기 추정분이며 **현재 남은 일정 견적이 아니다**. W7 썸네일 추가 견적도 아직 제외다.
- W2-I 실측 문서의 9일안은 추가 의미 정책 등을 포함한 제안이다. 현재 정본의 6일을 이번 상태 갱신에서
  자동 증액하지 않는다. 전용 드로어 및 썸네일 착수 전 남은 실작업을 재산정한다.
- DX12 테마의 대표 100→150→100%/64 checks, UI 조작, 각 기능별 게이트 기록을 근거로 한다.
  전체 폭/DPI 조합, 실제 모니터 이동, 연속 resize·모든 drop/terrain 소비자·성능은 미완료다.
- 최신 Float3은 라이브러리 빌드/링크와 사용자 화면 확인이 있고 추가 selftest는 실행 통과로 세지 않는다.
  당시 공유 런타임 배포 변경 때문에 전체 빌드가 중단된 기록도 보존한다. 이번 문서 갱신에서는 재빌드하지 않았으며
  최신 공유 작업 트리의 통합 성공은 W8에서 별도 확인한다.
- **완료 판정은 DX12 다** — 이 페이즈의 backend 가 그것 하나이기 때문이다. 원래 행렬에 있던 Vulkan 항목은 이 페이즈의 대상이 아니므로 통과로도 잔여로도 세지 않는다.
  실제 모니터 DPI와 사용자 배율도 구분한다. 사용자 승인 화면을 자동 golden 전수 검증으로 세지 않는다.

### W0 — 관측 표면 · 기준선 · 실패 게이트 (P0, 2일 · 정찰 뒤 1일→2일)

**추정 정정 근거:** §1.9대로 에디터 chrome을 밖에서 볼 CLI 표면이 0이다. 관측 커맨드를 만들지
않으면 canary가 "창이 떴다"밖에 단정하지 못한다. 원래의 1일은 측정·캡처만 센 값이다.

- ~~`editor.layout` / `editor.windows` / `editor.dock` / `editor.theme` / `editor.viewport` 관측
  커맨드를 신설한다(§1.9).~~ **(2026-09-11 착지 — 다섯.)** `editor.windows`(M4) ·
  `editor.menu`(M2) · `editor.dock` · `editor.theme` · `editor.layout`. `editor.viewport`는 읽을
  신호가 없어 W5로 넘겼다(§1.9). 이번 슬라이스에서는 **관측만** 하고 설정·저작은 W3/W6에 둔다.
  **(9-10 재정찰)** `editor.viewport`는 `ScenePhase`를 실어야 한다 — 현행 `play.state`는
  gameStart·paused·pending만 내서 스냅샷 실패 뒤의 상태가 성공과 구분되지 않는다
  (`SceneObjectCommands.cpp:1034-1057`). 관측 대상 상태의 정본은 재정찰 문서 §1.4의
  "GUI에만 있는 동작" 표다. screenshot은 기존 `Tools/regression/capture-window.ps1`을 재사용한다.
- ~~`ImGuiRegister`의 창 순회를 결정적 순서로 바꾼다(§1.3-4).~~ **(M4가 먼저 했다.)** 펌프가
  은퇴하고 순회가 선언 순서의 `std::vector`가 됐다. 도크 노드 순회도 결정적이다 — `ImGuiStorage`가
  키로 정렬돼 있어 뿌리에서 재귀로 내려가지 않고 그 맵을 훑는다(떠 있는 노드도 놓치지 않는다).
- ~~현재 `imgui.ini` 4벌을 fixture로 고정한다(§1.4).~~ **(2026-09-11 착지 — 여섯.)**
  `Tools/regression/fixtures/imgui-ini/`. 막고 있던 것은 `Bin/`이 아니라 **전면 `*.ini`**
  (`.gitignore:511`)였고, 그래서 실물 넷이 전부 한 기계의 디스크에만 있었다. 손상본을 둘로
  나눴다 — 처음 만든 절단본이 아무것도 부수지 못했기 때문이다(ImGui가 각 `[Window]`에
  `DockId=`를 따로 적어 트리가 잘려도 소속을 복원한다). 게이트가 여섯을 태운다.
- ~~핵심 window title/flags, open state inventory를 고정한다. `PushStyleColor/Var` 91건의 위치
  목록을 W1 입력으로 남긴다.~~ **(2026-09-11 착지.)** `docs/analysis/EditorWorkspaceW0Baseline.md`.
  **91건은 M3 이전 수이고 지금은 57건이다**(아래 W1에 반영했다).
- ~~1920×1080/2560×1440, user scale 100%/150%의 shell screenshot을 캡처한다.~~
  **(2026-09-11 착지.)** `docs/analysis/images/w0-shell/`. 예고한 대로 user scale 캡처이고 진짜
  DPI 캡처는 W1 뒤다. **넷 다 `imgui.ini`를 지우고 띄웠다** — `EditorRenderer.cpp:296`이
  `resetRequested || !file::exists(iniPath)`라 ini가 있으면 도크 빌더가 돌지 않고 배치를 그
  파일이 정한다. 처음에는 이것을 빠뜨려 그 기계에 남아 있던 옛 배치를 찍었다.
  **배치는 아직 유니티식 2×3이다** — S&Box식 고정 중앙 `ViewportHost`는 W4가 세우므로,
  이 캡처는 목표 배치가 아니라 **목표 이전의 모습**이고 그것이 기준선의 정의다.
  **캡처가 미결 건 둘을 드러냈다** — ⑴ 시작 창 크기를 고를 수 없다(`App.cpp:100-101`
  하드코딩, 설정의 `lastWindowSize`는 **읽는 코드가 없는 죽은 키**), ⑵ 런타임 리사이즈가
  뷰포트를 검게 만든다. 그래서 **이 넷은 visual golden이 아니다** — W8이 golden을 뜰 때 위
  둘이 먼저 닫혀 있어야 한다.
- ~~Editor UI CPU, ImGui vertices/indices/draw commands, target별 GPU ms를 기록한다.~~
  **(2026-09-11 부분 착지.)** 앞의 둘은 `editor.dock`이 낸다. **ini를 지우고 띄워 선언이 세운
  배치 위에서 잰다** — Release 표본 열둘로 정점 1,688 · 인덱스 3,858 · draw command 12,
  UI CPU 중앙값 0.38 ms. 처음에는 ini를 지우지 않아 개발자의 옛 배치(창 13 · 노드 11)를 쟀고
  값이 다섯 배 컸다(1.90 ms). **무엇 위에서 쟀는지가 수 자체만큼 중요하다.** **target별 GPU ms는 잴 수단이
  저장소에 없다** — GPU 타임스탬프 질의 표면이 0이고 `profile.stats`는 프로파일러의 건강
  상태이지 구간 시간이 아니다. 추정치로 채우지 않고 **W4로 넘겼다**(그 수를 실제로 쓰는
  판정이 거기 있다).
- ~~`verify-editor-workspace.ps1` canary를 만든다.~~ **(2026-09-11 착지.)** 선언 게이트와 **파일을
  나눴다** — 뜨는 상태가 다르기 때문이다. 선언 게이트는 개발자의 평소 상태에서 한 번 띄워 표를
  보고, 이쪽은 W0 후반에 ini fixture 네 벌과 재시작·손상 ini를 태우므로 준비된 상태로 여러 번
  띄운다. 한 파일에 섞으면 선언 검사가 남의 이유로 에디터를 다시 띄운다. `imgui.ini`가 없는
  기계에서는 항목 단정이 잴 것이 없으므로 **조용히 건너뛰지 않고** 에디터를 한 번 더 돌려
  만든다(빈 집합을 성공으로 읽지 않는다).

**판정:** 빈 측정·빈 screenshot으로 통과하지 않는다. **canary는 불변식을 단정하고 현재 개수를 단정하지
않는다** — "상단 메뉴가 셋"처럼 착수 전 상태를 지키는 단정은 부록 A의 M1이 Tools/Window를 세우는 순간
정당하게 빨개져 게이트가 제 일을 못 한다. 대신 "등록됐는데 그려지지 않는 고아 0", "DockBuilder 이름과
`Begin` 이름 불일치 0"처럼 **어느 시점에도 참이어야 하는 것**을 단정한다.
canary는 **변이로 이빨을 증명한다** —
Content Browser 이름을 한 글자 바꾸거나 central node를 지운 fixture를 넣었을 때 정확히 그 단정만
빨개져야 하고, 첫 실행부터 전부 초록이면 통과로 세지 않는다. legacy layout과 performance
baseline이 이 문서 또는 별도 analysis 산출물에 기록된다.

**(2026-09-11 전반 변이 증명)** 결함을 **하나씩만** 심고 에디터를 통째로 다시 빌드해 게이트를
태웠다. 무변이 대조군을 앞뒤에 두었고 둘 다 초록이었다.

| 변이 | 게이트가 내놓은 줄 |
|---|---|
| 선언 host 를 거치지 않고 직접 `Begin` 한 창을 기존 노드에 붙임 | `ghost=1` |
| 창 하나의 **안정 id** 를 바꿈(ini 의 도크 항목을 잃는다) | `docked=5 undocked=1` |
| 에디터 스킨 적용이 돌지 않음 | `applied=0` · `에디터 스킨이 적용되지 않았다` |
| 배율 적용 경로가 설정값과 어긋남 | `scaleMatch=0` · `배율 출처가 갈렸다` |
| 모든 고정 id 에 접미사가 붙음 | `Only 0 windows are docked; the gate would be vacuous` |
| ImGui 판을 19170 으로 되돌림 | `versionKnown=0` · 내부 구조체 읽기 재검증 지시 |

대조군은 `dock nodes=7 leaf=4 docked=6 central=0, style colors=63 differing=53 scale=0.8,
ini entries=16 matched=14, imgui=19280, checks=29`이다.

**★ 첫 변이 셋이 제 단정을 못 건드렸고, 그 이유가 전부 달랐다.** 변이가 초록이면 멈추지 말고
**왜**를 봐야 한다는 것이 여기서 세 번 값을 했다.

1. **`panel(stable_id, label)` 의 첫 인자가 안정 id 다.** 둘째(라벨)를 바꿨더니 게이트가
   초록이었는데, 그것은 **의도된 동작**이다 — `###` 규칙의 요점이 "라벨이 바뀌어도 도크가
   살아남는다" 이므로 초록이 맞다. 첫 인자를 바꾸자 `undocked=1` 이 나왔다.
2. **초기 배율 대입은 값이 살아남지 않는다.** `m_lastRequestedScale{-1.f}` 이라 첫 프레임에
   `ApplyEditorScale` 이 무조건 돌아 `io.FontGlobalScale` 을 다시 쓴다. 그래서
   `EditorRenderer.cpp` 의 초기 대입을 변이해도 관측되지 않는다. 살아 있는 경로를 변이하자
   `scaleMatch=0` 이 나왔다. 이 단정의 사정권은 **값을 실제로 정하는 경로**다.
3. **모든 id 에 접미사를 붙이는 변이는 유령 탭을 건드리지 못한다.** id 가 전부 바뀌면 ImGui 가
   어느 창도 모르게 되어 도크 노드의 탭 목록 자체가 빈다 — `ghost=0` 이고 대신 양성 확인
   (`dockedWindows >= 2`)이 잡았다. **빈 집합 위에서 도는 부재 단정**의 교과서적 사례이고,
   양성 확인을 함께 둔 것이 그것을 건졌다. 유령 탭의 실제 결함 모양(직접 `Begin` + 기존 노드에
   붙이기)으로 다시 심어 `ghost=1` 을 얻었다.

**★ 못 잡는 변이 하나를 지우지 않고 적어 둔다.** `duplicate_entries`(같은 창 이름이 ini 에 두
번)는 **제품 경로로 발화시킬 수 없다.** 이유 둘 — ① 에디터가 도는 동안 자기 ini 를 다시 쓴다
(Debug 45프레임이 약 9초이고 `io.IniSavingRate` 가 5초라, 명령이 파일을 읽을 때 심은 중복은 이미
지워져 있다). ② ImGui 는 `###` 오른쪽만 ini 키로 적으므로 같은 키가 둘이 되려면 선언에 같은 안정
id 가 둘이어야 하는데, 그것은 `editor.windows` 의 `duplicateIds` 가 선언 단계에서 먼저 잡는다.
그래서 이 단정은 손으로 고친 ini·파일 손상을 거르는 값싼 방어로만 남기고 **이빨이 증명됐다고
적지 않는다.** §1.4 가 기록한 실물 사고(Content Browser 항목이 둘로 갈림)는 이름이 **서로 다른**
두 키였고, 그 모양은 `###` 규칙(M4)이 구조적으로 막았다.

### W1 — Theme token · font/icon · DPI 정본 (P1, 2일)

**2026-09-11 기존 테마·폰트·DPI 구현 반영, Debug/Release 자동 회귀 통과.** 상세 증거와 남은 검증은
[EditorThemeW1Validation.md](../analysis/EditorThemeW1Validation.md)에 둔다.

**같은 날 아이콘 방향 확정:** 공통 UI·유형 아이콘은 **Google Material Symbols**로 교체한다.
Outlined의 59역할/54글리프 static TTF(7,920바이트), 역할 매핑·버전·codepoint·라이선스를 적용했다.
본문 Inter는 별도 역할이다. FA6 헤더·압축 블롭을 제거했고 기존 창 8개의 저장 ID 바이트는 유지한다.
Inter와의 PUA 충돌은 UI 사설 문자 영역의 3값 제외 범위로 처리한다(64값 상한 준수).
수정 후 Debug/Release 빌드와 각 구성의 DX12·Vulkan 6기동·121검사가 통과했다.
전체 59역할의 누락은 0이며 본문 폰트가 아이콘을 덮어쓰지 않는지도 검사한다.
실제 에디터 화면과 OS DPI 왕복도 검증했다. 폰트·DPI 값은 통과했지만
DX12 Scene 표시 복구 지연과 Vulkan 모니터 경계 리사이즈 후 device loss로 완료 판정을 보류했다.
재현·복원 기록은 [EditorW1InteractiveValidation.md](../analysis/EditorW1InteractiveValidation.md)에 둔다.
2026-09-12 원인 분석·수정 순서는 [EditorW1DpiResizeRootCauseAndFixPlan.md](../analysis/EditorW1DpiResizeRootCauseAndFixPlan.md)에 둔다.
사용자 요청으로 DX12를 우선 수정했다. 공통 크기 snapshot을 동기화하고, DX12의 전체 패스 재구축을
표시 슬롯·transient 풀·SSGI 히스토리 교체로 바꿨다. 패스·ShaderMeta·IBL은 유지한다.
Debug/Release 빌드와 각 10회 resize·실제 DPI 왕복이 통과했으며 DPI 축소 시 표시 공백은
기존 8.58초에서 Debug 118ms / Release 63ms로 줄었다.
[EditorW1Dx12ResizeValidation.md](../analysis/EditorW1Dx12ResizeValidation.md)에 증거와 범위를 둔다.
**2026-09-12 후속 사용자 결정:** W1을 `done`으로 판정한다. 남은 W1 구현·필수 검증은 없다.
Vulkan 의 DX12 대응과 표시 오류 처리는 **이 페이즈에서 다루지 않는다**(§0 범위).
다만 관측 기록은 남긴다 — Vulkan 모니터 경계 resize 후 device loss 는 미수정·미재검증이며
해결된 것으로 세지 않는다. RHI 트랙이 그 backend 를 다룰 때
[기존 원인 분석](../analysis/EditorW1DpiResizeRootCauseAndFixPlan.md)의 실제 extent·실패
상태·DPI/resize 검증 항목을 이어가면 된다. 이것은 PHASE 21 의 잔여 작업이 아니다.
CoreCLR 자동 변경 감지 방향에 따라 Live Code 비활성 자리표시자 **버튼을 제거했다**.
자동 변경 감지 기능 자체의 구현은 이번 범위가 아니다. 메시·텍스처 등 Browser 타일은 유형 아이콘을 먼저 표시하고,
비동기 썸네일이 GPU에서 표시 가능해지면 다음 프레임부터 교체한다. 유형 아이콘 정리는 W1,
요청·생성·캐시·완료 게시와 타일 연결은 W7 Browser 추가 범위다.
상세 선정표와 상태·수명 계약은 [EditorIconSelectionStudy.md](../analysis/EditorIconSelectionStudy.md),
제품 적용과 추가 검증은 [EditorMaterialSymbolsW1Validation.md](../analysis/EditorMaterialSymbolsW1Validation.md)에 둔다.

2026-09-12 아이콘/본문 정렬 후속: 폰트 병합에서 실제 가시 영역 중심을 측정해 공통 기준선을 보정했다.
Scene/Game 탭·툴바 및 본문/한글/작은 글씨에 같은 보정을 적용한다.
검증 범위는 [EditorIconAlignmentValidation.md](../analysis/EditorIconAlignmentValidation.md)에 둔다.

- `EditorThemeTokens` / `ApplyEditorTheme`가 §3.1의 14색과 logical geometry를 소유한다.
  `EditorRenderer`와 창별 공통 색·간격 override가 같은 토큰을 소비한다.
- Inter 4.1 static regular와 OFL 라이선스를 Editor resource로 포함·배포한다.
  번들 → 시스템 후보 → ImGui 기본 폰트의 대비 경로를 유지하고 Material Symbols를 병합하며,
  본문·heading은 같은 Inter를 쓴다. 본문 크기와 icon 크기·baseline 보정은 별도 토큰이다.
  기존 한글 폰트는 별도 후보를 유지한다.
  monospace ImGui 소비자는 현재 없으므로 그 체인은 소비자가 생길 때 추가한다.
- 사용자 배율은 `FontScaleMain`, 모니터 배율은 `FontScaleDpi`다.
  매 프레임 OS 창 DPI를 확인하고 **NewFrame 전에** 두 축을 적용한다.
  geometry는 기준 style에서 `ScaleAllSizes(user × DPI)`를 한 번만 수행해 누적 절삭을 막는다.
- `ConfigDpiScaleFonts`를 채택했다. `WM_DPICHANGED`의 제안 RECT는 창 소유 스레드에서
  즉시 적용하고 포인터를 표시 스레드 메시지 큐로 넘기지 않는다.
  `DisplaySize`/framebuffer 좌표는 Win32 backend에 맡기며 OS multi-viewport는 꺼 둔다.
- 기존 EXE에 없던 PerMonitorV2 선언을 `CreatorEditor.manifest`와 `AdditionalManifestFiles`로
  연결했다(§3.2 정정). `editor.theme`와 회귀 게이트는 실행 HWND의 PMv2 상태도 확인한다.
- `editor.theme`는 14색 토큰, 실제 style 연결, geometry, OS/viewport/font DPI와 폰트 출처를 보고한다.
  `editor.selftest`는 지역 style 위에서 고정 hex·mapping·독립/결합 배율·왕복·오염 복구를 검사한다.
- `verify-editor-theme.ps1`은 DX12/Vulkan의 시작 배율 100→150→100%를 검사하며
  설정과 ini를 원본 바이트로 복원한다. 선언·크롬·obsolete 게이트와 함께 run-all에 연결한다.

**판정:** Debug/Release Build와 양쪽 DX12/Vulkan 테마 6기동·103검사씩 통과했다.
지역 style 183검사, Debug 선언 배선 33검사, workspace ini 6종·108검사, obsolete surface도 통과했다.
실행 중 사용자 배율 100→150→100%는 Debug UI에서도 확대·복귀를 확인했다.
활성 ImGui style push는 Editor 전체 51→36건이다(범위·주석 차이는 검증 기록 참조).
후속 실기에서 Windows 배율을 실제로 150→100→150%로 변경했고 모니터 경계도 시험했다.
본문은 사용자 100%에서 24→16→24px, 사용자 150%에서 36→24→36px로 복귀했다.
다만 DX12 표시 복구 지연과 Vulkan 경계 리사이즈의 `VK_ERROR_DEVICE_LOST`를 발견했다.
`editor.theme clean`은 GPU 표시 성공까지 검사하지 않으므로 당시에는 두 오류를 남겨 `progress`로 유지했다.
이후 DX12 수정·실기 재검증으로 현재 판정은 위의 **`done`** 이다(Vulkan 오류는 이 페이즈의 판정 대상이 아니다).
Material Symbols 제품 적용과 Live Code 제거는 이후 W1 후속 작업에서 착지했다.
추가된 전체 의미 아이콘 coverage·저장 ID/레이아웃 검증 결과는 위 Material Symbols 검증 기록을 따른다.
Browser는 유형 아이콘까지 적용됐으며 비동기 썸네일 Ready 교체는 W7 미구현 범위다.
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS`를 제품에 켜는 옛 판정은 §3.2의 ABI 문제로 폐기했고,
`verify-imgui-obsolete-surface.ps1`로 구식 호출 회귀를 검사한다.

### W2 — 공통 styled primitive와 custom draw 4종 (P1, 3일)

**2026-09-13: 네 family 구현·승계와 현재 외관 적용 완료, 상태/입력/성능 전수 gate는 남아 `progress`다.**
승계 결정은 [확정표](../analysis/EditorWidgetInheritanceW2.md)를 따른다. 아래는 적용 이력이다.

**2026-09-12 진행 기록:** 기존 공통 위젯 위에 Inspector 범위 팔레트·간격,
XYZ 배지와 숫자 표시, 가벼운 컴포넌트/내부 그룹 헤더를 적용했다.
대표 DX12 실행 및 편집 검증은 [EditorInspectorStyleValidation.md](../analysis/EditorInspectorStyleValidation.md)에 둔다.
이어 Hierarchy의 회색 외곽/선택 행, 어두운 교차 행, 파란 아이콘·이름과 고정 `+`/Search 도구를 적용했다.
기존 생성 메뉴를 공유하며 DX12에서 생성·검색·검색 중 Delete·drag-drop/Undo·스크롤을 확인했다.
배율 재기동 3회/64개 검사와 자세한 범위는 [EditorHierarchyStyleValidation.md](../analysis/EditorHierarchyStyleValidation.md)에 둔다.
전체 family/state/performance gate 완료로 세지 않으며 `progress`다.

- **§7.1의 승계 결정표를 먼저 확정한다.** 기존 `ImGuiHelper` 자산과의 관계를 정하지 않고
  구현을 시작하면 같은 역할의 위젯이 두 벌 남는다.
- 편집 대상에 비-UTF8 파일이 있으면(§1.10) **인코딩을 먼저 정리한 뒤** 내용을 고친다.
- §7의 네 family를 구현해 Inspector/toolbar의 대표 지점부터 이관한다.
- hover/active/focus/nav/disabled/mixed/error 상태 matrix를 고정한다.
- standard widget을 재작성하지 않는 lint/review 목록을 둔다.
- `ImGuiContext.h`의 죽은 `imgui_impl_dx11.h` include를 걷는다.

**판정:** keyboard navigation과 clipping이 유지되고, visual golden 및 §8 성능 gate를 통과한다.

#### W2-1 착지 — keyboard navigation (2026-09-15)

위 판정문의 앞 절반에 **자가 없었다.** nav 상태를 내보내는 표면이 0 이고 키를 주입할 표면도
0 이라, "키보드 탐색이 유지된다" 는 관찰이 아니라 문장이었다. W8-1(`IMGUI_CHECKVERSION` 이
Release 에서 힘 0), W8-3(검증 레이어를 켜 놓고 아무도 큐를 읽지 않음)에 이은 같은 결함
계통의 세 번째다 — **판정문을 만나면 출하 구성에서 그 수를 읽을 수단이 있는지부터 본다.**

**실제로 틀려 있던 것.** `ImGui::RenderNavCursor`(1.91.4 에서 `RenderNavHighlight` 개명)는
위젯이 **직접** 불러야 한다. `ItemAdd` 는 대신 그려 주지 않고, 표준 `ButtonEx` 도 스스로
부른다. 아이템을 등록하는 자리 넷 중 `EditorPropertyRow` 하나만 그것을 불렀다. 나머지 셋은
`ItemAdd` + `ButtonBehavior` 를 하므로 **키보드로 닿고 Enter 로 눌리기까지 하는데 지금 어디에
서 있는지가 화면에 없었다.** 테마는 `ImGuiCol_NavCursor` 를 `Primary` 로 이미 정해 두었다 —
색은 있고 그리는 곳이 없었다. 꺼진 `EditorModeButton` 은 `ImGuiItemFlags_Disabled` 없이 등록돼
Tab 이 그 자리에 **서 버렸다.**

**세운 것.** `editor::nav`(`Editor/ImGuiHelper/EditorNavContract.{h,cpp}`) — `rhi::validation`
과 같은 꼴의 프로세스 장부다. 위젯이 `announce_item`/`draw_cursor` 로 신고하고, 프레임 끝
(`EditorRenderer::EndRender`)에서 `observe_frame()` 이 `g.NavId` 를 신고분과 맞대 **커서 없이
선 프레임**과 **disabled 인데 선 프레임**을 센다. `deliver_pending_key()` 가 `io.AddKeyEvent` 로
키를 넣되 누름과 뗌을 두 프레임에 나눈다 — 한 프레임에 둘을 넣으면 `IsKeyPressed` 가 보는
전이가 상쇄돼 아무 일도 안 일어난다. 읽고 자극하는 창구는 `editor.nav [reset|key <키>...]`.

**면제 하나를 남겼다.** Tab 으로 `Inputable` 아이템에 들어가면 ImGui 는 `PreferInput` 으로
활성화하고, 그때부터 `TempInputScalar`/`InputTextEx` 가 그리기를 **가져간다** — 커서까지
자기가 그린다. 그래서 `EditorPropertyRow` 의 드래그 자리는 위임(`delegated`)으로 신고하고
판정에서 뺀다. 다만 **수는 남긴다**: `delegatedFrames` 를 세고 `delegatedFrames < frames` 를
단정해 면제가 전 프레임을 덮지 못하게 한다(허용치를 늘리는 대신 판정에서 빼되 수는 남긴다).
이 면제가 하중을 받는 줄이라는 증거는 반대쪽 변이다 — 위임 신고를 일반 신고로 되돌리자
`silentFrames 132 · EditorPropertyRow.drag` 로 처음 겪은 오탐이 그대로 재현됐다.

**게이트:** `Tools/regression/verify-editor-keyboard-nav.ps1`(단정 28 건, run-all 배선 완료).
두 축이다.

- **① 런타임.** Inspector 에 포커스를 주고 Tab 40 회를 주입한 뒤 장부를 읽는다. "부르는 줄이
  있다" 가 아니라 "그 상황에서 실제로 그렸다" 를 재는 유일한 방법이다.
- **② 소스 대조.** 런타임은 nav 가 닿는 자리만 본다. `ItemAdd` 를 부르는 파일을 전수로 뽑아
  허용 목록(§7.1)+면제와 집합 그대로 맞대고, 각 파일이 신고·커서를 부르는지, 장부를 우회한
  `RenderNavCursor` 직접 호출이 0 인지를 본다.

**자극하지 못한 것을 숨기지 않는다.** 이 하네스는 `EditorModeButton`(툴바)과
`EditorSectionHeader` 에 키보드로 닿지 못한다. 게이트는 방문한 위젯 이름과 함께 **닿지 못한
대상의 이름을 찍어** 출력한다 — 그 둘의 계약은 ②만이 지킨다는 사실이 초록 안에 묻히지
않게 한다.

**변이 증명(6종, 양쪽 팔을 따로).** 소스 축 넷은 빌드 없이, 런타임 축 둘은 Release 빌드로.

| 변이 | 축 | 결과 |
|---|---|---|
| `EditorModeButton` 이 꺼진 버튼을 `Disabled` 로 신고하지 않는다 | ① | 잡았다 |
| `EditorSectionHeader` 가 커서를 안 그린다 | ② | 잡았다 |
| 장부를 우회해 `ImGui::RenderNavCursor` 를 직접 부른다 | ② | 잡았다 |
| `ItemAdd` 를 부르는 새 파일이 허용 목록 밖에서 생긴다 | ② | 잡았다 |
| `EditorInspectorPanel` 의 커서 호출을 **죽은 분기로** 만든다(소스엔 남음) | ① | 잡았다 — silent 16 |
| `EditorPropertyRow` 의 위임 신고를 일반 신고로 되돌린다 | ① | 잡았다 — silent 132 |

다섯째는 소스 축을 일부러 통과시켜 런타임 축만 겨눈 것이고, 여섯째는 면제가 옳다를 증명하는
반대쪽 팔이다. Release·Debug 양쪽에서 초록(프레임 318/321 · 신고 1055/1057 · 커서 911/913 ·
위임 144 · 키 40).

**남은 것:** visual baseline, §8 성능 게이트
(hover/active/focus/nav/disabled/mixed/error). W2 는 `progress` 로 둔다.

#### W2-2 착지 — clipping · tooltip (2026-09-15)

같은 문장의 다음 두 절이다. 그리고 **둘은 한 규칙의 양쪽**이다 — 자기 칸보다 넓은 글자를
안 자르면 옆 칸의 버튼·아이콘 위로 그려지고(글자가 대개 나중이라 덮는 쪽이다), 잘랐는데
전체를 tooltip 으로 돌려주지 않으면 이름을 영영 못 읽는다. 앞쪽은 눈에 띄는 고장이고
뒤쪽은 **조용히 사라지는 정보**라 더 오래 남는다.

**규칙은 이미 서 있었고, 한 위젯만 알고 있었다.** `EditorPropertyRow` 가 정본이다 —
`CalcTextSize` 로 재고, 넘치면 `PushClipRect` 로 자르고, 잘린 줄에 `SetTooltip` 으로 전체
이름을 준다. 형제들은 일부만 알았다.

| 위젯 | 자르는가 | 잘린 것을 돌려주는가 | W2-2 가 한 일 |
|---|---|---|---|
| `EditorPropertyRow` | ○ | ○ | 신고만 붙였다 |
| `EditorInspectorPanel` | ○ | **×** | tooltip 을 더했다 |
| `EditorSectionHeader` | **×** | **×** | 자르기와 tooltip 을 더했다 |
| `EditorModeButton` | **×** | (용도 tooltip) | 자르기를 더했다 |
| `EditorAxisField3` | **×** | (한 글자) | 자르기와 신고를 더했다 |
| `SceneViewportOverlay.button` | ○ | ○ | 신고만 붙였다 |

`EditorInspectorPanel` 은 주석에 *"오른쪽 버튼 위로 넘어가면 글자와 아이콘이 겹쳐 둘 다
안 읽힌다"* 고 적어 두고도 잘라 낸 이름을 돌려줄 길은 만들지 않았다.

**세운 것.** `editor::clipping`(`Editor/ImGuiHelper/EditorClipContract.{h,cpp}`) —
`editor::nav`·`rhi::validation` 과 같은 꼴의 장부다. 위젯이 글자를 그리기 직전에
`announce_text(위젯, 글자폭, 칸폭, 잘랐는가, 돌려줄수있는가)` 로 신고하면 장부가 그 자리에서
가른다: 넓은데 안 잘랐다 → `overflow`, 잘랐는데 tooltip 이 없다 → `silent`, 둘 다 했다 →
`truncated`(정상이지만 **수를 남긴다**). 클립 스택 깊이도 함께 신고해 `PushClipRect` 뒤
조기 반환으로 그 뒤 전부가 좁은 사각형에 갇히는 사고를 그 자리에서 잡는다. 창구는
`editor.clipping [reset]`.

**자극이 이 축의 진짜 문제였다.** `EditorPropertyRow` 의 라벨 열은 **라벨에 맞춰 커지므로**
평상시 잘릴 일이 없고, Inspector 도크는 창을 좁혀도 폭이 그대로다 — 실측으로 창을
2200 → 900 으로 줄여도 Inspector 는 둘 다 **634 px** 였다(도크 노드가 절대 폭을 지키고
중앙이 차이를 흡수한다). 그 상태로 재면 위반 0 인데, 그것은 계약을 지켰다는 뜻이 아니라
**한 번도 자극하지 않았다**는 뜻이다. 그래서 게이트가 저장된 배치의 도킹 `SizeRef` 를
고쳐 좁힌 판과 넓힌 판을 **한 저장본에서** 유도한다.

**대조군을 한 번 틀렸다.** 처음엔 "손대지 않은 기본 배치" 를 대조군으로 뒀는데, 기본
배치도 이미 좁아(1600x1000 에서 오른쪽 열 352 px) 182 건이 잘렸다 — 대조가 대조가
아니었다. 같은 출처에서 **방향만 반대로** 유도해야 대조다(좁힘 150 px / 넓힘 900 px).

**게이트:** `Tools/regression/verify-editor-widget-clipping.ps1`(단정 46 건, run-all 배선).
① 런타임은 좁힌 판에서 `truncated > 0`(자극 확인)과 `overflow == 0 · silent == 0 ·
unbalanced == 0`을, 넓힌 판에서 `truncated == 0`을 본다. ② 소스 대조는 글자를 직접 그리는
파일을 전수로 뽑아 계약 6 · 면제 8 과 집합 그대로 맞대고, 계약 대상이 전부 신고하는지와
창과 교차하지 않는 `PushClipRect(..., false)` 가 0 인지를 본다(ImGuizmo 가 정확히 그래서
패널 위로 샜다 — W4, 2026-09-14).

**자극하지 못한 것을 숨기지 않는다.** `EditorModeButton` 은 소비자가 0 이라 **영영** 닿지
못한다. 게이트가 회차마다 그 이름을 찍는다.

**변이 증명(9 종, 세 축).**

| 변이 | 축 | 결과 |
|---|---|---|
| `EditorSectionHeader` 가 신고하지 않는다 | 소스 | 잡았다 |
| `SceneViewportOverlay` 가 신고하지 않는다 | 소스 | 잡았다 |
| 창과 교차하지 않는 클립을 민다 | 소스 | 잡았다 |
| `EditorModeButton` 에 소비자가 생긴다 | 소스 | 잡았다 |
| 글자를 직접 그리는 새 파일이 목록 밖에서 생긴다 | 소스 | 잡았다 |
| `EditorInspectorPanel` 이 잘린 이름을 안 돌려준다 | 런타임 | 잡았다 — silent 91 |
| `EditorPropertyRow` 가 넘쳐도 안 자른다 | 런타임 | 잡았다 — overflow 344 |
| `EditorSectionHeader` 가 안 자른다(신고는 남는다) | 런타임 | **자극하지 못했다**(선언대로) |
| 좁히기를 넓히기로 바꾼다 | 게이트 | 잡았다 — A/B 가 서지 않는다 |

여덟째는 **미리 선언한 대로** 안 잡혔다. `EditorSectionHeader` 의 유일한 소비자가
RectTransform 컴포넌트 하나뿐이라 이 하네스의 씬(빈 엔티티)에는 그려지지 않는다. 그 계약은
소스 대조만이 지킨다 — `EditorModeButton` 과 함께 두 번째 미자극 대상이다.

아홉째를 짜면서 게이트 자신의 결함도 하나 나왔다: 좁힌 폭을 출력에 **리터럴로** 찍고
있어서, 폭을 바꾸는 변이 아래서 게이트가 한 일이 아니라 **적어 둔 의도**를 말했다. 쓴
값을 기록해 찍도록 고쳤다.

Release·Debug 양쪽 초록(좁힘 프레임 91/92 · 신고 2730/2760 · 자름 455/460, 넓힘 자름 0).

**남은 것:** visual baseline, §8 성능 게이트. W2 는 `progress` 로 둔다.

<a id="w2-3-state-matrix"></a>

#### W2-3 착지 — 상태 행렬 (2026-09-15)

계획서의 요구는 한 줄이었다 — *"hover/active/focus/nav/disabled/mixed/error 상태
matrix를 고정한다."* 그런데 **행렬이 없었다.** 있던 것은 위젯 둘의 색 열거뿐이고
(`mode_button_surface`, `section_header_surface`) 그 둘의 항목마저 서로 달랐다. 일곱
상태를 한자리에서 볼 수단이 0 이었다.

W2-1·W2-2 와 같은 결함 계통이다 — **판정문은 있는데 자가 없다.** 그래서 같은 방식으로
장부를 세웠다(`editor::state`): 위젯이 "나는 이 상태들을 구분한다" 를 코드로 **선언**하고
매 프레임 지금 상태를 신고한다. 판정은 선언과 관측을 맞대는 것이다.

**일곱 중 둘은 자가 없는 게 아니라 대상이 없다.** `mixed` 는 씬에 다중 선택이 있는데
Inspector 가 `m_selectedEntity` 하나만 그려 값이 갈리는 상황이 위젯에 오지 않고(W2-I3 의
몫), `error` 는 값 검증이라는 원천이 아예 없다. 둘은 `not_applicable` 로 **선언하고 이유를
남긴다** — 목록에서 지우면 다음 사람이 "일곱 중 다섯만 있네" 를 처음부터 다시 발견한다.

**자극 표면도 0 이었다.** hover·active 는 포인터가 있어야 서는데 CLI 에 마우스를 넣을
길이 없었다 — W2-1 이 키를 만든 자리와 같다. `editor.nav pointer|press|release` 를 더했다.
★ 이벤트 큐로는 서지 않았다. 큐에 넣으면 다음 프레임의 `ImGui_ImplWin32_NewFrame` 이 자기
좌표를 **뒤에** 넣어 덮는다(실측: 주입이 한 번도 서지 않았다). 고정 상태로 들고 있다가
`NewFrame` **뒤**에 매 프레임 얹는다.

**자리를 한 번만 읽으면 모자란다.** 어디를 찌를지는 위젯이 그려진 자리가 정하므로 장부에서
읽는데, 그 자리를 찌르자 패널이 펼쳐져 **1 회차에 없던 위젯**(`EditorPropertyRow.drag`)이
드러났고 Inspector 자체도 121 px 올라갔다. 한 바퀴만 도는 게이트는 "열어야 보이는 것" 을
영영 못 찌르고 그것을 미자극으로 보고한다 — 사실은 자극할 기회를 스스로 없앤 것이다. 그래서
세 회차를 돈다(읽고 · 찌르고 다시 읽고 · 같은 접두사로 배치를 재현한 뒤 새 자리까지 찌른다).

행렬을 세우면서 제품 쪽 결함이 셋 나왔다.

| 무엇 | 왜 틀렸나 | 고침 |
| --- | --- | --- |
| `disabled` 계측이 `NoInput` 플래그만 읽었다 | 인스펙터가 실제로 쓰는 기제는 `ImGui::BeginDisabled` 다. 한 팔만 읽으면 그 기제를 통째로 못 본다 | 둘의 **합집합**으로 |
| 값 줄이 Tab 에 닿는 순간 행렬에서 사라졌다 | `Inputable` 아이템은 그리기를 표준 위젯에 넘기고 신고 앞에서 빠져나간다 — **자극이 도착하는 바로 그 경로가 장부의 눈을 가렸다** | 위임 경로에도 신고 |
| `focus` 와 `nav` 가 같은 뜻이 될 뻔했다 | 같은 식으로 적으면 이름만 둘이다 | `NavId` 와 `NavId + NavCursorVisible` 로 갈랐다 |

판정은 **미자극 0** 이다. 처음엔 미자극을 이름으로 찍기만 했더니 계측을 망가뜨리는 변이가
전부 초록으로 지나갔다 — 관측이 사라져도 "자극하지 못한 것" 으로 읽혔기 때문이다. 지금
그려지는 세 위젯은 선언한 다섯 상태를 **전부** 낸다(실측). 그 값을 판정으로 세우고, 하네스가
닿지 못하는 자리가 생기면 사유와 함께 면제 표에 적게 했다 — 지금 그 표는 비어 있다.

그려지지 않는 위젯 둘은 **이유가 서로 다르다.** `EditorModeButton` 은 `draw_mode_button` 의
소비자가 0 이라 그릴 코드가 없고, `EditorSectionHeader` 는 그릴 **대상**을 만들 표면이 없다 —
소비자가 RectTransform 그리개 하나뿐인데 그 컴포넌트는 엔티티 타입이 UI/Canvas 일 때
생성자가 붙이는 것이라 `component.add` 로 못 붙이고(등록 표 26 종에 없다), `object.create` 의
타입은 Empty/Light/Camera/Mesh 뿐이며 UI 계층을 세우는 `ui.navprobe` 는 커맨드릿 전용이라
라이브 CLI 에서 부를 수 없다. 둘을 같은 "안 나타남" 으로 뭉뚱그리면 다음 사람이 둘 다 자극
부족으로 읽는다.

변이 다섯으로 이빨을 쟀다.

| 변이 | 대상 | 결과 |
| --- | --- | --- |
| `declare` 호출 하나를 지운다 | 소스 | 잡았다 |
| 창별 Tab 순회를 하나로 줄인다 | 게이트 | 잡았다 — `button.nav` 미자극 |
| `disabled` 를 `NoInput` 만 읽게 되돌린다 | 제품 | 잡았다 — `drag.disabled` 미자극 |
| 포인터 주입을 꺼 버린다 | 제품 | 잡았다 — `active` 셋이 사라진다 |
| 주입을 `NewFrame` **앞**으로 옮긴다 | 제품 | **자극하지 못했다** |

셋째가 중요하다. 그 변이 아래서 *"disabled 를 낸 위젯이 하나 이상"* 단정은 **통과했다** —
다른 위젯이 채웠기 때문이다. 미자극 0 을 판정으로 세우지 않았다면 이 변이는 초록으로
지나갔다. 축마다 하나씩 세는 단정은 계측 하나가 죽은 것을 보지 못한다.

다섯째는 못 잡았고 그 이유를 안다 — 게이트가 창을 숨겨 띄우므로 `ImGui_ImplWin32_NewFrame`
이 마우스 좌표를 갱신하지 않아 앞뒤가 등가가 된다. 사람이 쓰는 조건에서는 등가가 아니니
**그 호출을 앞으로 옮겨도 된다고 읽어서는 안 된다.** 게이트 머리에 그렇게 적었다.

게이트가 내 판단을 한 번 뒤집었다. `EditorInspectorPanel` 의 `active` 를 소스만 읽고 "올 수
없다"(`PressedOnClick` 이니 눌린 상태가 없다)고 적었는데, 변이 회차가 실제 관측을 내밀었다.
공식 표를 보니 `PressedOnClick` 은 누르고 있는 동안 `IsItemActive()` 가 참이었고, 못 섰던
진짜 이유는 **머리줄 한가운데가 켜짐 토글이라 `overToggle` 단락 평가로 `ButtonBehavior` 가
아예 돌지 않은 것**이었다. 자리 안의 여러 점을 찌르도록 고치자 관측이 섰다. 소스만 읽고
"없다" 를 적으면 행렬에 거짓 빈칸이 남는다.

Release 초록(위젯 4 · 관측 14 · 프레임 1253 · 신고 26775 · 미자극 0 · 단정 54 건).

**남은 것:** 없다. visual baseline 과 §8 성능 게이트는 W2-4 로 닫았다. W2 는 `done` 이다.

#### W2-4 착지 — 시각 기준선과 §8 성능 계약 (2026-09-15)

남은 둘을 한 조각으로 닫았다. 둘은 성질이 달랐다 — 하나는 **이미 있었고** 하나는
**자가 없었다.**

**① 시각 기준선 — 새로 세우지 않았다. 이미 W8-2 가 쥐고 있었다.**

W2 절의 "시각 기준선" 을 새 골든으로 뜨려다, 그 전에 *"지금 있는 것이 무엇을 덮는가"*
를 먼저 쟀다. W8-2 의 chrome 골든(`verify-editor-chrome-golden.ps1`)은 창 배치가 아니라
**화소**를 대조한다. 그러면 W2 위젯의 그림도 그 화소 안에 있다. 물어볼 것은 하나였다 —
*W2 위젯의 그림을 바꾸면 그 게이트가 붉어지는가.*

변이로 물었다. `EditorPropertyRow` 에서 `RenderFrameBorder` 한 줄을 걷었더니
`verify-editor-chrome-golden` 이 **단정 3 건으로 붉었다**(대조 회차 다른 화소 305,557 개).
덮고 있다. 그러니 같은 자를 두 벌 두지 않는다 — W2 의 시각 기준선은 **W8-2 가 정본**이고,
이 절은 그 사실과 변이 증거를 적는 것으로 닫는다.

> 판정문이 요구한 것이 "새 게이트" 가 아니라 "재는 자" 일 때는, 세우기 전에 **이미 재고
> 있는 자가 있는지**를 변이로 먼저 물어라. 없으면 세우고, 있으면 적어라. 두 벌은 한쪽만
> 고쳐진다([기본 preset 은 재정의 0 이어야 한다] 와 같은 계통이다).

**② §8 성능 계약 — 판정문에 자가 없었다.**

§8.2 는 *"p95 CPU 가 기준선 대비 악화되면 **원인을 기록하고** 최적화 또는 rollback 한다"*
고 적었다. 그런데 원인을 기록할 수단이 0 이었다. 실측이 그것을 못 박았다 — chrome 한
프레임이 **1.1 ms 인데 이름이 붙은 것은 0.015 ms(1.4%)** 였다. W7 이 세운 슬롯 셋
(`hierarchy` · `browser_tree` · `browser_files`)은 자기 축을 재려고 만든 것이라 나머지
98.6% 는 통째로 익명이었다. 악화를 감지해도 "어디인지 모른다" 를 되풀이할 판이었다.

두 층을 새로 열었다(`editor::windows`, `EditorPanelCost.{h,cpp}`).

| 층 | 무엇 | 거는 자리 |
|---|---|---|
| 창 | 에디터 창 하나를 그리는 전체 | `EditorWindowHost` 의 순회 **한 곳** — 창마다 흩지 않는다. 키는 선언 표의 색인이라 매 프레임 문자열 비교가 없다 |
| 셸 | 창 **밖**의 구간 일곱 | `EditorRenderer` 의 `BeginRender`/`Render`/`EndRender` |

★ 슬롯 표와 **층위가 다르다.** 슬롯은 창 하나 *안*의 구간이고(`browser_tree` 와
`browser_files` 는 같은 창이다) 창 표는 창 하나를 그리는 전체다. 한 표에 더하면 같은
시간이 두 번 잡힌다 — CLI 가 두 표를 따로 낸다.

설계가 실측에 두 번 고쳐졌다.

- **잔차가 음수로 나왔다(-0.320 ms).** 프레임 총계를 `EndFrame` **뒤**에서 닫고 있었는데
  그 안에 `RenderAndPresent` 가 들어 있었다. 총계는 W0 이 기준선을 뜬 자(`ui_cpu_ms`)와
  같은 구간이어야 하므로 **제출 앞에서 끊고**, `(present)` 를 총계 **밖** 줄로 따로 냈다.
  넣었다면 "UI CPU" 가 GPU 제출을 포함하는 이름이 됐다.
- **`host.beginframe` 을 `imgui.newframe` 으로 부를 뻔했다.** 그 안에는
  `m_renderer->Resize` 와 `m_renderer->NewFrame()` — **RHI 프레임 자원 획득**(펜스 대기가
  될 수 있다)이 함께 있다. 이름을 `imgui.*` 로 두면 다음 사람이 순수 UI CPU 로 읽는다.
  주석과 이름에 별표로 못 박았다.

그림이 뒤집혔다. 계측 전의 짐작은 "창 본문이 비싸다" 였는데, 창 다섯을 전부 계측해도
설명된 것이 0.088 ms 뿐이었고 **`(host.beginframe)` 혼자 프레임의 77%** 였다. W2 의 사설
위젯 넷이 쓰는 몫은 +0.044 ms — **프레임의 2.1%** 다.

게이트: `Tools/regression/verify-editor-chrome-perf.ps1`(단정 59 · 건너뜀 0 · run-all 배선).
Release 전용이고(Debug 는 방향까지 뒤집는다) 두 회차(기본 / 엔티티 선택)로 돈다. 판정 일곱:

1. **계측 자리 집합** — 열거된 구간이 하나도 빠짐없이 관측됐는가. 이것이 주 판정이다
   (구간 하나를 지우면 잡는다).
2. `(present)` 가 총계 **밖**이고 `설명 + 미설명 = 총계` 가 성립하는가.
3. 잔차가 음수가 아니고 총계의 50% 이하인가.
4. W2 위젯의 델타 — 선택 회차의 인스펙터가 기본의 **2 배 이상**이고 정점이 **+500 이상**
   인가. 처음엔 `> 0` 으로 뒀는데 선택 없이도 통과해 변이 하나를 놓쳤다.
5. 가장 비싼 구간에 **이름이 있는가**(익명 잔차가 1 위가 되면 붉다).
6. W0 기준선(2026-09-11)과 나란히 출력하는가.
7. 정점·인덱스·draw command 가 골든(`editor-chrome-perf.golden.json`)과 한 글자도 다르지
   않은가. 환경(배율·창 크기)이 다르면 건너뛴다.

③ 잔차 임계는 **50%** 로 느슨하다. 총계가 절반이 되면 같은 절대 잔차가 19% → 26% 로
뛰기 때문이다 — 비율은 총계에 따라 흔들린다. 그래서 잔차는 보조이고 ①이 주 판정이다.

Release 초록(설명력 base 95.6% · selected 96.5% · 단정 59 건 · 건너뜀 0).

#### Content Browser 를 기본 앞 탭으로 (2026-09-15 · 사용자 요청) — 대가를 함께 적는다

기본 배치에서 아래 패널의 앞 탭이 `AssetBundle` 이었다. 사용자가 Content Browser 가 앞에
서기를 요청해 바꿨다.

**막힌 곳이 계획과 달랐다.** `DockBuilderDockWindow` 는 `window->DockId` 만 적을 뿐이고,
빌드 단계에서 `node->SelectedTabId` 를 써도, ContentBrowser 를 먼저 도킹해도 소용이
없었다(둘 다 실측). **탭 선택은 창이 실제로 `Begin` 되는 순서가 정한다.** 그래서 빌더는
플래그(`m_selectContentBrowserOnBuild`)만 세우고, `Render()` 끝에서 창이 서 있는 것을
확인한 뒤 `SelectedTabId`/`NextSelectedTabId` 를 쓰고 플래그를 내린다. 빌더는
`workspaceReset` 일 때만 도므로 **저장된 사용자 배치는 건드리지 않는다.**

**대가가 크다.** 방금 세운 W2-4 의 자가 그 자리에서 값을 냈다.

| 축 | 바꾸기 전 | 바꾼 뒤 |
|---|---|---|
| chrome 프레임(UI CPU 중앙) | 0.380 ms(W0) → 0.82 ms | **3.650 ms** (W0 대비 9.6 배) |
| 가장 비싼 구간 | `(host.beginframe)` 77% | **`###Editor.ContentBrowser` 2.446 ms · 프레임의 82.6%** |
| draw command | 12(W0) | 126 (10.5 배) |

`AssetBundle` 은 0.002 ms 로 뒤로 물러났다. 즉 비용은 탭 전환이 만든 것이 아니라 **Content
Browser 가 원래 비쌌고 앞에 서지 않아 안 보였을 뿐**이다. 원인은 이미 알려져 있다 —
**§W7-5**(트리 노드마다 도는 파일시스템 호출, `equivalent` 가 캐시를 경유하지 않는다).
**2026-09-16 — W7-5 를 닫았고 수치가 따라 내려갔다.** `###Editor.ContentBrowser`
2.446 → **0.250 ms**, chrome 프레임 총계 1.892 → **0.636 ms**. Content Browser 를 앞
탭에서 되돌릴 이유가 없어졌다 — 그대로 둔다. §W7-5 착지 참조.

<a id="w2-inspector-layout"></a>

### W2-I — 인스펙터 공통 속성 배치 규칙 · Transform 컴포넌트 렌더 통합 (P1, 6일 · 초기 추정)

**2026-09-13 현재 외관 승인·고정. W2-I0 `done`(0.5일), I1~I5 잔여로 전체는 `progress`다.**
**잔여의 실제 크기는 2026-09-16 재기준선(아래 표 다음 문단)을 정본으로 한다.**
W2의 공통 위젯을 모든 인스펙터 경로가 같은 배치 규칙으로 소비하도록 확장한다.
W2 기존 3일에 흡수하지 않으며, 아래 여섯 단계의 초기 추정 합계가 6일이다.
초기 설계 근거와 이번 구현/검증을 구분한다. 기존 배치 기반 위에 적용한 스타일,
Transform Undo 수정, 대표 폭 DX12 실행 기록은
[EditorInspectorStyleValidation.md](../analysis/EditorInspectorStyleValidation.md)를 따른다.
아래 전체 타입·폭·배율·백엔드 매트릭스가 완료됐다는 의미는 아니다.

#### 착수 전 문제와 이관 경계 (현재 상태는 §9.0 참조)

- `InspectorWindow.cpp`의 Transform은 `Text`·공백·`SameLine`으로 라벨을 배치하지만,
  `ReflectionTypedDraw.h`의 일반 필드는 입력칸 오른쪽에 라벨을 붙인다.
- `ImGuiDrawHelperRectTransformComponent.cpp`는 X·Y를 각각 90px로 고정하고 라벨 열을 늘린다.
  좁을 때 앵커 프리셋과 값 표가 한 줄에 남고, 넓을 때 여유 폭이 라벨 열에 몰린다.
- `EditorAxisField3`는 XYZ를 항상 한 줄에 그리며 숫자 입력칸 폭을 1px까지 줄인다.
  `EditorPropertyRow`는 현재 float 배열을 호출자가 만든 표에 그리는 기능이라 전체 배치 정책은 없다.
- 기본 정보·Sound 등 전용 드로어의 고정 폭과 자산의 `DrawYamlNodeEditor`도 대상이다.
  일반 리플렉션만 바꾸고 전용 드로어·Import Settings를 남겨 두면 전체 이관 완료로 세지 않는다.

#### 공통 배치 계약

1. **배치와 값 편집을 분리한다.** 공통 계층이 가용 content rect, 라벨 열, 값 영역,
   보조 버튼 공간과 행 전환을 계산하고 기존 표준 ImGui 위젯이 값을 편집한다.
   `EditorPropertyRow`의 배치 책임을 확장하되 새 입력 체계나 매크로 기반 선언을 만들지 않는다.
2. **같은 깊이의 속성은 같은 열에 맞춘다.** 라벨은 왼쪽, 값과 체크박스는 공통 값 열,
   선택·초기화 같은 보조 버튼은 예약한 공간에 둔다. 중첩 그룹은 명시적인 들여쓰기만 더한다.
   라벨 폭에는 상한을 두고 나머지는 값 영역에 배분한다. 공백 문자열과 창 전체 폭 기반 좌표 보정은 제거한다.
3. **최소 가독성 이하로 줄이지 않는다.** 실제 content 폭에서 라벨·간격·값·버튼의 최소 폭을
   확보하지 못하면 라벨 위/값 아래로 전환한다. XYZ도 축 badge와 숫자 표시 형식에 필요한 폭을
   확보할 수 없으면 축별 세로 배치로 전환한다. 기준은 폰트 측정·W1 geometry 배율을 사용하며
   현재 숫자 값이나 매 프레임 라벨 최대값 변화로 열과 모드가 흔들리지 않게 한다.
4. **넓은 폭은 값과 관련 묶음이 사용한다.** 우선 값 영역을 확장하고, 각 묶음의 최소 폭이
   충족되면 Sound의 Bus/Params·Spatial 같은 허용된 하위 묶음만 두 열로 배치한다.
   컴포넌트 순서와 묶음 내부 필드 순서는 유지한다. 모든 컴포넌트를 자동으로 두 열에 재배열하지 않는다.
5. **정보 종류에 맞게 넘침을 처리한다.** 긴 라벨·설명·읽기 전용 경로는 줄바꿈한다.
   편집 문자열은 표준 입력칸의 탐색·스크롤을 유지하고 보조 버튼이 밖으로 밀리지 않게 한다.
   세로 부족은 스크롤로 처리하고, 높이가 늘어도 행 간격을 늘려 빈 공간을 채우지 않는다.
6. **배치 전환은 편집 상태를 보존한다.** ID는 엔티티·컴포넌트 인스턴스·필드·축의 안정 신원을
   사용하며 열 위치·표시 라벨·줄 전환을 ID로 사용하지 않는다. 드래그 중 모드 전환은 편집 종료까지
   보류하고 경계 왕복에 완충 폭을 둔다. Tab 이동, 포커스, 팝업, drag-drop, Undo 단위를 보존한다.

#### Transform 계통 결정 — 2안 채택

**별도 기본 렌더 호출을 없애고 실제 컴포넌트 렌더 경로로 통합한다. Transform·RectTransform은
상단 우선 배치하며 개별 비활성화와 개별 제거를 허용하지 않는다.**
1안(기본 렌더 + 일반 순회 제외)은 최종 구조로 채택하지 않고, 3안(개별 비활성화 허용)도 채택하지 않는다.

- **인스턴스당 한 번:** 현재 기본 렌더 뒤의 컴포넌트 순회는 RectTransform만 제외해 Transform이
  다시 일반 리플렉션으로 내려갈 수 있다. 공통 처리가 헤더와 정책을 소유하고 전용 드로어는 본문만 그려,
  표시 대상 컴포넌트 인스턴스마다 헤더는 1회, 펼친 본문도 1회만 호출되게 한다.
- **전용 편집 의미 보존:** Transform의 각도 편집·변경 게시·Undo와 RectTransform의 앵커 프리셋·
  레이아웃 변경 처리를 유지한다. 일반 리플렉션의 quaternion/내부 필드 편집으로 대체하지 않는다.
- **실제 보유 구성이 정본:** 일반 엔티티는 Transform, UI는 RectTransform, Canvas는 둘 다 가진다.
  Canvas에서는 UI 배치와 월드 공간 배치가 서로 다른 정보이므로 각각 한 번 표시한다.
  `RectTransform이 있으면 Transform 숨김`을 공통 규칙으로 만들지 않는다.
- **정책을 분리:** 표시 순서, 개별 활성화 허용, 개별 제거 허용을 별도 항목으로 둔다.
  공간 컴포넌트는 활성 체크박스와 제거 동작을 제공하지 않는다. `EditorSectionHeader`의
  `enabled == nullptr` 계약과 `EditorObjectOperations::RemoveComponent`의 기존 거부를 활용한다.
- **엔티티 전체 활성 전이 보존:** 비활성화 불가는 컴포넌트 단독 조작에 관한 결정이다.
  현재 `Entity::SetEnabled`는 모든 컴포넌트와 자식에게 상태를 전달하므로 공용
  `SetEnabled(false)`를 무조건 거부하는 구현은 금지한다. 개별 조작과 엔티티 전이를 구분해
  UI·개별 활성 변경 진입점에 같은 정책을 적용한다. 기존 저장값은 강제로 true로 덮어쓰지 않는다.
  RectTransform의 비활성 UI 레이아웃 갱신과 공간 계산 규칙도 유지한다.
- **논리 엔티티는 별도 범위:** 현재 Empty에도 Transform이 붙는다. 게임 매니저처럼 위치가 불필요한
  엔티티에서 공간 컴포넌트를 아예 생략하는 생성·저장·조회 계약은 후속 아키텍처 검토로 남긴다.
  이 항목을 Transform 비활성화 허용으로 대신하지 않는다.

#### 이관 순서와 초기 공수

| 단계 | 작업 | 초기 추정 | 완료 기준 | 상태(2026-09-16 재기준선) |
|---|---|---:|---|---|
| W2-I0 | content 폭·공통 열·줄 전환과 표시 정책 정의 | 0.5일 | 폭 계산·정책이 한 정본을 소비하고 전환 조건이 고정됨 | done — 공통 계약·측정 함수 및 소비 경로 확인 |
| W2-I1 | Transform 2안·공통 헤더/전용 본문 분리 | 1일 | 일반 엔티티·UI·Canvas에서 중복 0, 개별 조작 제한과 엔티티 활성 전이 보존 | done — **2026-09-18 공통 순회 하나로 합치고 정책을 한 표로 세웠다**. 신원·순서·체크박스·개별 변경 거부·엔티티 전이를 `verify-inspector-spatial-policy.ps1` 이 잰다(단정 85 · 엔티티 5). 착수 때 `object.property … Transform m_isEnabled false` 가 성공했다(아래 절) |
| W2-I2 | 기본 정보·Transform·RectTransform 반응형 배치 | 1일 | 공백 정렬·고정 숫자 열 폭 제거, 축별 최소 가독성·기존 편집 의미 보존 | done — 기본/XYZ 적용·사용자 확인. **2026-09-17 RectTransform 도 공통 줄로 옮겼다** — 240 넘침 122 → 0 px, 공통 줄 0 → 7(아래 절) |
| W2-I3 | 일반 리플렉션·중첩 필드·공유 드로어 이관 | 1일 | 수치·문자열·bool·enum·벡터가 같은 규칙을 사용하고 Inspector 밖 소비자도 회귀 없음 | progress — **일반 경로와 C# 노출 필드는 정본을 쓴다**(`ReflectionTypedDraw.h`·`EditorAxisField3`·`DrawManagedScripts` 5 건). **2026-09-17 중첩/배열도 닫았다** — 합성 자극물 줄 31 → 55(타입 정의에서 센 값), 240 넘침 323 → 0 px. 원인 하나는 공통 줄 정본의 항목 폭이었다(아래 절) |
| W2-I4 | 전용 컴포넌트·자산 Import Settings 이관 | 1.5일 | Sound 등 고정 폭 제거, 긴 참조·보조 버튼·허용된 두 열 묶음·저장 동작 확인 | progress — **잔여가 셈이 된다: 전용 드로어 15 중 12.** 아래 재기준선의 표가 이름을 전부 적는다. Import Settings 의 대상 파일은 인스펙터가 아니라 `DrawYamlNodeEditor.cpp` 다. **2026-09-17 자를 열었다** — `editor.inspector width` 와 `verify-inspector-drawer-layout.ps1`(이관 목록에만 단정, 착수 기준선은 아래). **같은 날 전용 드로어 열둘 이관 — 14/14, 네 폭 넘침 0, 소스 축 고정 폭 0.** 이어서 Import Settings(`DrawYamlNodeEditor.cpp`)도 옮겼다 — 15/15, 펼친 채 네 폭 넘침 0(`editor.inspector expand on`). **W2-I4 의 대상 표면 0** |
| W2-I5 | 폭·배율·편집·중복 렌더 회귀와 잔재 점검 | 1일 | 아래 검증 행렬 및 §8 성능 gate 충족, 미이관 표면 0 | progress — 대표 UI/편집 검증, 전체 행렬·호출 수/성능 남음 |

전용 드로어 전수 이관량과 개별 활성 변경 진입점의 실제 범위는 I0에서 재계수한다.
초기 추정이 달라지면 본문과 대시보드의 공수를 함께 갱신하며, 추가 범위를 완료 실적으로 계산하지 않는다.

##### 2026-09-16 재기준선 — 잔여를 문장에서 셈으로 바꾼다

착수 전에 상태를 읽었더니 2026-09-13 기준이었고, 잔여가 *"전용 드로어 전수 이관"*
처럼 크기를 알 수 없는 문장으로 적혀 있었다. 낡은 상태로 산정하면 이미 한 일을
다시 잡는다(같은 이유로 §W2-B 를 같은 날 재기준선했다). 실물을 세어 아래로 바꾼다.

★ **먼저 자를 고쳐야 했다.** 처음에 `EditorPropertyRow` 로 세어 "전용 드로어 다섯 중
넷이 미이관" 이라는 수를 얻었는데 **그 자가 틀렸다.** `EditorPropertyRow` 는 파일
이름이고 코드가 부르는 어휘는 `editor::widgets::` 다 — 그래서 include 와 주석만
잡히고 실제 소비는 한 건도 안 잡혔다. 게다가 전용 드로어의 **열이 별도 파일이 아니라
`InspectorWindow.cpp` 안의 멤버 함수**여서 파일 단위 세기가 통째로 놓쳤다. 어휘 아홉
(`begin_property_line`·`draw_property_row`·`measure_property_layout`·
`begin_inspector_panel`·`drag_property_float(s)`·`property_group_header`·
`draw_axis_field3`·`property_layout_inputs_now`)으로 함수 범위마다 다시 셌다.

**전용 드로어는 열다섯이고 정본을 쓰는 것은 셋이다.**

| 드로어 | 자리 | 어휘 |
|---|---|---:|
| `ImGuiDrawHelperTransformComponent` | `InspectorWindow.cpp:872` | 6 |
| `ImGuiDrawHelperGameObjectBaseInfo` | `InspectorWindow.cpp:667` | 4 |
| `ImGuiDrawHelperRectTransformComponent` | 별도 파일 | 3 |
| `ImGuiDrawHelperFSM` | `InspectorWindow.cpp:1034` | 0 |
| `ImGuiDrawHelperBT` | `InspectorWindow.cpp:1056` | 0 |
| `ImGuiDrawHelperVolume` | `InspectorWindow.cpp:1121` | 0 |
| `ImGuiDrawHelperDecal` | `InspectorWindow.cpp:1322` | 0 |
| `ImGuiDrawHelperImageComponent` | `InspectorWindow.cpp:1409` | 0 |
| `ImGuiDrawHelperSpriteRenderer` | `InspectorWindow.cpp:1561` | 0 |
| `ImGuiDrawHelperCanvas` | `InspectorWindow.cpp:1589` | 0 |
| `ImGuiDrawHelperSoundComponent` | `InspectorWindow.cpp:1602` | 0 |
| `ImGuiDrawHelperAnimator` | 별도 파일 | 0 |
| `ImGuiDrawHelperMeshRenderer` | 별도 파일 | 0 |
| `ImGuiDrawHelperPlayerInput` | 별도 파일 | 0 |
| `ImGuiDrawHelperTerrainComponent` | 별도 파일 | 0 |

즉 착지한 셋은 전부 **W2-I1·W2-I2 가 다룬 공간/기본 정보**이고, W2-I4 의 1.5 일이
상대하는 것은 **미이관 열둘 + Import Settings** 다. 이전 문장의 *"Sound 등"* 은
맞았다 — `SoundComponent` 가 그 열둘 중 하나다.

· **일반 경로는 이미 정본을 지난다** — `ReflectionTypedDraw.h` · `EditorAxisField3` ·
  그리고 C# 노출 필드를 그리는 `DrawManagedScripts`(5 건). W2-I3 의 남은 몫은
  *경로 이관* 이 아니라 중첩/배열이라는 **모양** 하나다.
· ★ **Import Settings 는 인스펙터에 없다.** `InspectorWindow.cpp:2385` 가 머리줄만
  붙이고 본문은 `DrawYamlNodeEditor(selectedNode->Root())` 로 넘긴다
  (`DrawYamlNodeEditor.cpp` 143 줄, 어휘 0 건). 인스펙터 파일을 훑어 이관을 끝냈다고
  읽으면 이 표면이 그대로 남는다 — 대상 파일이 다른 트리에 있다.

잔여 산정은 바꾸지 않는다. 바뀐 것은 **무엇을 열어야 하는지가 이름으로 정해졌다**는
것과, 착수하면 셈으로 진척을 잴 수 있다는 것이다 — 이관이 끝나면 위 표의 0 이 0 개다.

##### 2026-09-17 착수 — 자를 먼저 연다

같은 날 어휘 셈을 다시 했고 표와 같았다(열둘이 0). 그런데 소스 셈으로는 판정문의
절반인 *"가로 잘림·겹침"* 을 읽을 수 없고, 인스펙터 폭을 240/320/480/720 으로 정할
수단도 없었다(도크는 절대 폭을 지키고 `SizeRef` 고치기는 값을 못 맞춘다). 그래서
`editor.inspector [width <논리 px>|off]` 를 열었다. 폭을 주면 본문을 그 폭의 영역
안에서 그리고, 본문마다 **지난 공통 배치 줄 수**(`begin_property_line` 누계의 차)와
**오른쪽 넘침 px**(`CursorMaxPos` 를 본문 머리에서 내렸다가 읽는다)를 낸다.
검사 `verify-inspector-drawer-layout.ps1` 은 이관을 끝낸 드로어 목록에만 단정을 건다
(네 폭 모두 줄 > 0 · 넘침 0). 목록 밖은 판정하지 않고 수만 낸다. 자 자신도 단정한다 —
받은 폭 = 요청 × 배율, 240 에서 넘치는 본문이 실제로 있다(없으면 자극이 안 된 것).
변이 셋(폭 무시 · 줄 계수 제거 · 아래 텍스처 폴더 판정 복원)이 모두 붉다.

착수 기준선(Release, 실제 배율 2.25 — 사용자 1.5 × 모니터 150%, 넘침은 px):

| 드로어 | 공통 배치 줄 | 240 | 320 | 480·720 |
|---|---:|---:|---:|---:|
| 기본 정보 · Transform (이관) | 1 · 3 | 0 | 0 | 0 |
| ImageComponent | 0 | 260 | 80 | 0 |
| Canvas | 0 | 61.8 | 0 | 0 |
| DecalComponent | 0 | 54.3 | 0 | 0 |
| PlayerInputComponent | 0 | 47.3 | 0 | 0 |
| BehaviorTreeComponent | 0 | 46.5 | 0 | 0 |
| SoundComponent | 0 | 23.8 | 0 | 0 |
| TerrainComponent | 0 | 15.8 | 0 | 0 |
| MeshRenderer | 5 | 15 | 0 | 0 |
| SpriteRenderer · Animator | 5 · 2 | 0 | 0 | 0 |
| VolumeComponent · StateMachineComponent | 0 | 0 | 0 | 0 |

★ 줄 > 0 · 넘침 0 이 곧 이관 완료는 아니다. SpriteRenderer·Animator 는 리플렉션
경로(`DrawOwnMembers`)로 일부 줄이 공통 배치를 지나지만 `ImVec2(150, 20)` 같은
**배율을 받지 않는 고정 크기**가 남아 있다 — 배율 2.25 에서는 넘치지 않을 뿐이다.
옮길 때 소스 축(고정 크기 리터럴 0)을 같이 건다.

★ 자를 여는 중에 **DecalComponent 를 붙이는 순간 에디터가 죽었다.** 빈 텍스처 이름이
`Assets/Textures/` **폴더**가 되고, `Texture::LoadSharedFromPath` 계열 셋이 `exists` 만
보아 폴더를 통과시켜 WIC 가 폴더를 열다 예외를 던졌다. 셋을 `is_regular_file` 로 고쳤다.

##### 2026-09-17 전용 드로어 열둘 이관 — 14/14

`editor::widgets::property_sheet` 를 더했다. 구간이 그릴 **고정 라벨 목록**으로 라벨 열을
한 번 재고(`label_hint`), 줄마다 `line(label)` 이 라벨을 놓고 값 칸 폭을 돌려준다.
배치 판정은 공통 계층의 것 그대로이고 새 규칙은 없다. 값 칸 뒤에 정사각 버튼을 붙이는
줄은 `line_before_buttons` 가 버튼과 간격을 뺀 폭을 준다. 열두 드로어의 라벨 붙은 위젯을
전부 `"##"` 위젯 + 공통 줄로 바꾸고, 고정 폭(`SetNextItemWidth(150)` · `ImVec2(150, 20)` ·
`ImVec2(0, 200)` 등)을 줄이 준 폭 또는 `ThemePixels` 로 바꿨다.

| 드로어 | 공통 배치 줄 | 240 | 320 | 480·720 |
|---|---:|---:|---:|---:|
| 기본 정보 · Transform | 1 · 3 | 0 | 0 | 0 |
| ImageComponent | 13 | 0 (착수 260) | 0 (80) | 0 |
| SoundComponent | 12 | 0 (23.8) | 0 | 0 |
| MeshRenderer | 10 | 0 (15) | 0 | 0 |
| DecalComponent | 7 | 0 (54.3) | 0 | 0 |
| SpriteRenderer | 6 | 0 | 0 | 0 |
| TerrainComponent | 5 | 0 (15.8) | 0 | 0 |
| Animator | 3 | 0 | 0 | 0 |
| Canvas · BT · PlayerInput | 2 · 2 · 2 | 0 (61.8 · 46.5 · 47.3) | 0 | 0 |
| Volume · StateMachine | 1 · 1 | 0 | 0 | 0 |

검사 `verify-inspector-drawer-layout.ps1` 은 이제 열넷 전부에 단정을 건다(단정 213). 런타임
축(네 폭 모두 줄 > 0 · 넘침 0)에 **소스 축**을 더했다 — 드로어 함수와 그 조각 함수
(`DrawAssetSlot` · `DrawNamedPicker` · `NameButton` · `DrawMaterialTextureSlot` ·
`ReadOnlyLine` · `DrawBrushMasks` · `ButtonRow`) 본문에 숫자 리터럴 폭이 남으면 실패다.
런타임 축은 배율 2.25 기계에서 "넘치지 않았다" 만 보므로 배율 1 에서 넘칠 고정 폭을 못
잡는다. 240 폭 화면을 열둘 모두 찍어 눈으로 확인했다(라벨 아래로 값이 내려가고 잘림·겹침 없음).

변이 넷이 모두 붉다 —
이름 버튼 폭을 `ThemePixels(400)` 으로(런타임 축 — Decal·Image·Sprite·Volume 넘침 558 px),
Canvas 에 `SetNextItemWidth(150.0f)`(소스 축만 — 배율 2.25 에서는 안 넘친다),
StateMachine 이 공통 줄을 건너뜀(줄 0), MeshRenderer 텍스처 칸 미리보기 `ImVec2(30, 30)`
(소스 축 — 텍스처가 없으면 그리지 않는 조각이라 런타임은 자극을 못 한다).
같이 돈 검사: 잘라 그리기 계약 · 테마(121) · 키보드 탐색 · 상태 행렬 · 엔티티 저작(163) 모두 통과.

★ 목록이 다 차면 착수 때의 자극 단정(*240 에서 넘치는 본문이 하나는 있다*)은 설 자리가
없다 — 넘치는 드로어가 남지 않았다. 넘침을 재는 자가 살아 있다는 증거는 이제 첫 변이
(이름 버튼 폭을 `ThemePixels(400)` 으로 — 리터럴이 아니라 소스 축은 통과한다)가 진다.

같이 고친 결함:
- ImageComponent 방향 이동 칸 넷이 `BeginDragDropTarget` 을 열고 **닫지 않았다**.
- ImageComponent 의 텍스처 인덱스가 **창의 정적 변수**라 이미지 컴포넌트 둘이 한 값을 나눠 썼다 — 컴포넌트의 `curindex` 를 쓴다.
- Decal·SpriteRenderer·MeshRenderer 텍스처 칸이 모두 같은 ID `"MyDropTarget"` 으로 끌어 놓기 대상을 열었다 — 버튼 자신을 대상으로 한다.
- SoundComponent 의 잔향 적용이 **마지막 위젯(Reverb Index)의 편집만** 보았다 — 수준 편집도 적용한다.
- `Meta::DrawEnumProperty` 에 콤보 라벨 인자를 더했다(기본값은 예전과 같다).

바뀐 모양: Terrain 의 페인트·폴리지 두 모드가 따로 그리던 마스크 목록을 한 조각으로
합쳤다(선택 표시도 하나). 긴 안내 문장은 `TextWrapped` 로 바꿨다.

남은 것: **Import Settings**(`DrawYamlNodeEditor.cpp`) 는 이번에 손대지 않았다.

##### 2026-09-17 Import Settings 이관 — 15/15

자산을 고르면 인스펙터가 `.meta` YAML 을 `DrawYamlNodeEditor` 로 그린다. 예전 판은 키를 위젯
라벨에 붙여(`key##label`) 값 칸 **오른쪽**에 그렸고 폭은 ImGui 기본(창의 2/3)이었으며, 문자열은
256 바이트 버퍼에 `strcpy_s` 로 복사해 그보다 긴 값이면 런타임 검사로 죽었다. 64 비트 정수
(`timestamp` 같은 값)는 정수 판독에 실패해 `float` 로 떨어져 편집하면 값이 깎였다.

- 키는 공통 줄의 라벨이다. 파일이 키를 정하므로 고정 라벨 목록은 없고(라벨 열은 최소 폭), 긴 키는 공통 줄이 잘라 그리고 tooltip 으로 준다.
- 값: bool → 체크, 64 비트 정수 → `InputScalar(S64)`, 실수 → `InputFloat`, 나머지 → `std::string` 입력칸. 들여쓰기가 깊이마다 폭을 줄이므로 배치는 줄마다 그 자리에서 잰다.
- 맵·배열은 접힘 머리(`SpanAvailWidth`), ID 는 키·순번으로 묶는다.

자극: 명령은 클릭을 못 하므로 접힌 트리 안쪽이 그려지지 않는다. `editor.inspector expand on|off`
를 더해 접힘 머리를 모두 펼친다. `verify-inspector-drawer-layout.ps1` 은 추적되는 모델 메타
`Animation/Cha_Mon_5.fbx.meta`(긴 해시 · 맵 · 맵을 담은 배열)를 `editor.browser go`/`select` 로
고르고(보이는 결과 안에서만 고를 수 있다), 접힌 채 720 에서 한 번, 펼친 채 네 폭을 잰다.
접힌 줄 7 → 펼친 줄 27, 네 폭 넘침 0. 펼친 줄이 접힌 줄보다 커야 안쪽이 자극된 것이다.
240 폭 화면을 찍어 눈으로 확인했다(중첩 배열 안쪽은 값이 라벨 아래로 내려간다).

★ 검사의 읽기를 고쳤다. `wait` 는 게임 스레드 프레임이고 인스펙터는 표시 스레드에서 그려져,
같은 `wait 6` 이 기동 직후에는 인스펙터 3 프레임, 컴포넌트 적재 중에는 한 프레임도 못 채웠다
(ImageComponent 480 · PlayerInput 240 표본이 이전 상태를 읽고 붉었다). 폭마다 세 번 읽고
요청이 반영된(요청 폭 · 선택 엔티티 · 본문 열림) 마지막 읽기를 표본으로 삼는다. 끝까지
반영되지 않으면 그대로 실패로 남는다. 단정 246 · 이관 15/15, 연속 두 회차 통과.

★ 앞 커밋의 누락: 드로어 이관이 Decal·Sprite·Image 텍스처 칸의 받는 자리 다섯을 조각
`DrawAssetSlot` 하나(`AcceptDragDropPayload(payloadType)`)로 모았는데, 그 모양을 소스로 대조하는
`verify-content-browser-navigation.ps1` 을 그때 돌리지 않아 13·14 번이 붉은 채 올라갔다. 검사를
새 모양으로 고쳤다 — 유형이 변수인 받는 자리도 세어 `path_of` 를 단정하고, 조각을 부르는 자리의
유형 집합(데칼 Texture · 스프라이트 Texture · 이미지 UI_TEXTURE)을 기록과 대조하고, 데칼 세 칸이
Textures 폴더 검사를 통과한 이름만 자기 `Set*Texture` 로 넘기는지 본다. 이미지 칸 유형을
`Texture` 로 바꾼 변이가 붉다.

변이 —
옛 `DrawYamlNodeEditor.cpp` 를 그대로 되돌리면 공통 줄 0 · 240 펼침 넘침 78.75 px(착수 기준선)에 소스 축의 조각 함수 부재까지 붉다.
펼침 스위치를 무시하게 하면 펼친 줄이 접힌 줄 7 과 같아 네 폭 모두 붉다(자극 단정).
스칼라 값 칸에 `SetNextItemWidth(300.0f)` 를 넣으면 소스 축과 런타임 축(240 넘침 93 · 320 넘침 3 px)이 함께 붉다.
이미지 칸 유형을 `Texture` 로 바꾸면 탐색 검사 13 이 붉다.
같이 돈 검사: 명령 등록 골든(132) · CLI 발견 · 잘라 그리기 계약 · Content Browser 탐색(130) 통과.

##### 2026-09-17 W2-I2 RectTransform 최소 폭 — 16/16

착수 때 이 드로어는 **한 번도 재지 않았다.** `verify-inspector-drawer-layout.ps1` 은 컴포넌트를
`component.add` 로 붙여 자극하는데 RectTransform 은 붙일 수 없고 엔티티 유형이 정한다(UI 는
RectTransform 만, 캔버스는 둘 다 — `Entity::AttachSpatialComponent`). `object.create <이름> UI` 는
이미 있었으므로 검사가 UI 엔티티를 만들어 같은 네 폭에서 읽게 했다. 기준선: **240 넘침 122 px ·
공통 줄 0**(배율 2.25), 320 이상은 넘침 0.

원인은 배치였다. 왼쪽에 앵커 버튼 무리(`ImVec2(36, 36)`), 세로 구분선, 오른쪽에 3 열 표를 나란히
두어 표가 버튼 폭만큼 밀린 채 줄지 못했고, 끝의 `Text("World Rect …")` 는 자르지 않았다.

- 모든 줄이 `property_sheet` 한 벌을 쓴다(Anchors · Anchor Min/Max · Pos · Width/Height · Pivot · World Rect). 라벨 열이 다른 컴포넌트와 같은 x 에 서고, 좁으면 값이 라벨 아래로 내려간다.
- 앵커 버튼은 `Anchors` 줄의 값이다. 크기는 프레임 높이 × 1.6(값 폭을 넘지 않음), 팝업 칸은 × 1.4 — 논리 28·36 px 리터럴을 걷었다.
- vec2 줄은 값 열을 두 칸이 나눠 갖는다. 한 칸이 공통 배치의 축 최소치(`axis_need`)를 못 담으면 축마다 한 줄을 쓴다. 공통 배치의 `axis_stacked` 는 **축 셋** 기준이라 그대로 쓰면 320 에서도 두 칸이 세로로 내려갔다(화면으로 확인하고 고쳤다). ID 는 표 시절과 같은 라벨 아래 `##f0`·`##f1`.
- World Rect 는 읽기 전용 입력칸으로 틀이 잘라 그리고 tooltip 으로 전체를 준다.
- 같은 팝업을 ID 안과 밖에서 두 번 열던 중복을 걷었다. 편집 의미(앵커·피벗은 `SetAnchorsPivotKeepWorld`, 위치·크기는 직접 설정)는 그대로다.

결과: 네 폭 넘침 0 · 공통 줄 7 · 단정 268 · 이관 16/16. 240(두 칸 세로)·320(두 칸 가로) 화면을
찍어 눈으로 확인했다. 소스 축 목록에 `ImGuiDrawHelperRectTransformComponent`·`DrawVec2Row`·
`DrawAnchorPresetPopup`·`DrawAnchorIconButton` 을 더했다.

변이 —
옛 드로어를 그대로 되돌리면 공통 줄 0 · 240 넘침 122 px(착수 기준선)에 소스 축(`ImVec2(4, …)`·`ImVec2(1, 6)`)까지 붉다.
두 칸 줄에 `SetNextItemWidth(120.0f)` 를 넣으면 소스 축이 붉다(배율 2.25 에서는 120 이 칸보다 작아 런타임 축은 초록이다 — 소스 축이 필요한 이유).
World Rect 를 옛 `Text` 한 줄로 되돌리면 240 넘침 455 · 320 넘침 275 px 로 붉다.
못 잡는 변이: 두 칸을 늘 가로로 두어도(세로 전환 제거) 칸 폭이 1 px 까지 줄 뿐 넘치지 않아 초록이다 —
가독성 최소치는 넘침 자가 아니라 화면 확인으로만 보았다.
같이 돈 검사: 잘라 그리기 계약(46) · 상태 행렬(54) · 키보드 탐색(28) 통과, 드로어 배치 검사 연속 두 회차 통과.

##### 2026-09-18 W2-I1 공통 순회와 편집 정책 — 2안 착지

착수 상태는 1안이었다. 공간 컴포넌트 둘을 순회 **앞에서** 전용 드로어로 부르고 일반 순회에서
건너뛰었다. 머리줄·체크박스·메뉴를 드로어가 제각각 소유했고, 본문 기록의 인스턴스가 0 이라
"인스턴스마다 한 번" 을 셀 수단 자체가 없었다.

★ **정책이 화면에만 있었다.** `object.property <대상> Transform m_isEnabled false` 가 **성공한다**.
RectTransform 도 같다. 인스펙터에 체크박스가 없다는 것이 정책의 전부였고, 계획서가 *"UI 체크박스
생략만으로 통과 처리하지 않는다"* 고 적어 둔 그 구멍이 실재했다. 제거는 이미 거부되고 있었다.

- 편집 정책을 한 표로 세웠다(`EditorObjectOperations::PolicyOf`) — 표시 순서 · 개별 활성 · 개별 제거 셋을 **따로** 둔다. 인스펙터 순회와 CLI 진입점이 같은 표를 읽는다.
- 인스펙터는 순회 하나다. 머리줄·체크박스·메뉴를 순회가 소유하고 전용 드로어(Transform · RectTransform)는 본문만 그린다. 표시 순서는 정책의 `order`(rect → transform → 나머지)이고, 프레임마다 정렬 사본을 만들지 않도록 순서마다 한 번씩 훑는다.
- `object.property` 의 `m_isEnabled` 도 같은 표로 거부한다. 제거 거부의 손으로 적은 타입 비교도 표로 바꿨다.
- 엔티티 전체 전이는 정책의 대상이 아니다 — `object.enable <대상> on|off` 를 내고(Undo 포함), 기본 정보 체크박스도 그 작업을 지난다. 계획서가 금지한 *"`SetEnabled(false)` 무조건 거부"* 는 하지 않는다.
- RectTransform 이 쓰던 `EditorSectionHeader` 는 이로써 **제품 소비자 0** 이 됐다(`EditorModeButton` 과 같은 상태). 위젯을 지우지는 않았다 — W2-2 의 계약 표가 그 수를 못 박고 있다.

검사 `verify-inspector-spatial-policy.ps1`(단정 85 · 엔티티 5): 일반·Empty·UI·캔버스 두 모드마다
`object.describe` 의 인스턴스 id 집합과 인스펙터 본문의 인스턴스 집합이 같고 인스턴스마다 한 번,
표시 순서가 정책과 같고, 체크박스가 정책대로 있고 없고, 공간 컴포넌트의 개별 변경·제거가 거부되며
값이 그대로이고, 대조군인 MeshRenderer 는 개별로 꺼지고, 엔티티 전이가 공간 컴포넌트까지 끄고
켜고 `undo` 가 되돌린다. 캔버스 두 모드가 실제로 갈렸는지(RenderMode)도 함께 단정한다.

변이 —
옛 구조(순회 앞의 전용 호출 · 드로어가 머리줄 소유)를 되돌리면 인스턴스마다 본문 0 번 · 인스턴스 0 인 유령 본문 · MeshRenderer 체크박스 누락으로 붉다.
`object.property` 의 개별 활성 가드를 걷으면 공간 컴포넌트 넷이 전부 꺼지며 붉다.
가드를 모든 컴포넌트로 넓히면(통째로 막기) 대조군 MeshRenderer 가 거부돼 붉다.
머리줄이 정책을 무시하고 늘 체크박스를 내면 공간 컴포넌트 일곱 자리에서 붉다.
엔티티 전이가 공간 컴포넌트를 끄지 않게 하면(무조건 켜 두기) 전이 단정이 붉다.
Transform 본문을 두 번 그리면 공통 줄 6 이 3 과 달라 붉다.
못 잡는 변이: **표시 순서**는 지금 자극할 수 없다 — 공간 컴포넌트가 엔티티 생성자에서 먼저 붙어
정책 순서와 붙은 순서가 늘 같다. 순서를 무시해도 초록이다. 순서가 다른 보유 구성을 만들 표면이
생기면 그때 단정이 선다(지금은 수만 낸다).
같이 돈 검사: 드로어 배치(284) · 명령 등록 골든(133) · CLI 발견 · 엔티티 저작(163) · play 선택·undo · 잘라 그리기(46) · 상태 행렬(54) · 키보드 탐색(28) · 테마(64, dx12) · Content Browser 탐색(130) · 저작 줄바꿈 통과, 정책 검사 연속 두 회차 통과.

##### 2026-09-17 W2-I3 중첩 필드·배열 — 자극물 55 줄, 넘침 0

**실제 데이터로는 잴 수 없었다.** 일반 경로(`ReflectionTypedDraw.h`)로 그려지는 컴포넌트 중
중첩 구조체·배열·맵을 가진 것이 사실상 없다 — 컨테이너 필드를 가진 컴포넌트 셋(Animator ·
Sound · Script)은 전용 드로어이거나 숨긴 필드다. 그래서 분기마다 하나씩 닿는 합성 값을 세웠다:
`Editor/EngineGUIWindow/InspectorLayoutFixture.h`(스칼라·열거·벡터 · 중첩 구조체 두 겹 · 가변 배열
넷 · 고정 배열 · 집합 · 맵 둘 · 구조체 배열 · 긴 문자열·긴 맵 키). `editor.inspector fixture on`
이 엔티티 본문 끝에 `ReflectionFixture` 본문으로 그리고, 컨테이너 접힘 머리도
`editor.inspector expand on` 을 따르게 했다(전에는 명령으로 열 수 없었다).

검사는 줄 수를 **타입 정의에서 센 값과 같게** 단정한다 — 접힌 채 13, 펼친 채 55. 원소 하나라도
공통 줄을 지나지 않으면 수가 모자란다. 기준선: 펼친 줄 **31**(가변 배열 원소 18 · 고정 배열과
집합 원소 6 이 공통 줄 밖), 넘침 **240: 323 px · 320: 143 px**.

- 컨테이너 원소는 순번(`[0]`)을 이름으로 한 공통 줄에 선다(`BeginElementLine`). 가변 배열의 `^`·`v` 는 프레임 높이 정사각 버튼이고 그 몫을 값 폭에서 뺀다.
- ★ **넘침의 원인은 원소가 아니었다.** 원소를 옮겨 줄이 55 가 되어도 넘침이 그대로였고, 넘침이 폭과 무관하게 같은 오른쪽 끝(240 과 320 의 차이가 정확히 폭 차이)에서 멈췄다 — 고정 폭 항목 하나라는 뜻이다. 공통 줄 정본 `begin_property_line` 이 긴 라벨을 **잘라 그리기는** 하되 `TextUnformatted` 로 그려 **항목 크기는 잘리기 전 글자 폭**이었다. 화면에는 아무것도 안 보이는데 창의 내용 폭이 밀린다. 항목 크기를 라벨 열로 잡도록 고쳤다(`TextEx` 와 같은 순서로 폭만 바꾼다). 이 결함은 모든 공통 줄에 있었고, 이제까지 긴 라벨이 우연히 없었을 뿐이다.

결과: 네 폭 넘침 0 · 줄 13/55 · 단정 284 · 이관 16/16. 240 폭 화면의 윗부분(중첩 구조체·가변 배열)을
눈으로 확인했다. 창 높이에 막혀 아래쪽(맵·구조체 배열)은 화면으로 보지 못했고 수로만 판정했다.

변이 —
공통 줄의 잘린 라벨을 옛 `TextUnformatted` 로 되돌리면 줄은 55 인 채 240 넘침 323 · 320 넘침 143 px(기준선과 같은 값)로 붉다 — 넘침의 원인이 이 한 자리라는 증거다.
가변 배열 원소의 공통 줄을 빼면 줄 37 로 네 폭 모두 붉다(넘침은 0 — 기본 폭이 좁은 인스펙터 안에 들어가 줄 수만이 잡는다).
`^`·`v` 버튼 몫을 값 폭에서 빼지 않으면 네 폭 모두 넘침 101.5 px 로 붉다.
집합 원소의 공통 줄을 빼면 줄 52 로 붉다.
컨테이너 머리가 펼침 스위치를 무시하면 펼친 줄이 13 으로 붉다(자극 단정).
같이 돈 검사: 명령 등록 골든(132, 사용법 한 줄 갱신) · CLI 발견 · 잘라 그리기 계약(46) · 상태 행렬(54) · 키보드 탐색(28) · 테마(64, dx12) · Content Browser 탐색(130) · 저작 줄바꿈 · HashingString(Release) · Light 스크립트 통과, 드로어 배치 검사 연속 두 회차 통과.

#### 검증과 완료 판정

- 가용 content 폭 기준 240/320/480/720 logical px와 실제 줄 전환 경계 양쪽을 검사한다.
  사용자 배율과 OS DPI 100/125/150/200%를 구분하고 확대·축소 왕복을 확인한다.
  합성 geometry 검사와 실제 모니터 DPI 이동 결과를 따로 기록한다.
- 같은 깊이의 라벨·값 열 정렬, 숫자 최소 폭, 긴 이름·경로, 보조 버튼 도달성,
  세로 스크롤을 단정한다. 가로 잘림·겹침·0-size 입력칸은 실패다.
- 일반 엔티티·Empty·UI·Canvas(ScreenSpace/WorldSpace)별 기대 컴포넌트 신원을 기준으로
  헤더/펼친 본문 호출 수를 센다. 타입 수만 세거나 동일 함수의 호출문 수로 대체하지 않는다.
- 드래그/텍스트 편집 중 폭 변경, Tab·팝업·drag-drop, Undo/Redo, 선택 변경과 접기/펼치기,
  엔티티 활성 왕복, 씬·프리팹 저장/재로드에서 값과 필드 신원이 보존돼야 한다.
- UI 체크박스 생략만으로 개별 비활성화 제한을 통과 처리하지 않는다. 개별 변경 경로의 거부와
  엔티티 전체 비활성화·재활성화를 각각 확인한다. Transform 없는 엔티티에 더미 편집을 만들지 않는다.
- DX12/Vulkan 캡처와 §8 성능 검증을 수행한다. W2의 필드 ID·축 색 검사 통과는 배치 검증을 대신하지 않는다.
  중복 호출·고정 폭 회귀·전환 시 ID 변경·개별 활성화 허용을 주입해 해당 단정의 실패도 확인한다.

**선행:** W2 공통 위젯과 W1 폰트·geometry 계약. **후행:** W8 통합 회귀에 필수 포함.
W3 docking 완료를 구현 착수의 조건으로 삼지 않는다. W3와 공유하는 Inspector/창 선언 파일의 편집은
조정하고, 최종 dock/float·workspace 왕복 검증은 W3 이후 W8에서 수행한다.

<a id="w2-viewport-overlay"></a>

### W2-V — 씬뷰 툴바·오버레이 반응형 배치 규칙 (P1, 4일 · 초기 추정)

**2026-09-13 현재 외관 승인·고정. W2-V1 `done`(1일), V0/V2~V4 잔여로 전체는 `progress`다.**
**잔여의 실제 크기는 2026-09-16 재기준선(아래 표 다음 문단)을 정본으로 한다 — 넷 중
둘은 구현이고 둘은 판정이다.**
언리얼 참조 툴바 묶음과 폭에 따른 전체/축약/도구/메뉴 전환, 같은 프레임의 이미지 영역 기반 배치,
Mathematics를 사용하는 ImViewGuizmo 및 Blender 참조 표시를 적용했다.
FPS 상시 박스를 제거하고 Render Statistics에 Render Pass 창과 같은 Runtime 표시 함수 및 실제 GPU 패스 시간을 연결했다.
진행·검증 범위는 [EditorSceneViewportOverlayValidation.md](../analysis/EditorSceneViewportOverlayValidation.md)에 기록한다.
후속으로 Scene 표시를 원본 픽셀 기준 중앙 crop으로 변경했고, 기즈모·picking 좌표도 같은 이미지 사각형에 맞췄다.
공통 창 본문 여백은 8×6→3×2 logical px로 줄인다. 검증은 [EditorSceneCropValidation.md](../analysis/EditorSceneCropValidation.md)에 기록한다.
W4의 중앙 Host/렌더 타깃 크기 소유권 및 W5의 전체 입력 상태 머신까지 완료한 것으로 세지 않는다.

2026-09-11에 수립한 범위는 다음과 같다.
빈번한 창 이동·리사이즈에도 버튼의 정렬과 도달성을 유지하고, 방향 기즈모·HUD와 입력 영역이
겹치지 않게 한다. W2-I와 별도 후속이며 W4의 canvas 정본을 소비한다. 아래 다섯 단계는 4일 초기 추정이다.
스크린샷은 우측 도구의 밀집·잘림 증거로 사용하며, 원본 창 좌표·DPI나 원인별 기여도를 실측한 것으로 세지 않는다.

#### 착수 시점 문제와 W4/W5의 책임 경계

- `SceneViewWindow.cpp`의 좌측 Stats/Grid/Perspective/Camera는 이미지 좌상단에 5px씩 더하지만,
  우측 도구는 `SetCursorScreenPos(ImVec2(windowWidth - 270.f, currentPos.y))`로 놓는다.
  화면 좌표를 받는 함수에 창 원점을 더하지 않은 폭을 넣고, 실제 버튼 묶음 폭도 측정하지 않는다.
  따라서 같은 너비여도 창의 위치·폰트·라벨에 따라 상대 배치가 달라질 수 있다.
- 방향 기즈모는 창 원점과 고정 128px 크기, HUD는 이미지 오른쪽 끝과 창 기준 높이를 섞어 쓴다.
  HUD가 기즈모 아래에 놓이는 보정은 있지만 툴바와 기즈모가 차지하는 영역은 함께 계산하지 않는다.
- 이미지 크기와 `ImGuizmo::SetRect`는 창 전체 크기를 사용한다. picking·모델 배치의
  `CreateRayFromCamera`에는 크기 인자 대신 `imageMax`가 전달되고, 내부 계산은 이를 폭·높이로 나눈다.
  이 canvas 원점·extent 교정은 기존 **W4 범위**에서 해결하며 W2-V 공수에 중복 산정하지 않는다.
- 카메라 이동·picking·terrain 편집은 창 hover를 주로 보고, 모델 drop target은 이미지 전체다.
  이 함수에는 툴바·방향 기즈모·열린 팝업의 점유 영역을 함께 제외하는 계약이 없다.
  클릭 관통의 실제 재현 여부는 별도 검증하며, W2-V는 씬 내부 입력 분리, W5는 Edit/Play 소유권을 담당한다.

#### 공통 배치·입력 계약

1. **좌표의 정본은 같은 프레임의 canvas다.** W4가 content rect, 실제 image rect, clip rect와
   `extent = max - min`을 제공한다. letterbox 여백과 실제 이미지를 구분하고 오버레이는 이미지와 clip의
   교집합 안에 둔다. 창 원점·title bar를 소비자마다 더하지 않는다. ImGui 화면 좌표를 사용하며
   logical 간격은 W1 geometry 배율로 한 번만 변환한다. GPU target의 이전 크기를 UI 배치 기준으로 쓰지 않는다.
2. **버튼 묶음을 먼저 측정하고 같은 행에 정렬한다.** 실제 폰트·아이콘·라벨·padding·gap으로
   좌우 묶음의 폭과 공통 높이를 계산한 뒤 여백을 예약한다. 버튼 높이·아이콘 중심·텍스트 기준선을 맞추고,
   `-270`과 개별 `SameLine` 좌표 보정은 없앤다. 넓은 창에서도 묶음 내부 간격은 일정하게 유지한다.
   측정은 동작을 실행하지 않는 계산이며, 측정용으로 위젯을 한 번 더 그리지 않는다.
3. **한 줄 툴바 안에서 표시 밀도를 바꾼다.** 전체 라벨 → 아이콘 중심 → 더보기 메뉴 순서로
   전환한다. Select/Move/Rotate/Scale과 Snap을 우선하고 Stats/Grid/카메라 세부 설정부터 메뉴로 옮긴다.
   도구 아이콘조차 모두 들어가지 않으면 현재 도구 선택 버튼 + Snap + 더보기로 줄인다.
   모든 명령은 이름·현재 상태가 보이는 메뉴/tooltip으로 접근 가능해야 한다. 버튼·글자를 가독성 이하로
   줄이거나 잘라 숨기지 않는다. 최소 조작 행도 담지 못하는 크기는 호스트 최소 content 크기로 제한하며,
   일시적인 0-size/최소화에서는 그리기·입력만 중단한다. 자동 줄바꿈으로 툴바 높이와 render extent가
   서로 바뀌는 순환을 만들지 않는다. 모드 경계는 실측 폭과 복귀 완충 폭으로 정한다.
4. **방향 기즈모와 HUD의 자리를 함께 예약한다.** 방향 기즈모는 툴바 아래 우측에 놓고,
   FPS/해상도와 Runtime/GPU 정보는 사용자 결정에 따라 **Render Statistics 팝업**에 모으고 상시 HUD는 두지 않는다.
   폭뿐 아니라 높이도 검사한다. 기즈모를 표시할 수 없는 높이에서는 방향 선택 메뉴로 접근할 수 있어야 한다.
   현재는 숨김까지만 구현됐고 대체 방향 메뉴는 V2 잔여다. 보이는 항목끼리 겹치지 않아야 한다.
5. **보이는 영역과 입력 판정은 같은 배치 결과를 쓴다.** 버튼·방향 기즈모·열린 팝업의
   hit rect와 활성 조작이 소비한 입력을 씬 카메라·선택·월드 기즈모·모델 drop·terrain 편집에 전달하지 않는다.
   빈 간격과 읽기 전용 HUD 뒤의 씬은 계속 조작할 수 있다. 창 hover나 전역 `WantCaptureMouse` 하나만으로
   판정하지 않고 기존 ImGui/기즈모 상호작용 결과와 합친다. 누른 곳의 소유권은 release까지 유지하고,
   숨김·포커스 상실로 취소된 조작의 release도 씬으로 넘기지 않는다. Q/W/E/R/T는 텍스트 편집·팝업·
   다른 창 포커스를 존중한다. W5의 PlayingPossessed에서는 편집 도구가 게임 입력을 가져가지 않는다.
6. **리사이즈는 표시만 바꾸고 명령 신원은 유지한다.** ID는 창·명령의 안정 신원을 사용하며
   아이콘·Perspective/Orthographic 라벨·메뉴 이동 여부에 의존하지 않는다. 현재 도구·Snap·Grid·투영 상태,
   keyboard nav·팝업·Undo 의미를 보존한다. 누른 버튼이 이동하거나 overflow로 옮겨져도 다른 명령이
   release를 받지 않는다. 조작 중 표시 모드 전환은 가능한 동안 보류하되 영역 안에 유지할 수 없으면
   조작을 명시적으로 취소한다. 원점·가용 크기는 매 프레임 갱신하고 폰트·배율·라벨·가시성 변화는 측정
   캐시를 무효화한다. 표시 모드 전환만으로 렌더 타깃을 재생성하거나 한 프레임 늦은 hit rect를 사용하지 않는다.

`EditorModeButton`과 표준 ImGui Menu/Popup/Tooltip을 사용한다. 별도 입력 프레임워크나 새로운
게임 모드 정본을 만들지 않는다. 인스펙터와는 W1 치수·W2 위젯·안정 ID 원칙을 공유하되,
속성 편집의 세로 reflow와 씬뷰 툴바의 한 줄/overflow 정책은 각 표면에 맞게 유지한다.

#### 이관 순서와 초기 공수

| 단계 | 작업 | 초기 추정 | 완료 기준 | 2026-09-13 상태 |
|---|---|---:|---|---|
| W2-V0 | canvas 소비 계약·묶음 측정·표시 모드 계산 | 0.5일 | 같은 크기에서 창 원점이 달라도 상대 배치 동일, W4 정본 외 좌표식 없음 | **구현 닫힘 · 판정 미비**(2026-09-16 재기준선) — `W4 Host canvas 통합 남음` 은 닫혔다. `SceneViewWindow.h:4·74` 가 `EditorViewportCanvas.h` 의 `editor::ViewportCanvas m_canvas` 를 들고 오버레이도 그 헤더를 소비한다. 남은 것은 *"정본 외 좌표식 없음"* 을 읽을 **자**다(소스 축 0 건) |
| W2-V1 | 좌우 툴바 공통 정렬·아이콘/더보기 전환 | 1일 | 최소 크기부터 넓은 창까지 도구·설정 도달 가능, 명령 ID·상태 유지 | done — 4개 폭 모드·도구/상태 조작 검증 |
| W2-V2 | 방향 기즈모·HUD의 공간 예약과 작은 높이 대응 | 0.5일 | 툴바/기즈모/HUD 겹침·잘림 0, 방향 선택·Stats 대체 접근 가능 | progress — **잔여가 절반으로 좁혀졌다**(2026-09-16 재기준선). 높이 축은 **실재한다**: `SceneViewportOverlay.h:42` 의 `showGizmo` 가 낮은 높이에서 거짓이 되고 `.cpp:256` 의 `drawViewGizmo` 가 그리기를 끈다. Stats 대체 접근도 이미 있다(`RenderStatistics` 팝업, `.cpp:253`). 없는 것은 **기즈모가 숨은 뒤의 방향 선택 창구 하나** — 오버레이 팝업 일곱 중 방향을 고르는 것이 0 이다 |
| W2-V3 | 씬 입력의 오버레이 제외·조작 취소/소유권 연결 | 1일 | 클릭·카메라·drop·terrain 입력 관통 0, W5와 소유권 연결 지점 확정 | progress — **그 "포인터 차단 구현"이 기즈모를 지우고 있었다**(2026-09-14 `eb602724`). 오버레이 제외가 `ImGuizmo::Manipulate` 호출 조건에 들어가 있었는데 그 호출은 그림과 입력을 함께 하므로, 툴바에 마우스를 올리는 것만으로 기즈모가 사라졌다. 제외를 `ImGuizmo::Enable` 로 옮기고 소스 대조 게이트로 못 박았다(§W2-V3 후속). ★ **2026-09-16 재기준선 — `drop/terrain 입력 관통` 은 닫혔다**: `SceneViewWindow.cpp:222` 의 `canvasInput = pointerInCanvas && !m_overlay.blocksPointer` 하나가 우클릭 카메라(:380)·terrain(:441)·drag-drop(:486)·선택(:553)을 함께 관문한다. W5 소유권 연결도 §W5 가 모드 하나로 닫았다. 남은 것은 **`resize 중 조작 취소` 하나**이고 그 기제는 아직 0 건이다(`SceneViewWindow.cpp` 에 취소·resize 문자열 0) |
| W2-V4 | 연속 리사이즈·DPI·입력 회귀 및 관측 확장 | 1일 | 아래 행렬·성능 gate 충족, 위치/ID/입력 변이를 실패로 검출 | progress — **이 줄은 구현이 아니라 회귀다**(2026-09-16 재기준선). `연속 resize` 의 제품 몫은 2026-09-14 W4 후속(extent 기반 resize·렌더 배율)이 가져갔고 `verify-editor-viewport-extent.ps1` 이 캔버스·렌더 해상도를 단정한다. DPI 축은 W8-3 통합 행렬이 user scale 1.0/1.5 로 돈다. 남은 것은 **씬뷰 오버레이 자신의 위치/ID/입력 변이 검출** 하나다 |

##### 2026-09-16 재기준선 — 다섯 줄 중 넷이 줄어들었다

W2-I 와 같은 날 같은 이유로 다시 읽었다. 위 표의 상태가 2026-09-13 기준이었고 그
뒤로 W4 후속(extent resize·렌더 배율, 2026-09-14) · W5 · W8-3 이 착지했다. 결과:

| 줄 | 2026-09-13 잔여 | 2026-09-16 |
|---|---|---|
| V0 | W4 Host canvas 통합 | **구현 닫힘** — 남은 것은 좌표식 단정의 자 |
| V2 | 낮은 높이의 방향 메뉴 · Stats 대체 접근 | **방향 창구 하나** — 높이 축과 Stats 는 이미 섰다 |
| V3 | drop/terrain 관통 · 조작 취소 · 소유권 | **조작 취소 하나** — 관문과 W5 연결은 닫혔다 |
| V4 | 연속 resize · DPI · 성능 | **오버레이 변이 검출 하나** — 앞의 셋은 W4·W8 이 가져갔다 |

★ **남은 넷은 서로 성질이 다르다.** V2 는 없는 UI 를 만드는 일(제품), V3 는 없는
기제를 만드는 일(제품), V0 와 V4 는 **이미 도는 것을 읽을 자를 만드는 일**(판정)이다.
이 저장소에서 반복된 실패 양식이 그 둘을 섞어 *"남았다"* 로 적는 것이었다 — 판정이
없으면 착지해도 초록이 무엇을 뜻하는지 말할 수 없고, 그 상태의 잔여는 구현 잔여보다
싸다고 오해된다(§W8-1·§W8-3·§W2-1~4 가 전부 같은 계통이었다).

★ V0 의 자를 세울 때 주의할 것 하나 — 캔버스 **런타임** 축은 이미 있다
(`verify-editor-viewport-extent.ps1` 이 `canvasWidth/Height` 와 렌더 해상도를 단정).
없는 것은 *"정본 외 좌표식이 없다"* 는 **부재 단정**이고 그것은 소스 축이다. 두 벌을
세우지 말고 없는 쪽만 더한다(§W2-4 가 시각 기준선에서 같은 판단을 했다).

##### W2-V3 후속 — 오버레이 제외는 그림이 아니라 입력에 건다 (2026-09-14)

`ImGuizmo::Manipulate` 는 **한 호출로 그림과 입력을 함께** 한다. W2-V3 의 "오버레이
위 입력 제외"가 이 호출을 감싸는 조건(`!m_overlay.blocksPointer`)으로 적혀 있었고,
`blocksPointer` 는 순수 hover 로 서므로 상단 툴바에 마우스를 올리는 것만으로 기즈모가
통째로 사라졌다(사용자 보고). 조건 한 줄이 두 가지 뜻을 겸하고 있었던 것이다.

**고친 방식.** 그림 조건은 선택·편집 가능 판정만 본다(`obj && selectionEditable &&
!selectMode`). 제외는 `ImGuizmo::Enable` 로만 흘린다. 다만 hover 내내 `Enable(false)`
를 주지 않는다 — 1.10 의 `ComputeColors` 가 `mbEnable` 이 거짓이면 색 7 개를 전부
`inactiveColor` 로 덮어, 사라지던 것이 **회색으로 변하는 것**으로 바뀔 뿐이다.
`CanActivate()` 가 `IsMouseClicked(0) && !IsAnyItemHovered() && !IsAnyItemActive()` 이므로
**새 클릭이 들어오는 한 프레임만** 끄면 충분하다. 드래그 중에는 `IsUsing()` 이 참이라
툴바 위로 넘어가도 안 끊기고, 눌러 둔 채 캔버스로 들어와도 새로 잡히지 않는다.
`Enable` 은 전역 상태라 RAII 로 되돌린다. 버튼 자체는 `CanActivate` 가 이미 막고 있고,
보호가 실제로 필요했던 자리는 **버튼 사이 빈틈**(ImGui 항목이 아닌 배경)이다.

**왜 회귀로 서지 않았나.** 기즈모는 ImGui 드로 리스트에 직접 그려져 CLI 가 읽는 표에
흔적을 남기지 않는다. 표시 여부를 묻는 창구를 새로 만들어도 그 창구가 같은 조건을 다시
적는 두 벌이 된다(W5 가 "기즈모 잔류는 CLI 로 못 몬다"고 적어 둔 것과 같은 벽이다).
그래서 `verify-scene-gizmo-input-boundary.ps1` 로 **양쪽을 다 소스에서 유도해** 맞댄다 —
한쪽은 오버레이의 `blocksPointer` 대입식에서 닫은 관문 이름(실측 12 개), 다른쪽은
`Manipulate` 를 감싸는 블록 머리들(중괄호를 세어 올린다). 단정은 셋이다: 감싸는 어떤
조건도 관문을 언급하지 않는다 · 감싸는 `if` 의 **첫 문장**이 관문을 인자로 하는 `Enable`
을 세운다 · 같은 문장에 `Enable(true)` 복구가 있다. 31 checks, run-all 연결.

★ 단정 범위를 "호출 앞까지"로 두었을 때 **변이 하나를 놓쳤다** — 그 사이에
`isWindowHovered` 라는 다른 관문 이름이 우연히 놓여 있어 `Enable` 을 상수로 굳혀도
초록이었다. 첫 문장으로 좁혀서야 이빨이 섰다. 변이 다섯은 게이트 머리에 적혀 있다.

**여전히 못 잡는 것.** 소스 대조라 화면을 보지 않는다. 조건을 통과시켜 놓고 다른 이유로
기즈모가 안 그려지는 경우는 W8 visual golden 몫이다(캔버스 정책 `fill`→`crop` 되돌림
변이와 같은 칸에 쌓인다).

W4의 첫 작업으로 canvas rect 생산·기존 소비자 교정을 먼저 제공한다. W2-V0의 측정 정책은
그 계약을 기준으로 준비할 수 있고, 제품 연결은 이 선행 작업 뒤에 수행한다. W4 전체의 view demand
완료까지 기다릴 필요는 없다. W2-I와는 순차 의존이 없으며 `SceneViewWindow.cpp`를 공유하는 W4/W5와
편집을 조정한다. W3 docking·W5 모드 전환을 포함한 최종 판정은 W8에 연결한다.

#### 검증과 완료 판정

- 가용 canvas 폭 320/480/720/1024 logical px, 높이 180/320/640, 실제 전환 경계 양쪽을 검사한다.
  최소 조작 폭 미만·0-size에서는 정의된 제한/중단 동작을 확인한다. OS DPI 100/125/150/200%와
  사용자 배율을 구분하고 실제 DPI 왕복과 합성 geometry 결과를 따로 기록한다.
- 원점 0·이동한 창·음수 화면 좌표, 도킹 분할·최대화·복원·workspace 재로드를 포함한다.
  같은 크기에서 원점만 이동하면 각 항목의 상대 위치는 같아야 한다. 폭·높이와 경계를 100회 이상
  왕복해 겹침·버튼 유실·표시 모드 진동·이전 프레임 위치로 입력되는 현상을 검사한다.
- 버튼을 누른 채 리사이즈, 팝업/텍스트 편집 중 단축키, 방향/월드 기즈모 drag, 모델 drop,
  terrain brush, 카메라 회전, focus loss를 확인한다. UI 조작으로 씬 선택·변형이 발생하거나 같은
  입력을 둘이 소비하면 실패다. 빈 canvas 조작과 읽기 전용 HUD 뒤 조작은 유지돼야 한다.
- 각 명령이 버튼/메뉴 중 어디에 있는지, 활성 명령·rect·입력 소비 결과, 방향 기즈모/HUD rect와
  표시 모드를 같은 프레임 스냅샷에 게시한다. 기존 창/dock rect 관측만으로 버튼 정렬을 통과 처리하지 않는다.
  창 원점 누락·기즈모 영역 미예약·전환 시 ID 변경·입력 제외 누락을 각각 주입해 검출도 확인한다.
- DX12/Vulkan에서 첫 target 대기·resize·Edit/Play/Eject/Stop을 검증하고 §8 성능 gate를 적용한다.
  검은 뷰포트·target generation 문제는 W4와 별도로 판정하며 툴바 정렬 성공으로 해결됐다고 세지 않는다.
  이번 계획 반영은 빌드·실행·위 회귀 검증의 완료 실적이 아니다.

<a id="w2-content-browser"></a>

### W2-B — 컨텐츠 브라우저 내부 분할·탐색·생성·검색 도구 (P1, 7일 · 초기 추정)

**2026-09-13 현재 외관 승인·고정. 배치·탐색·검색·새 폴더는 구현, 최근/전체·상태 복원·생성/검증 잔여로 `progress`다.**
왼쪽 폴더와 오른쪽 자산의 배치뿐 아니라 폴더 폭 조절, 현재 위치 확인, 이동, 생성과 검색까지
한 브라우저 안에서 수행할 수 있게 한다. W2의 공통 위젯을 소비하고 W3의 외부 docking,
W7의 목록/썸네일 최적화와 담당 범위를 구분한다. 아래 여섯 단계의 초기 추정 합계는 7일이다.

#### 2026-09-12~13 적용 완료 부분

- 고정 200px 트리를 logical 선호 폭·드래그 분할선·좁은 창 폴더 팝업으로 교체했다.
  선호 폭은 기존 `EditorSettingsStore`에 저장하며, 창 크기에 따른 제한 폭과 분리한다.
- 오른쪽 상단 New / 뒤로·앞으로·상위 / breadcrumb / 통합 검색 / 유형·보기 도구를 연결했다.
  가용 폭에 따라 탐색·검색이 두 줄로 나뉘고, 긴 경로는 조상 메뉴로 접는다.
- 폴더와 지원 자산을 함께 표시한다. 루트·트리·폴더 타일·경로 바의 이동은 같은 상태를 사용한다.
  New와 폴더 우클릭은 새 폴더 및 기존 Volume Profile 생성 경로를 공유한다.
- Scene 탭은 저장된 도킹 배치를 불러온 후 시작 시 한 번 선택한다. 이후 Game 선택을 유지한다.
- 디렉토리 트리는 프로젝트 머리행, 14 logical px 들여쓰기와 16px 투명 PNG 폴더 아이콘을 사용한다.
  분할선 뒤 10px 여백을 두고, 화살표·아이콘·이름을 행 중앙에 맞춘다.
  목록·트리 안쪽 여백은 후속 요청으로 6→2px로 줄였다.
  고정 UI 이미지의 수명은 EditorAssetPresentation이 소유하며 W7 자산 썸네일과 구분한다.
  2026-09-13 트리·프로젝트·폴더 타일/목록·파일 유형 이미지를 Microsoft Fluent Emoji 3D로
  통일했다. 유형 분류를 표시 서비스에 모아 필터와 이미지의 확장자 판정을 일치시킨다.
- W2-B 전체 완료는 아니다. 최근/전체 범위, 이력별 검색·선택 복원, 전체 회귀 행렬이
  후속 범위다.

  **2026-09-16 재기준선.** 위 목록에 있던 셋이 그 사이에 닫혔다 — 이 문단의 상태는
  2026-09-13 기준이었고 그 뒤로 W3 와 W7-0~W7-6 이 착지했다.

  · **W3 workspace 저장 이관 — 닫힘.** 트리 폭은 `EditorSettingsStore` 의
    `Get/SetContentTreeWidth` 가 지고 W3 autosave 가 소유한다(프로젝트 설정에 쓰지
    않는다).
  · **W7 snapshot/cache/clipping 연결 — 닫힘.** 창이 `browser_cache_listing` 을
    소비한다(§W7-1·§W7-5).
  · **W7 비동기 썸네일 소비 — 닫힘.** 타일이 `thumbnail_acquire` 로 묻는다(§W7-6).
    아래 표의 "썸네일 → W7 소비 연결" 칸도 이것으로 닫힌다. 모델 렌더 썸네일만
    W7 쪽 잔여다.

  ★ 남은 둘은 **실재한다.** 검색은 현재 폴더에 `ImGuiTextFilter` 하나뿐이고
  `m_history` 는 경로 목록일 뿐이라 방문별 검색어·선택이 없다.

  ★ **`전체` 의 원천을 먼저 못 박는다.** 계획서는 "별도 자산 DB를 만들지 않는다" 고
  적었는데, 쓸 만한 전역 색인이 없다 — `AssetIdentityRegistry::Entries()` 는
  `canonicalInput`·`uuid`·`context` 뿐이고 **경로도 타입도 없는** GUID 유도
  레지스트리라 브라우징 원천이 못 된다. 그래서 `전체` 는 **W7 목록 캐시 위를 걷는다.**
  디스크 접촉은 캐시가 이미 하는 것 이상 늘지 않지만, 재스캔 예산이 프레임당 1 이라
  첫 순회가 점진적으로 채워진다 — 설계 귀결이지 결함이 아니다. 그 과정에서
  `browser_files` 의 `probes` 가 0 을 유지해야 한다(§W7-5 계약).
  실행 검증 결과는 [Content Browser 검증](../analysis/EditorContentBrowserLayoutValidation.md)에 기록한다.

#### 2026-09-17 방문 이력·가상 위치·끌어다 놓기 신원·Volume Profile 생성 착지

- **밖에서 읽고 모는 창구가 0 이었다.** `editor.browser` 를 세웠다 — 요청함에 넣고 창이 프레임
  머리에서 적용하며 프레임 끝에 사본을 게시한다(`editor.viewport` 와 같은 모양,
  `ContentBrowserControl.h`). 위치·범위·검색어·선택·이력·버튼 상태·결과(앞 256)·스크롤·전체 순회
  진행·최근 수를 낸다. 요청은 `go <경로>|@recent|@everything`·`back`·`forward`·`up`·`search`·
  `select`(지난 프레임에 보인 결과 안만)·`scroll`·`create folder|volume <이름>`.
- **이력의 칸은 경로가 아니라 방문이다**(범위·폴더·검색어·선택·스크롤). 떠날 때 적고 돌아올 때
  되살린다(계약 4). 선택은 부모 폴더의 캐시 목록에 지금도 있을 때만 되살린다(계약 6). 지워진 폴더는
  Assets 안의 가장 가까운 조상으로 가고 이유를 적는다.
- **최근 항목·전체 자산은 가상 위치다**(계약 5). 트리 위 두 줄로 들어간다. 전체는 W7 목록 캐시를
  걷고 캐시에 없는 폴더는 프레임당 2 개만 훑어 차오른다. 이름 다음 경로 순으로 정렬해 동명 파일이
  둘 다 안정적으로 나온다. 최근은 메타를 읽어 인스펙터에 올리는 데 성공한 선택과 열기에 성공한
  파일만 적고, 프로젝트별 `Library/EditorState/ContentBrowserRecents.txt`(루트 상대 32 줄)에 둔다.
  파일이 사라지면 목록과 저장 파일에서 빠진다. 가상 위치에서는 New·빈 곳 메뉴·Up 을 닫는다.
- **잘라 그리기 — 수치가 먼저였다.** Release 실측으로 전체 자산 274 개 p95 2.75ms, 4,274 개
  15.9ms 였고 units 가 결과 수와 같았다(안 보이는 타일까지 썸네일 조회째 그렸다). 줄 단위 clipper
  를 넣어 1.18ms · 3.25ms(그린 타일 5). ★ 파일 타일은 폴더 타일보다 선 하나와 3px 만큼 높아
  clipper 가 첫 줄(폴더)로 줄 높이를 재면 파일 줄마다 어긋난다 — 그린 칸의 최대 높이를 모든 줄의
  최소 높이로 준다(폴더만 있는 줄이 파일 줄 높이로 맞춰진다). ★ 남은 3.25ms 는 매 프레임 모으고
  정렬하는 비용이고 결과 수에 선형이다 — 수만 남기고 판정하지 않는다.
- **끌어다 놓기 신원.** payload 가 파일 이름이었고 받는 자리 17 곳이 `<유형 폴더>\이름` 으로
  경로를 다시 지었다 — 전체 자산에서 `Animation/Cha_Mon_5.fbx` 를 끌면 `Models/` 쪽이 열렸다.
  이제 UTF-8 전체 경로 하나다(`EditorAssetDragPayload.h` 의 `set_payload`·`path_of`). 이름만 저장하는
  소비자 가운데 데칼 셋·스프라이트 시트는 그 폴더 바로 아래 파일만 받고(`lives_in`), 폴리지는
  stem→GUID 가 끌어 온 파일로 돌아오는지 본다. ★ 남는 것: `DataSystem::LoadSharedTexture` 의
  캐시 키가 stem 이다 — 엔진 캐시의 신원 문제라 이 조각 밖이다.
- **Volume Profile 생성.** OS 저장 대화상자에서 이름만 가져와 넘겨받은 폴더에 썼고, 같은 이름엔
  말없이 숫자를 붙였으며, 취소·쓰기 실패·메타 실패가 반환값을 버린 호출자 앞에서 같은 침묵이었다.
  이제 새 폴더와 같은 이름 대화상자를 쓰고 `CreateVolumeProfile(directory, name, created, error)` 가
  같은 이름·잘못된 이름·쓰기 실패·메타 실패마다 이유를 돌려주며 반쯤 만든 파일을 지운다. 만든 것을
  고른다. 두 메뉴가 같은 술어(`CanCreateVolumeProfileIn`)와 같은 생성 경로(`CreateNamedAsset`)를
  쓴다. 생성이 동기라 늦은 완료는 없다.
- **검사 `Tools/regression/verify-content-browser-navigation.ps1`** — 실행 축(에디터 1 회·창 숨김·
  표본 22)과 소스 축, 단정 114. Debug·Release 통과. 변이 14 종 전부 잡음 — 소스 8(경로 다시
  짓기·이름 싣기·받는 자리 이동·데칼 가드 제거·폴리지 검사 제거·숫자 붙이기·메뉴 술어 분기·
  Show in Folder 선택 누락), 실행 6(앞으로 이력 유지·검색어 복원 누락·조상 복구 누락·같은 이름
  덮어쓰기·전체 자산 예산 제거·잘라 그리기 제거). ★ 받는 자리는 수가 아니라 (파일, 유형) 목록으로
  맞댄다 — 한 자리가 빠지고 다른 자리가 늘어도 수는 17 그대로였다.
  ★ 에디터가 도는 중에 폴더를 지워야 하는데 결과 파일은 에디터가 공유 없이 열어 읽히지 않는다 —
  에디터가 만드는 폴더(`create folder Signal`)를 표지로 쓴다. ★ 최근 항목 정리는 목록 캐시 나이
  (1 초)에 걸려 Release 에서만 붉었다(프레임이 빨라 1 초 안에 도착) — 같은 이름 폴더 만들기가
  실패하면서 캐시를 버리는 것으로 자극한다.
- **함께 고친 것.** 잘라 그리기 뒤 `verify-browser-thumbnail-contract.ps1` 의 요청이 0 이 됐다 —
  뿌리에 올린 검사용 PNG 가 폴더 스무 개 뒤 화면 밖이었다. 전용 폴더에 두고 `editor.browser go`
  로 연다(56 단정 통과). `verify-browser-filesystem-contract.ps1` 가 `lives_in` 의 `equivalent` 를
  잡아 `browser_canonical` + 어휘 비교로 바꿨다. W7 의 `editor.thumbnail` 이 명령 씨앗에 빠져
  있었고, `5eebcc23` 이 골든 갱신 없이 `material.override` 를 넣었다 — 둘 다
  `cli_registry.golden.tsv` 에 함께 적었다.
- **이번에 돌린 검사.** Debug: 탐색·`verify-cli-registry-golden`·`verify-cli-discovery`·
  `verify-editor-command-surface`·`verify-model-authoring-transaction`·`verify-model-multifile-import`.
  Release: 탐색·`verify-browser-filesystem-contract`·`verify-browser-thumbnail-contract`·
  `verify-editor-widget-clipping`·`verify-editor-chrome-perf`. 소스만: `verify-asset-guid-contract`·
  `verify-asset-runtime-change-boundary`·`verify-authoring-line-endings`·`verify-hierarchy-flatten-contract`·
  `verify-nlohmann-retirement`·`verify-scene-gizmo-input-boundary`.
  ★ `verify-editor-chrome-perf` 골든을 갱신했다 — 기본·선택 두 회차 모두 정점 −24·인덱스 −36·draw
  command −10 이다. 이미지 여섯 장 분량으로, 잘라 그리기가 화면 밖 타일의 아트워크를 더 내지 않은
  것이다. 픽셀 골든의 브라우저 영역은 그대로다.
  ★ 이 변경 전부터 붉은 셋 — `verify-asset-presentation-boundary`(`d2b70402` M4 2단계가
  `ContextRegister(kMaterialPicker` 를 창 선언으로 옮긴 뒤 안 따라옴), `verify-editor-chrome-golden`
  (9-16 로그 작업이 아래 상태 막대에 넣은 개수가 회차마다 달라 "같은 상태 두 번" 재현 축부터 붉다 ·
  브라우저 영역 차이 0), `verify-asset-authoring-ownership`(`efsw.dll` 을 exe 옆에서 찾는데 지금은
  `Bin/x64-*/Runtime/Editor/` 에 있다).
  **셋을 닫았다(2026-09-17).** 모두 Release 에디터에 `-Exe`·`-EditorExe` 를 명시해 돌렸다.
  · `verify-asset-presentation-boundary` — 통과. 머티리얼 선택기의 실제 경로 세 고리를 단정한다:
  `EditorAssetPresentation.cpp` 의 `bind_window_body(kMaterialPicker, … RenderMaterialPicker)`,
  `EditorStandardWindows.h` 의 `panel<&windows::draw_material_picker>`, `.cpp` 의
  `EDITOR_DEFINE_WINDOW_ENTRY(material_picker, …)`(텍스처 가져오기 선택기도 같은 셋). 고쳐 보니
  한 줄 뒤 `AddFontFromFileTTF` 단정도 W1(`afe52737`)이 폰트 적재를 `EditorFontResources` 로 모은 뒤
  낡아 있어 `add_optional_font` 로 바꿨다. 변이 4 종(본문 비움·선언 진입점 바꿈·정의 끊음·폰트 창구
  끊음) 전부 붉음.
  · `verify-editor-chrome-golden` — 25 단정 통과. **계수를 고정하지 않고 가렸다.** 계수는 명령마다
  늘고(`scene.save` 표지 자체가 로그를 남긴다) 자릿수가 바뀌면 오른쪽 정렬이 움직이므로, 캡처 직전에
  비워도 대기 12 초 사이 비동기 로그를 막을 수 없다. 가릴 칸은 제품이 낸다 — `editor.dock` 의
  `statusCountsSlot`(ProfileFrame 버튼 오른쪽 끝 ~ 디버그 버튼 왼쪽 끝, 자릿수와 무관한 두 끝).
  칸을 못 받으면 단정이 붉고, `-Update` 는 가리기가 서지 않으면 골든을 뜨지 않는다. 가린 뒤 재현 축
  0 을 먼저 확인하고 골든을 다시 떴다. 골든이 움직인 것은 ① 가린 칸 36,315 화소와 ② 칸 바깥 380
  화소다 — ②는 씬 툴바 이동·회전 버튼 경계와 Content Browser·Inspector 탭 제목·Hierarchy 한 글자의
  명암 차(1~17 단계)이고, 두 번 따로 실행해 화소 하나까지 같았으니 흔들림이 아니라 골든(`ff679836`)
  이후의 결정적 변화다. 어느 커밋인지는 가르지 않았다. 변이(가리는 칸을 왼쪽 절반으로)가 원래 증상
  그대로 붉었다 — 4 건, 재현 축 x 1023..1193 · y 910..933.
  · `verify-asset-authoring-ownership` — 통과(11 축). 감시자 런타임을 `RuntimeLauncher.cpp` 와 같은
  규칙으로 찾는다 — exe 폴더에서 두 단계 위까지 `Runtime/layout.version`(판 1), 그 아래
  `Runtime/Editor/efsw.dll`, 그리고 `Runtime/Manifests/CreatorEditor.json` 이 그 파일을 싣는지.
  가짜 배치 변이 5 종(파일 없음·옛 자리·목록에서 뺌·판 2·layout 없음) 전부 붉고 대조군은 통과.
  ★ 그 전제를 넘자 **뒤에 가려 있던 둘**이 드러났다. `foliage-ads` 는 거부 모드가 없는 탐침이라
  거부가 곧 `Failed`(exit 4)인데 검사가 0 만 받아 출력도 안 보고 던졌다 — 기대 종료 코드를 받게
  했다(거부가 풀려 커밋되면 0 이 되어 붉다). `blackboard-empty` 는 **탐침 명령의 판정이 틀려**
  있었다 — `committed keys=0` 을 찍고도 반환식이 `!empty && …` 라 빈 판의 성공을 언제나 Failed 로
  냈다. LC1(`987a552e`)이 실패를 종료 코드에 이은 뒤로 계속 붉었을 자리다. 반환식을 고쳤다(제품
  빌드가 필요한 변이는 걸지 않았다).
- **프로젝트 전환 실행 검증 — 2026-09-17 닫힘.** ★ 이 제품에는 세션 안에서 프로젝트를 바꾸는
  길이 **없다** — 자산 뿌리는 기동 때 `--development-project` 로 한 번 정해지고(`App.cpp`),
  `PathFinder::Initialize` 호출자는 기동 경로 하나다. 창의 "뿌리가 바뀌면 이력·선택·범위를
  비운다" 분기는 첫 프레임 초기화로만 돈다. 그래서 전환은 **다른 프로젝트로 다시 띄우는 것**이고,
  새는 통로는 같은 작업 공간(`CREATOR_EDITOR_WORKSPACE_DIR` — 트리 폭만 싣고 위치는 안 싣는다)과
  최근 항목 저장 파일 둘이다. `verify-content-browser-navigation.ps1` 에 전환 축(17)을 더했다 —
  임시 프로젝트 둘을 같은 작업 공간으로 A→B→A 세 번 띄운다. A 에서 Volume Profile 을 만들어
  최근에 올리고 하위 폴더로 옮긴 뒤, B 는 뿌리·이력 0/1·선택 없음·최근 0·B 의 폴더만이어야
  하고, A 의 저장 파일 바이트가 그대로여야 하며, 돌아온 A 는 최근 1 이다. B 에도 **같은 상대
  경로** `VolumeProfile/Keep.volume` 을 둬서 최근 항목이 상대 경로로만 묶이면 드러나게 했다.
  단정 135(전환 축 21), Debug·Release 통과. 변이 — 최근 항목 파일을 전역 한 곳에 두면 B 에서
  A 의 항목이 살아나 셋이 붉다(잡음). ★ 검사 자체 결함 하나를 변이 회차가 드러냈다 — 파일이 없을
  때 `if` 식이 `[byte[]]@()` 를 풀어 null 로 만들어 비교가 예외로 죽었다(문자열로 바꿨다).
- **모으기 비용 — 2026-09-17 닫힘.** 잘라 그리기 뒤 남은 3.25ms 는 **아무것도 안 바뀐 프레임마다**
  전체 순회·지원 확장자 판정·검색·정렬·게시 문자열 256 개를 새로 만드는 비용이었다. 목록 캐시에
  **세대 번호**(`browser_cache_generation`)를 두었다 — 폴더 목록의 내용이 바뀔 때만 오르고, 낡아서
  다시 훑었는데 내용이 같으면 목록 객체를 갈아 끼우지 않아 창이 기억한 항목 주소가 산다. 창은
  (세대·범위·폴더·검색어·유형·정렬)이 같으면 지난 결과를 쓴다. 전체 자산이 아직 차오르는 중이거나
  최근 항목(최대 32 개, 모으며 정리한다)이면 매번 모은다. Release 실측(창 2400x1400):

  | 범위 | 결과 | 전 p95 | 후 p95 |
  |---|---:|---:|---:|
  | 뿌리 폴더 | 22 → 24 | 0.085ms | 0.027ms |
  | 전체 자산 | 274 | 1.18ms | 0.051ms |
  | 전체 자산 | 4,274 | 3.25ms | 0.053ms |

  결과 수와 무관해졌다. 검사에 둘을 더했다 — ① 전체 자산이 다 찬 뒤 대기 200 프레임 동안
  `resultRebuilds`(스냅샷에 새로 낸 누계)가 거의 오르지 않는다(Debug·Release 모두 0 회). 결과
  기억을 끄는 변이에서 203 회로 붉다. ② 폴더에 선 채로 밖에서 파일이 생기면 결과에 나타난다 —
  재검증이 내용 변화를 보고도 세대를 안 올리는 변이에서 붉다. ★ ② 는 처음에 `wait 900` 하나로
  기다렸다가 Release 에서 붉었다(900 프레임이 재검증 나이 1 초에 못 미친다). 400 프레임씩 여섯 번
  표집한다 — Release 는 6 번 중 뒤의 4 번에서 보였다. 단정 140, Debug·Release 통과. 함께 돌린 것:
  `verify-browser-filesystem-contract`(idle probes 0)·`verify-browser-thumbnail-contract`(56) Release 통과.
  ★ `verify-editor-chrome-perf` 는 정점 5602 → 5622 로 붉다 — 이 변경은 그리는 양을 바꾸지 않는다.
  오늘 07:10 다른 작업이 추적 밖 자산 폴더(`BehaviorTree`·`Foliage` 등)를 만들어 뿌리 결과가 22 → 24
  가 됐고 기본 회차의 첫 줄 타일이 달라졌다. 골든은 갱신하지 않았다 — 추적 밖 자산에 기대는 골든이라
  그 폴더를 구워 넣으면 다른 기계에서 붉다.
- **1k/10k/50k 공동 측정 — 2026-09-17 닫힘.** 새 자를 세우지 않고 W7-0 의 `measure-panel-cost.ps1`
  에 `-WithAssets` 를 더했다 — 엔티티 N 개와 **같은 회차에** 추적 fixture `Tiny4.png` 를 N 개 복사해
  한 폴더에 올리고 네 구간을 잰다(대기 · 끝까지 스크롤 · 검색 넣고 빼기 40 회 · 전체 자산). 게이트가
  아니라 수치를 남기는 자다. **파일 수에 묶이지 않는 것은 섰다** — 그린 줄 5, 썸네일 요청 5(= 보이는
  타일), 끝까지 스크롤하면 정확히 5 늘어난다. ★ 그런데 대기의 p95 가 가리고 있던 것이 있었다. **max**
  가 1k 7.7ms · 10k 114ms · 50k **543ms** 였다 — 목록 캐시가 1 초마다 그 폴더를 다시 훑으며 항목을
  통째로 새로 만들고(5 만 개에 90ms) `path.filename()` 비교자로 정렬했다(비교마다 경로 둘을 만들어
  150ms). 결과 기억(바로 위)이 대기 p95 를 0.05ms 로 만들어 이 멈칫이 표에서 사라져 있었다. 고친 것:
  ① 다시 훑을 때 **지문부터** 본다(경로·종류·revision 을 문자열 할당 없이 접는다, 5 만 개 40ms) — 같으면
  목록을 만들지 않는다. 목록을 만드는 순회와 확인 순회가 같은 `fold_entry` 를 탄다. ② 다음 확인까지
  쉬는 시간을 훑는 비용의 100 배로 늘린다(최소 1 초) — 확인 비용을 1% 아래로 묶는다. ③ 정렬은 담아 둔
  `nameUtf8` 로 비교한다(150 → 8ms, 전체 자산 범위와 같은 키). ④ 폴더 트리 노드가 프레임마다 항목 수만큼
  `reserve` 하고 전부 돌았다 — 목록은 폴더가 먼저라 첫 파일에서 멈춘다. Release(창 2400x1400):

  | 파일 | 구간 | 전 p95 / max | 후 p95 / max |
  |---:|---|---:|---:|
  | 1,000 | 대기 | 0.042 / 7.72ms | 0.040 / 0.69ms |
  | 10,000 | 대기 | 0.046 / 114.4ms | 0.047 / 0.29ms |
  | 50,000 | 대기 | 0.051 / **542.8ms** | 0.052 / **0.41ms** |
  | 50,000 | 끝 스크롤 | 0.067 / 530.7ms | 0.051 / 0.23ms |
  | 50,000 | 검색 되풀이 | 6.27 / 497.6ms | 6.18 / 9.34ms |
  | 50,000 | 폴더 트리 | 1.48 / 7.48ms | 0.53 / 3.74ms |
  | 50,274 | 전체 자산 대기 | 0.060 / 0.19ms | 0.080 / 0.26ms |

  Hierarchy 는 세 크기 모두 units 14 · p95 0.06ms 아래(W7-3 그대로). 남는 비용은 검색어가 바뀌는
  프레임에 한 번 다시 모으는 것(5 만 개 6.2ms, 1k 0.96ms)이다 — 키 입력마다 한 프레임이라 두었다.
  변이 둘: 지문에서 revision 을 빼면 `verify-browser-thumbnail-contract` 가("원본을 다시 썼는데 무효화
  0 건"), 확인 없이 늘 같다고 보면 그것과 `verify-content-browser-navigation` 의 늦은 파일 표집(6 가운데
  0)이 붉다. 되돌린 뒤 Release 에서 탐색 140 · 파일시스템 계약 35 · 썸네일 계약 56 통과. ★ 측정 도구의
  첫 회차가 종료 코드 4 였다 — `editor.browser go` 를 첫 줄에 두면 브라우저가 한 번도 안 그려져
  `not_drawn` 으로 거절 응답이 난다(요청은 적용된다). 그때 요청 수가 7·14 로 흔들린 것도 이동 전 뿌리
  폴더 타일이 섞여서였다. 이동을 창 앞세우기 뒤로 옮겼다.
- **실제 마우스 분할선·배율 행렬(B1) — 2026-09-17 닫힘.** 9-12 기록은 "자동화 끌기에서 폭 변화를
  못 봤다" 로 남아 있었다. 숨긴 에디터 창에 `PostMessage(WM_MOUSEMOVE/LBUTTONDOWN/UP)` 를 보내 창
  프로시저 → `WinProcProxy` → 표시 스레드의 Win32 백엔드 → `DirectorySplitter` 를 지나게 했다
  (`editor.nav pointer` 는 ImGui 입력 상태를 직접 덮어 백엔드를 안 지난다). 먼저 관측을 세웠다 —
  `editor.browser` 의 `layout` 이 트리·분할선·본문 사각형, 선호(논리)·적용(px) 폭, 배율, **ImGui 가 본
  포인터**를 같은 프레임 값으로 낸다. 폭이 안 바뀔 때 메시지가 못 닿았는지 위젯이 못 받았는지를
  경계 통과로 가르려는 것이다. 그 자가 곧바로 두 함정을 드러냈다. ① **보내는 스레드의 DPI 인식** —
  DPI 를 모르는 PowerShell 이 (476,1224) 로 보낸 좌표를 ImGui 는 (714,1836), 모니터 배율 1.5 배로
  받았다. 9-12 의 실패가 이것이었을 가능성이 크다. 보내기 전에 스레드를 모니터별 인식으로 바꾼다.
  ② **숨긴 창의 `WM_MOUSELEAVE`** — 백엔드가 이동마다 `TrackMouseEvent` 를 걸고 커서가 창 밖이라
  곧바로 좌표가 -FLT_MAX 가 된다. 따로 보낸 누름은 허공을 눌렀다. 이동과 누름·뗌을 틈 없이 연달아
  보낸다. 검사 `verify-content-browser-splitter.ps1`(사용자 배율 1·1.25·1.5·2 × 이 기계 모니터
  150% = 실제 1.5·1.875·2.25·3.0, 배율마다 네 회차): 넓은 창 배치 불변식(트리 끝 = 분할선 시작,
  분할선 8u, 본문 = 분할선 끝 + 10u, 적용 폭 = clamp(선호×u, 140u, 가용−358u)), 창 크기 100 회 왕복
  뒤 배치 그대로, 가용 800u·600u·400u 에서 여유·본문 최소 340u 에 잘림·트리 접힘, 두 번 눌러 220 →
  방향키 두 번 236 → 40 논리 px 끌어 276 → 다시 띄워 276, 프로젝트 설정 파일은 그대로. 네 배율 모두
  같은 논리 값이 나왔다. 단정 272 통과. 변이 넷(끌기를 배율로 안 나눔 · 두 번 눌러 초기화 제거 ·
  본문 최소 폭 상한 제거 · 접힘 기준에서 배율을 뺌)이 모두 붉다. ★ 검사 자신의 함정 둘: 오른쪽
  도크는 창을 좁혀도 절대 폭을 지켜 고정 창 폭 목록이 잘림 구간(560u~658u)을 건너뛰었다 — 넓은
  창에서 도크 몫을 재어 창 폭을 고른다. 에디터는 `imguiScale: 1.0` 을 `1` 로 다시 써서 폭을 안
  건드렸는데도 바이트 비교가 붉었다 — 에디터가 쓰는 모양으로 적는다. 물리 모니터 사이 이동은 여전히
  하지 않았다.
- **끌기 실물 자극 (2026-09-17 닫힘).** 끌기 신원(payload = 전체 경로)은 소스 축뿐이었다. 분할선의
  메시지 경로로 타일을 눌러 Hierarchy 에 놓는다. 검사 `verify-content-browser-drag.ps1`: 유형 폴더
  (Textures) **밖**에 고유 이름 PNG 를 두고, 타일 사각형(`editor.browser` 의 `tiles`)과 Hierarchy 창
  사각형(`editor.dock` 의 `placements`)을 먼저 잰 뒤 두 번 끈다 — ① Hierarchy 빈 자리에 놓기: 끄는
  동안 브라우저가 든 payload(`dragPayloadType`·`dragPayloadPath`)가 `Texture`·`<폴더>/<이름>.png`,
  엔티티 +1, 그 SpriteRenderer 의 `m_SpritePath` 가 채워짐(텍스처가 실제로 올라왔을 때만 채워진다)
  ② 브라우저 본문 빈 곳에 놓기: 같은 payload 를 들었다가 엔티티 그대로. 단정 14 통과, 세 번 연속 같다.
  변이 셋(받는 쪽이 `Textures\` + 이름으로 다시 짓기 · 원천이 이름만 싣기 · Hierarchy 의 `Texture`
  수락 제거)이 모두 붉다. ★ 놓기는 **두 프레임**을 요구한다 — ImGui 는 직전 프레임에 대상이 payload 를
  받아들였고 이번 프레임에 뗐을 때만 전달한다. 이동과 뗌을 같이 보내면 한 프레임에 적용돼 넷 중 셋이
  전달되지 않았다. `이동 → 가운데 누름 → 가운데 뗌 → 이동 → 왼쪽 뗌` 을 한 번에 보내 입력 흘림이
  대상 위 좌표를 두 프레임 이상 살린 뒤 왼쪽 뗌을 넣게 한다(8/8). 받는 자리는 Hierarchy 하나만
  실물로 밟았다 — 나머지 16 곳은 소스 축 그대로다.
- **텍스처 캐시의 stem 신원 (2026-09-17 닫힘).** 엔진 쪽 소유는 `ModelGeometryTextureImprovementPlan` G2 다 —
  경과와 검사는 그 문서 §G2. 끌기 검사가 적는 스프라이트 경로도 이름에서 `<폴더>/<이름>.png` 로 바뀌었다.
- **남은 것.** Volume Profile 취소 — 렌더 계층 개편이 끝나면 구조가 다시 바뀌므로 그때 본다(2026-09-17 보류).

#### 스크린샷과 착수 전 코드 대조

기준 소스는 `Editor/EngineGUIWindow/ContentsBrowserWindow.{cpp,h}`다. S&Box 기능의 세부 동작이나
열지 않은 New/필터 메뉴 내용은 스크린샷만으로 확정하지 않는다. 아래는 보이는 표면과 CreatorEngine 호출 경로의 비교다.

| 표면 | 현재 확인한 구현 | W2-B에서 처리할 내용 |
|---|---|---|
| 디렉토리 영역 | `BeginChild("DirectoryHierarchy", ImVec2(200, 0), false)`로 고정. 폭 저장값·resize flag·splitter 없음 | 내부 드래그 분할선, 배율 반영, 폭 저장/복원, 좁을 때 폴더 패널 접기 |
| 폴더 이름/루트 | 긴 이름은 child 경계에서 잘리고, 루트 Assets는 펼침만 처리하며 현재 폴더로 이동하지 않음 | 말줄임/전체 이름·경로 tooltip, 루트 선택, 탐색과 펼침의 입력 구분 |
| 경로 바·뒤로/앞으로·상위 이동 | `m_currentDirectory`에 직접 대입하며 breadcrumb·history·Up 동작 없음 | 공통 탐색 함수와 위치/이력 상태, 경로 바와 탐색 버튼 |
| 오른쪽 자산 영역 | `is_regular_file()`과 지원 확장자만 표시. 하위 폴더는 표시하지 않음 | 폴더와 자산을 함께 표시하고 폴더 더블클릭/Enter 탐색. 파일 없이 하위 폴더만 있는 위치도 도달 가능 |
| 검색 | `ImGuiTextFilter`는 존재. 검색 아이콘은 비활성 장식 버튼이고 입력 폭은 `avail - 90`, 현재 폴더 파일명만 필터 | 실제 입력 영역/최소 폭 확보, 지우기, 검색 범위 표시, 유형 필터·정렬 |
| New | 상단 버튼 없음. VolumeProfile 폴더 빈 영역 우클릭에 생성 동작은 있음 | New와 폴더 우클릭이 같은 생성 명령을 소비, 생성 대상 폴더/결과 선택 일치 |
| 생성 메뉴 조건 | 폴더/타일 팝업 두 곳의 `empty() && equivalent(...)` 조건은 정상 비어 있지 않은 경로에서 생성 메뉴를 막음 | 빈 경로·허용 위치·쓰기 가능 여부를 공통 술어로 판정. 경로 오류를 예외 없이 표시 |
| Recents / Everything | 해당 가상 위치와 방문 이력 상태 없음 | 최근 사용 자산 / 프로젝트 전체 자산을 명시적인 탐색 범위로 제공 |
| 표시 도구 | 오른쪽 목록은 항상 타일이며 타일 본문은 160 logical px. `ContentsBrowserStyle` 분기는 현재 본문에서 제거됨 | 브라우저 내부 타일/목록 전환·타일 크기·보기 옵션. 외부 dock tree와 독립 |
| 썸네일 | ~~현재 유형 아이콘 사용~~ → **2026-09-16 W7-6 으로 비동기 썸네일 착지.** 타일이 `thumbnail_acquire` 로 묻고 준비 전에는 유형 아이콘으로 버틴다 | **닫힘.** 모델 렌더 썸네일만 W7 쪽 잔여다 |

오른쪽이 빈 화면인 원인을 하나로 단정하지 않는다. 현재 루트에 지원되는 직접 자식 파일이 없으면
하위 폴더가 있어도 빈 화면이 될 수 있다. 검색 결과 0, 실제 빈 폴더, 미지원 파일만 존재,
읽기 실패는 각각 구분해 표시한다. 스크린샷의 클라우드 등 추가 아이콘은 기능 계약이 확인되지 않아
온라인 자산 서비스 연동으로 확대하지 않으며, 프로젝트 로컬 자산 탐색을 이번 범위로 고정한다.

#### 배치·탐색 계약

1. **내부 분할과 창 docking은 별개다.** 왼쪽 폴더 트리와 오른쪽 본문 사이에 보이는 드래그
   분할선을 둔다. 기본 폭은 W1 logical 치수로 정하고, 트리와 본문의 최소 조작 폭을 실측해 제한한다.
   hover 때 resize 커서를 표시하고 드래그·키보드 조정·기본 폭 복원을 제공한다. 사용자가 정한 선호 폭과
   현재 창에 맞춘 제한 폭을 분리해 작은 창을 거친 뒤에도 선호 폭을 복원한다. 폭은 logical 값으로 저장한다.
   두 영역의 최소 폭을 담지 못하면 트리를 접고 폴더 버튼/팝업으로 접근하게 한다. 자산 본문을 0폭으로 밀지 않는다.
   긴 폴더 이름은 명시적으로 말줄임하고 전체 이름/경로를 tooltip과 경로 바에서 확인할 수 있게 한다.
2. **상단 도구도 가용 폭을 나눈다.** 오른쪽 본문 위에 `New`, 뒤로/앞으로/이력, 상위 이동,
   경로 바, 검색·유형 필터·표시 옵션을 배치한다. 충분히 넓으면 한 줄, 좁으면 탐색/검색 두 줄로 전환하고
   부가 옵션은 더보기로 옮긴다. 행 높이·간격·버튼 중심을 맞추며 `avail - 90` 같은 보정은 제거한다.
   현재 폴더명과 검색 입력의 최소 가독성을 확보하고 경로의 중간 구간은 조상 메뉴로 접는다.
   New·이동·검색을 모두 사라지게 만드는 압축은 허용하지 않는다. 브라우저는 세로 스크롤을 사용하므로
   씬뷰 W2-V의 한 줄 고정 정책을 그대로 강제하지 않는다.
3. **모든 폴더 이동은 하나의 탐색 경로를 사용한다.** 트리 클릭·루트·폴더 타일/목록·breadcrumb·
   경로 입력·Up·뒤로/앞으로가 같은 상태 전이를 호출한다. 폴더 펼침만 바꾸면 탐색 이력이 늘지 않는다.
   경로 바는 클릭 가능한 프로젝트/Assets/하위 경로를 표시하고 직접 경로 입력·복사를 지원한다.
   실제 탐색 루트는 `PathFinder`가 제공하는 프로젝트 자산 루트이며 상위 이동은 그 경계에서 멈춘다.
   임의 절대 경로를 두 번째 자산 루트로 등록하지 않는다. 존재하지 않는 경로 입력은 현재 위치를 유지하고 오류를 표시한다.
4. **이력·검색·선택의 의미를 고정한다.** 뒤로/앞으로와 이력 메뉴는 유효한 이동만 기록하고
   같은 위치 중복과 검색 타이핑 한 글자마다의 이력 생성을 억제한다. 뒤로 간 뒤 새로 이동하면 앞으로 이력을 버린다.
   정상 폴더 이동은 검색어를 비워 내용이 숨지 않게 하고, 뒤로/앞으로는 해당 방문의 검색어·스크롤·선택을 복원한다.
   유형/정렬/보기 설정은 유지한다. 삭제된 위치는 현재 프로젝트 루트 안의 유효한 상위 위치로 복구하며
   접근 실패·없는 이력은 상태를 알린다. 트리에서 현재 폴더의 조상을 펼쳐 같은 위치를 보여 준다.
5. **최근 항목과 전체 자산은 검색 범위다.** 기본은 현재 폴더, `전체 자산`은 현재 프로젝트 루트 아래,
   `최근 항목`은 실제 열기/편집에 성공한 자산을 중복 없이 최근 순으로 보여 준다. 단순 hover로 최근 목록을 채우지 않는다.
   범위·검색어·유형 필터를 화면에 표시하고 지우기를 제공한다. 전체/최근 결과에는 원래 폴더와
   해당 위치 열기를 제공한다. 가상 위치에서는 New를 비활성화하고 생성할 실제 폴더를 선택하도록 안내한다.
   초기 정렬은 폴더 우선·이름순이며 이름/유형/수정일 정렬과 타일/목록 전환·타일 크기를 제공한다.
   전체 검색·정렬·필터는 W7의 같은 목록 스냅샷에서 적용하고, 그 결과에 clipping을 적용한다.
6. **신원과 표시 상태를 분리한다.** 자산은 GUID, 폴더는 프로젝트 신원 + 정규화한 루트 상대 경로로
   선택·펼침·이력을 식별한다. 같은 파일명, 아이콘/썸네일 교체, 타일/목록 전환이 ID를 바꾸지 않아야 한다.
   이름·경로는 한글·공백·긴 Unicode 경로를 보존한다. 폴더 이동은 이전 목록의 자산 선택을 정리하고
   Inspector와 일치시킨다. 뒤로 복원하는 자산은 현재도 존재할 때만 선택한다.
   W3의 버전 있는 workspace 저장 경계에 선호 폭·접힘·보기 설정·마지막 위치를 연결하고,
   최근 항목은 프로젝트별 제한된 목록으로 저장한다. 앞뒤 탐색 이력은 세션 상태로 둔다.
   프로젝트 변경 때 정적 `DataDirectory`와 이전 프로젝트의 이력/선택을 재사용하지 않는다.

#### New와 기존 자산 동작 연결

- **최소 생성 범위는 새 폴더와 기존 Volume Profile 생성이다.** 머티리얼·스크립트·씬 등은
  B0에서 실제 생성 진입점·허용 위치·저장/GUID 계약을 계수하고, 재사용 가능 여부와 별도 확장이 필요한 항목을
  명시한다. 스크린샷에 New가 있다는 이유로 지원되지 않는 생성기를 완료 범위에 포함하지 않는다.
- New와 폴더 우클릭은 부록 A의 같은 명령/활성 조건을 소비한다. 대상은 마지막 우클릭한 폴더가 아니라
  각 버튼/메뉴가 명시적으로 전달한 폴더다. 팝업을 연 뒤 탐색해도 생성 요청의 대상이 바뀌지 않게 하고,
  표시한 대상·실제 저장 경로·완료 결과의 경로가 일치해야 한다. 현재 Volume Profile은 저장 대화상자 경로와
  전달한 directory가 따로 쓰이므로 이를 함께 정리한다.
- 파일/폴더 쓰기는 `EditorAssetDatabase`와 기존 authoring/command 소유 경로를 사용하고 UI에서
  별도 파일 생성기를 만들지 않는다. 새 폴더의 쓰기 진입점이 필요하면 같은 소유 경계에 추가한다.
  유효한 이름·충돌·쓰기 실패·취소를 처리하고 덮어쓰지 않는다. 파일 생성은 GUID/sidecar·catalog 반영까지
  성공해야 완료로 게시한다. 인식 전이나 실패한 항목을 성공한 선택으로 표시하지 않는다.
- 생성 완료 시 대상 폴더의 목록을 갱신하고 새 항목을 선택/표시한다. 요청 중 사용자가 다른 폴더나
  프로젝트로 이동했다면 늦은 완료가 현재 위치/선택을 강제로 바꾸지 않는다.
  기존 자산 Inspector 선택·파일 열기·경로 복사·삭제 확인·씬/프리팹 drag-drop은 유지한다.
  현재 파일명만 담는 일부 drag payload는 전체/최근 목록의 동명 자산을 구분할 수 없으므로 소비자까지
  기존 GUID/경로 계약을 대조해 교정한다. 새 목록 표면에서만 신원을 바꾸어 소비자를 남겨 두지 않는다.

#### 이관 순서와 초기 공수

| 단계 | 작업 | 초기 추정 | 완료 기준 | 2026-09-13 상태 |
|---|---|---:|---|---|
| W2-B0 | 화면/상태 계약·생성/drag 소비자 계수 | 0.5일 | 기존 기능·신규 범위·W3/W7 경계와 지원 생성 종류 확정 | progress — 화면 계약 적용. **생성/끌기 소비자 전수는 2026-09-17 닫힘**(받는 자리 17 · 생성 경로 2) |
| W2-B1 | 내부 분할선·긴 이름·좁은 창 대응 | 1일 | 폭 조절/복원·접기·tooltip 도달 가능, DPI/resize에서 본문 최소 폭 유지 | progress — 분할/저장/접기 적용. **폭 저장의 W3 이관은 2026-09-16 닫힘.** **실제 마우스·배율 행렬은 2026-09-17 닫힘** — 운영체제 메시지로 끌기·두 번 눌러 초기화, 방향키, 100 회 크기 왕복, 본문 최소 폭·접힘을 실제 배율 1.5~3.0 에서 `verify-content-browser-splitter.ps1`(272 단정·변이 4)이 잰다. tooltip 도달은 재지 않았다 |
| W2-B2 | 경로 바·루트/Up·뒤로/앞으로·이력 | 1.5일 | 모든 폴더 진입점 통합, 삭제/실패 복구·프로젝트 경계·선택 복원 일치 | progress — 경로/뒤·앞/상위 적용. **방문별 검색·선택·스크롤 복원과 사라진 폴더 복구는 2026-09-17 닫힘. 프로젝트 전환(A→B→A 재기동) 실행 검증도 같은 날 닫힘** |
| W2-B3 | 폴더+자산 목록·검색 범위·최근/전체·표시 옵션 | 1.5일 | 유형/정렬/목록·타일이 같은 결과/ID를 소비하고 W7 스냅샷과 연결 | progress — 현재 폴더 검색/유형/정렬/보기 적용. **W7 스냅샷 연결은 2026-09-16 닫힘**(§W7-1·§W7-5). **최근/전체 가상 위치·잘라 그리기·결과 기억(4k p95 3.25 → 0.053ms)은 2026-09-17 닫힘.** 1k/10k/50k 공동 측정도 닫힘 — 5 만 개 폴더의 1 초마다 0.5 초 멈칫(max 543 → 0.41ms)을 지문 확인으로 고쳤다 |
| W2-B4 | New·폴더 우클릭 공통 생성 및 결과 반영 | 1.5일 | 새 폴더·Volume Profile 생성/취소/실패, 경로·GUID·선택 일치와 늦은 완료 처리 | progress — 새 폴더 적용. **Volume Profile 이름 대화상자·충돌/실패 이유·선택은 2026-09-17 닫힘**(생성이 동기라 늦은 완료 없음). 취소는 사람 손 확인만 남음 |
| W2-B5 | 배치·탐색·생성·검색·기존 자산 동작 회귀 | 1일 | 아래 행렬·관측·W8 연결 완료, 새 목록의 동명 자산 drag 소비자까지 검증 | progress — 대표 UI/fixture 검증. **`verify-content-browser-navigation.ps1` 114 단정·변이 14 종(2026-09-17), 끌기 신원은 소스 축.** 실제 마우스·배율 행렬은 `verify-content-browser-splitter.ps1` 로 닫힘(2026-09-17). **끌기 실물 자극은 `verify-content-browser-drag.ps1`(14 단정·변이 3)로 닫힘(2026-09-17)** — Hierarchy 받는 자리 하나를 실물로, 나머지 16 곳은 소스 축 |

W1/W2와 이미 착지한 M1 메뉴 배선을 선행으로 한다. W2-I·W2-V 완료와 순차 의존은 없다.
W7의 폴더/자산 스냅샷 계약을 먼저 공유해 B3가 같은 정본을 소비하게 하고, 검색을 위해 매 프레임
재귀 파일 순회나 별도 자산 DB를 만들지 않는다. 파일 감지·목록 cache·clipping·썸네일 생산 비용은
기존 W7 범위이며 W2-B에는 탐색/검색 상태·조건·표시와 소비자 연결을 산정한다.
W3 저장/복원 연결과 W7 통합 결과는 W8에서 함께 판정한다. 추가 생성 종류와 W7 썸네일의
미산정 공수는 위 7일에 포함되지 않으며 B0에서 확장할 경우 계획/대시보드를 함께 갱신한다.

#### 검증과 완료 판정

- content 폭 320/480/720/1024/1440 logical px와 낮은 높이 180/320, OS DPI/사용자 배율
  100/125/150/200%를 구분해 검사한다. 분할선 drag·키보드 조정·폭 복원·패널 접기·100회 이상 resize,
  dock/float·workspace 재로드에서 버튼/검색/경로 도달성과 저장한 선호 폭을 확인한다.
- 긴 한글 이름·깊은 경로·동명 파일, 폴더만 있는 루트, 빈 폴더, 검색 결과 0, 미지원 파일,
  접근 거부·탐색 중 삭제/rename·프로젝트 변경을 포함한다. 경로 바·트리 선택·오른쪽 목록의 위치가 달라지면 실패다.
- 트리/타일/목록/경로/Up의 모든 진입점, A→B→뒤로→C 뒤 앞으로 이력 제거, 무효 경로 유지,
  검색 중 뒤로 복원, 전체/최근 결과의 원래 위치 열기, 삭제된 최근 항목 정리를 검증한다.
- New/우클릭의 대상 일치, 생성 성공·충돌·권한 실패·취소, 요청 뒤 다른 폴더/프로젝트 이동,
  파일 GUID/sidecar와 Inspector 선택을 검사한다. 빈 메뉴·잘못된 위치의 생성·실패를 성공처럼 표시하면 실패다.
- 검색·정렬·타일/목록·타일 크기·썸네일 교체 전후 결과 신원과 선택/drag payload를 비교한다.
  1k/10k/50k 목록의 비용·clipping·가시 썸네일 요청 검증은 W7과 공동 fixture로 수행하고 DX12/Vulkan은 W8에 포함한다.
- 같은 프레임의 트리/분할선/본문/도구 rect, 선호/적용 폭, 현재 위치·검색 범위·이력 버튼 상태,
  결과 신원·선택·생성 완료 상태를 관측한다. 고정 폭·이력 분기 누락·생성 대상 혼동·동명 payload를
  각각 변이로 주입해 실패를 확인한다. 기존 창/dock 관측이나 유형 아이콘 완료만으로 이 항목을 통과 처리하지 않는다.

### W3 — stable ID · 자유 docking · workspace 저장/복구 (P0, 3일)

**현재 `todo`: M4의 창 선언·표시 이름 분리·Window 메뉴와 선언 기반 기본 배치는 선행 완료다.**
`###Editor.*` ID 이주, 자유 도킹 및 versioned workspace/legacy 복구는 남아 있다.
아래 Tile 분기·창 등록 설명은 착수 전 이력이며 이미 제거한 분기를 다시 구현하지 않는다.

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

**(2026-09-11 소유권 정리 S1~S3 착지.)** ID 이주에 앞서 창의 소유 구조를 먼저 세웠다. 계획서에 없던 선행이고, 넣은 이유는 §1.3의 "표시 상태 저장소가 둘" 을 하나로 합치려면 그 상태를 **누가 드는지**가 먼저 정해져야 하기 때문이다.

- **S1 — 창 클래스 여덟을 자유 함수로.** 여덟 개를 열어 보니 소유하는 자원이 **0** 이다 (멤버는 전부 UI 지역 상태이거나 빌린 포인터다). `EditorMain` 이 들고 있던 창 멤버 여덟이 사라졌고, 상태는 각 TU 의 지역 접근자 하나가 든다. `EditorViewportWindows.cpp` 는 매크로 둘만 남아 삭제했다. Inspector 생성자의 typed Draw 등록은 **부팅의 일**이라 부팅으로 올렸다 — 소유자가 사라지면 그 등록이 "인스펙터를 처음 열 때" 로 늦어지고, 그 표를 읽는 것은 인스펙터만이 아니다(애니메이터 창·메시 렌더러 헬퍼가 같은 `Meta::TypedDraw` 를 읽는다).
- **S2 — 표를 주입으로.** `window_table` 을 세우고 렌더러가 그것을 참조로 받는다. 자가 검사가 제품 표를 `swap` 으로 치우던 자리가 없어졌다 — 그 치우기는 CLI 가 도는 게임 스레드였고 표를 순회하는 `draw_windows` 는 PresentationThread 였다. 잠금이 없었으므로 순회 도중 버퍼가 바뀔 수 있는 자리였다. 막은 것이 아니라 없앴다.
- **S3 — 본문 바인딩을 RAII 핸들로.** `MenuBarWindow` 가 본문 **열 개**를 걸고 푸는 자리가 하나도 없었다(소멸자가 `= default`, 본문은 모두 `this` 캡처). `bind_window_body` 가 `[[nodiscard]]` 핸들을 돌려주도록 바꿔 컴파일러가 강제한다. 본문 보관소도 주입 가능해졌다. 강제력은 변이가 검증했다 — 처음에는 핸들을 버려도 빌드가 exit 0 이었다(이 프로젝트는 `TurnOffAllWarnings` 라 C4834 가 나오지 않는다). `/we4834` 로 그 한 번호만 오류로 올렸다.

**(2026-09-13 W3 착지.)** stable ID 이주·자유 도킹·versioned workspace 저장/복구·legacy ini
이주가 전부 섰다. 파일 계층은 `EditorWorkspaceFile`(이주·검증·원자 쓰기·백업), 세션 계층은
`EditorWorkspaceStore`(적재·적용·주기 저장·Reset·복구)이고, 밖에서 보는 문은 `editor.workspace`
명령과 Window 메뉴의 Save/Reload/Reset 셋이다. 게이트는 둘로 나눴다 — 형식만 보는
`verify-editor-workspace-storage.ps1`(169 checks, 실물 ini 6벌)과 살아 있는 에디터를 열여섯 번
띄우는 `verify-editor-workspace.ps1`(212 checks). 둘 다 run-all 에 넣었다.
W4 가 두 수를 153→169·181→212 로 올렸다 — 늘어난 몫이 §W4 의 W4-①②③ 구역이다.

착지하면서 **다섯 가지가 실제로 틀려 있었고, 그 다섯은 전부 새 게이트가 붉어져 드러났다.**
계획서에는 하나도 적혀 있지 않았다.

1. **`###` 표식을 떼고 선언 표와 맞대고 있었다.** `stable_part` 가 `rfind("###")+3` 을 돌려주는데
   W3 이 안정 ID 자체를 `###Editor.*` 로 옮겨 표가 든 값에 그 세 글자가 들어갔다. 이주 직후
   `ghost=6` — 그려지고 있는 창 여섯이 전부 유령으로 보고됐다.
2. **ImGui 는 `Begin` 이 받은 이름을 저장하지 않는다.** `CreateNewWindowSettings` 가 `###` 를
   건너뛰고 **표식까지 버린 뒤**의 글자만 든다. 그래서 에디터가 스스로 쓴 파일은 창을
   `Editor.Scene` 으로 적는데 검증기는 `[Window][###Editor.` 를 요구했다. 결과는 조용했다 —
   **깨끗한 첫 실행에서 배치가 한 번도 저장되지 않았다.** 이주 표를 legacy 목록이 아니라
   **살아 있는 선언 표**에서 함께 유도하게 고쳤다(W3 뒤에 늘어난 창도 덮는다).
3. **검증기가 도크 뿌리를 하나로 못 박고 `DockSpace` 철자를 요구했다.** 빌더가 막 세운 노드는
   다음 프레임에 `DockSpace()` 가 다시 표시하기 전까지 평범한 `DockNode` 로 적히고, **자유
   도킹은 패널을 떼어 낼 때마다 뿌리를 하나 더 만든다.** 둘 다 정당한 파일을 거부하던 규칙이라
   "뿌리 ≥ 1 · 주 도크스페이스 ≤ 1" 로 바꿨다. 손상본 둘은 여전히 거부된다.
4. **창 표가 렌더러보다 늦게 서 있었다.** `register_editor_windows()` 가
   `make_unique<EditorRenderer>` **뒤**였고, 스토어는 생성자에서 그 표를 읽는다. 그래서 저장된
   패널 상태를 되돌리는 순회가 **빈 표**를 돌았다 — 파일에는 `panel "###Editor.Hierarchy" 0`
   이 적혀 있는데 아무도 읽지 않아 닫아 둔 패널이 재시작마다 다시 열렸다. 등록을 앞으로
   옮기고, 스토어 생성자가 빈 표를 받으면 던지게 했다(조용히 잃는 것보다 낫다).
5. **W3 ID 이주가 두 자리를 빠뜨렸다.** `EditorAssetPresentation` 이 옛 표시 이름
   `"TextureType Selector"`·`"SelectMaterial"` 을 자기 상수로 들고 있어 본문 둘은 고아,
   선언된 창 둘은 본문 없음이었고 `open_window`/`close_window` 는 아무것도 못 찾은 채 돌았다.
   `verify-editor-declaration-wiring.ps1` 이 이것을 들고 있었다.

**이빨은 변이로 쟀다.** alias 표를 통째로 비우면 이주 자체가 거부돼 붉고, **alias 하나만**
빼면 `ghost`·`undocked`·`duplicate` 셋이 전부 통과했다 — 살아남은 옛 이름이 살아 있는 도크
탭이 아니라 아무도 만들지 않는 ini 항목으로 남기 때문이다. 그 하나를 잡는 것은
`orphanEntries` 뿐이어서 게이트에 그 단정을 더했다. `layout_audit::clean()` 에는 넣지 않았다 —
개발자 기계에는 지난 빌드의 창 이름이 정당하게 남아 있을 수 있어, 격리된 시나리오 폴더에서만
0 을 요구한다.

게이트가 개발자의 `Saved/Config/imgui.ini` 를 빌려 쓰던 것도 여기서 끝냈다. 스토어가
`CREATOR_EDITOR_WORKSPACE_DIR`·`CREATOR_EDITOR_LEGACY_INI` 를 읽으므로 시나리오마다 빈 폴더를
준다. 이전 판은 개발자 파일을 백업했다 되돌리는 식이어서 게이트가 중간에 죽으면 손상본이
개발자 자리에 남았다.

**판정 — 모두 게이트로 선다:** title/icon을 바꿔도 dock 위치가 유지되고(저장 파일이 라벨을
버리고 안정 ID만 든다), save→restart→load가 **바이트까지 동일**하며, 닫아 둔 패널이 재시작을
넘고, Reset 은 되돌리기 전에 백업을 남기고, 손상된 layout은 사용자 파일을 잃지 않고 기본
preset으로 복구된다(legacy ini 는 `.pre-workspace-v1`, v1 파일은 `.rejected` 로 남는다).
migration canary는 §1.4의 실물 ini 4벌 — Content Browser 이중 entry가 든 것 포함 — 이고 넷 다
`undocked=0 · ghost=0 · duplicate=0 · orphan=0` 으로 통과한다.

**W3 이 남긴 것 둘.** Scene 의 `no_move` 는 걷지 않았다 — 중앙 뷰포트라 W4 의 단일 Host 가
그 자리를 가져간다. `dock_slot` 열거자를 workspace 선언에서 받는 일도 W6 의 preset 과 함께
간다(W3 은 열거자를 그대로 쓴다).

### W4 — 중앙 ViewportHost · canvas 분리 · view demand (P0, 4일) — `done`

**2026-09-13 `done`.** 가운데는 이제 **닫을 수 없는 창 하나**이고 Scene·Game 은 그 Host 의 표시
모드다. canvas 규약도 하나가 됐다. 판정 둘은 `verify-editor-workspace.ps1` 의 W4-①②③ 구역이
살아 있는 에디터 네 번으로 센다(아래 *착지와 결함*). extent 기반 resize 는 이 계획서가 두
backend 의 generation/retire 검증 뒤로 미뤄 둔 항목이라 여기 포함하지 않는다.

- central non-closable Host를 만든다.
- **canvas 규약을 하나로 정한다(§1.5).** letterbox냐 stretch냐, rect 원점이 창이냐 content냐를
  먼저 확정하고 ImGuizmo·picking이 같은 출처를 읽게 한다. 지금은 Scene이 창 전체 + titleBar
  수동 보정, Game이 content + letterbox로 갈려 있다.
  content/image/clip rect를 같은 프레임의 화면 좌표로 생산하고 extent는 `max - min`으로 구한다.
  picking·모델 배치의 `imageMax` 크기 인자 오용을 함께 교정한다. 이 정본과 기존 소비자 이관을
  W4 초기에 제공해 W2-V가 소비하게 하며, 오버레이 배치용 canvas 좌표식을 별도로 만들지 않는다.
- Scene image, scene interaction, overlay, game input surface를 분리한다.
  툴바·방향 기즈모·HUD의 정렬/overflow와 씬 내부 입력 제외는 W2-V가 담당한다.
- 기존 Editor/Game presentation key를 mode별로 소비한다. Game의 `active`/`ready` 2단 신호는
  이미 있으므로 Scene 쪽과 통일해 재사용한다.
- Scene/Game 두 곳의 `BringWindowToDisplayBack` 강제 호출을 제거한다.
- optional Game Preview와 visible target demand를 연결한다. **현재 UI 쪽에서 view demand를
  제어하는 경로는 없다** — 표시 타깃 선택은 전부 렌더러 내부에서 결정되므로 2단계는 전량 신규다.
- extent 기반 resize는 두 backend의 generation/retire 검증 뒤에만 활성화한다.

**판정:** central Host를 닫거나 ToolPanel로 대체할 수 없고, Game Preview가 닫힌 single-view 상태에서
불필요 target 생산 여부가 계측된다.

#### W4 착지와 결함 (2026-09-13)

**구조.** `ViewportHostWindow.{h,cpp}` 가 Host 본문·모드·view demand 를 들고,
`EditorViewportCanvas.h` 가 좌표 규약을 든다(`SceneViewportImage.h` 는 지웠다). 선언은
`Windows/EditorViewportWindows.h` 에 `central<&draw_viewport_host>` 하나와 기본 닫힘
`panel<&draw_game_preview>` 하나다. 모드 막대는 Host 본문이 `TabStyleScope` 아래
`BeginTabBar` 로 그린다 — 창을 둘에서 하나로 합치는 것이 화면의 모양까지 바꿀 이유는 없어서
가운데 노드의 탭 바(`ImGuiDockNodeFlags_NoTabBar` 로 끈 것)와 같은 스타일을 쓴다. 관측은
`editor.viewport [scene|game]` 이다.

**찾아 고친 결함 다섯.**

1. **가운데 노드가 central 이 아니었다.** `DockBuilderSplitNode` 가 남긴 중앙을 아무도
   `ImGuiDockNodeFlags_CentralNode` 로 표시하지 않아 실측 `centralNodes=0` 이었다. 그래서
   `DockBuilderGetCentralNode` 가 널이고, **중앙을 특별히 다루는 것들이 전부 조용히 닿지
   않았다** — 탭 바 억제와 `PassthruCentralNode` 의 투명 배경이 그렇다. W3 게이트가 이 수를
   `-le 1` 로만 봐서 0 을 통과시키고 있었다. 이제 `-eq 1` 이다.
2. **dock 감사가 §1.4 의 사고를 구조적으로 볼 수 없었다.** `EditorChromeProbe` 가 "도크
   빌더가 건너뛰는 조건과 같은 조건을 쓴다" 고 적어 놓고 실제로는 `window_role::panel`
   **전체**를 면제했다. 빌더는 패널을 도크한다(Hierarchy 는 `right_upper` 다). 면제한 탓에
   `undocked` 는 중앙 창만 셌고, 그리는데 자리를 잃은 패널 — 바로 그 감사를 만든 이유 —
   은 영원히 0 으로 보고됐다. 조건을 `floating` 하나로 줄였더니 `dockedWindows` 가 2→5 가 됐다.
3. **Game 이 창의 종횡비로 letterbox 하고 있었다.** `ScreenResizeBus::GetAspectRatio()` 는
   창 비율이지 게임 타깃 프레임버퍼의 비율이 아니다. 둘이 갈리면 비율을 "지켜" 맞춰도 그림이
   늘어난다. 캔버스 규약 ②대로 소스 픽셀에서 낸다.
4. **모드가 스레드 경계를 넘어 평문으로 읽혔다.** 모드를 쓰는 쪽은 UI 스레드(모드 막대)인데
   `editor.viewport` 는 게임 스레드에서 돈다. 게시본(`viewport_demand::mode`)과 요청함을 둘 다
   `demandMutex` 아래로 넣었다 — workspace 상태를 가른 것과 같은 이유다.
5. **옛 배치를 물리면 central 이 영원히 없었다.** ①의 표시는 도크 빌더 안에 있었는데
   빌더는 **ini 가 없을 때만** 돈다(§W0). `CentralNode` 는 ini 에 저장되는 local flag 라
   W4 전에 쓰인 파일에는 그 비트가 없고, 이주는 없는 비트를 지어낼 수 없다. 그래서 W4 이전
   워크스페이스나 legacy ini 를 물린 세션은 중앙 노드가 없는 채로 돌았다 — ① 과 같은 증상이
   같은 이유로 되살아나는 자리다. 표시를 매 프레임 `BeginRender` 로 옮기고, central 이 없으면
   Host 가 들어앉은 노드를 central 로 세운다(그 창은 `no_move` 라 자기 노드를 떠나지 않는다).
   **이것을 잡은 것은 게이트의 legacy fixture 넷이다** — 본 배치만 보던 단정은 초록이었다.
   `dock_audit::clean()` 도 "둘 이상이 아니다" 에서 "정확히 하나다" 로 조였다.

**거둔 것과 거두지 않은 것.** 게임 타깃은 Scene 모드이고 Game Preview 가 닫혀 있으면 만들지
않는다(실측: 61 프레임 중 60 프레임 미제출, Preview 를 열면 `gameTarget=true`). **에디터 타깃은
대칭으로 끄지 않았다** — Game 모드에서 씬 뷰가 보이지 않는 것은 맞지만 그 타깃을 프레임마다
세웠다 무너뜨리는 것은 생성·회수를 두 backend 에서 검증한 뒤에야 안전하고, 그것은 이 계획서가
extent 기반 resize 를 미뤄 둔 것과 같은 이유다. 지금 거두는 것은 비싼 쪽 하나 — 게임 카메라의
전체 씬이다. 게이트가 `editorTarget=true`·`suppressedEditorViews=0` 을 단정하므로 나중에
대칭으로 거두면 그 줄이 붉어지고, 그때 결정이 바뀐 것을 적으면 된다.

**이빨은 변이 셋으로 쟀다.** 결함을 하나씩만 심고 에디터를 통째로 다시 빌드해 태웠다.
셋 다 **맞는 자리에서 맞는 이유로** 붉었고, 앞에 선 단정이 뒤를 가리지 않았다.

| 심은 결함 | 붉어진 자리 |
|---|---|
| `close` 가 `central`·`closable` 검사를 건너뛴다 | W4-① — "Closing the central viewport host was accepted; it must be refused" |
| 뷰 수요를 무시하고 게임 뷰를 늘 만든다 | W4-② — "The game view was sealed on every frame … (suppressed=0)" |
| `Save` 가 모드를 언제나 Scene 으로 적는다 | W4-③ — "The workspace file did not record the game mode" |

**변이가 단정 하나의 뜻을 조이게 했다.** `suppressedGameViews` 는 원래 "게임 뷰를 만들지
않은 프레임" 을 셌는데, 그 안에는 **만들 수 없었던 것**(주 카메라가 없음)이 섞여 있었다.
지금 게이트 시나리오에는 게임 카메라가 있어서 변이가 잡혔지만, 카메라 없는 씬을 물리는
순간 수요 문을 통째로 걷어도 그 수가 그대로여서 단정이 아무것도 재지 않게 된다 — 게이트가
씬이 무엇을 담고 있느냐에 기대는 모양이다. 카메라를 먼저 집어 "만들 수 있었는데 수요가
없어 만들지 않았다" 만 세도록 바꿨다(`App.cpp` · `note_view_submission`). 조인 뒤에도
실측은 그대로다 — Scene 모드 46 프레임 중 45 미제출.

**못 잡는 것 하나를 적어 둔다.** ①의 central 표시를 `BeginRender` 에서 걷는 변이는 **본
배치 시나리오에서 초록이다** — 도크 빌더가 여전히 세우기 때문이다. 그것을 잡는 것은 legacy
fixture 넷뿐이고, 결함 ⑤ 가 실제로 그렇게 드러났다. 값싼 단정 하나가 아니라 **fixture 를
물린 채 도는 것**이 이 축의 이빨이라는 뜻이다.

**`display_back` 의 마지막 소비자가 사라졌다.** 매 프레임 강제로 맨 뒤로 보내는 것은 자유
도킹과 충돌하고, 가운데 노드가 서면 필요도 없다 — Host 는 자기 노드를 떠나지 않는다. 열거자와
기구는 남겼다: 자가 검사가 그 값을 태우고 있고, 지우면 "쓰는 곳이 없다" 와 "지워서 못 쓴다" 가
섞인다.

**착지 뒤 드러난 결함 둘 (2026-09-14, 사용자 보고).** W5 를 닫고 나서 카메라를 돌려 선택
객체가 캔버스를 벗어나게 하니 넷이 보였다 — 기즈모가 계층·인스펙터 위, Scene/Game 탭 줄 위,
Content Browser 위로 그려지고, 탭 줄과 그림 사이에 검은 띠가 남았다. 앞의 셋은 원인이 하나이고
넷째는 다른 하나다. **둘 다 W4 가 낸 회귀**이지 뒤 단계가 닫을 것이 아니었다.

6. **ImGuizmo 는 `SetRect` 사각형을 창 클립과 교차하지 않고 클립으로 민다.** `Manipulate`
   첫 줄이 `mDrawList->PushClipRect(rect, false)` 다(ImGuizmo 1.10 `ImGuizmo.cpp:2682`). W4 가
   좌표 규약을 세우며 `SetRect` 를 창 사각형에서 **crop 의 image 사각형**으로 바꿨는데(화면
   밖으로 밀린 조작점의 투영이 맞으려면 잘린 부분까지 포함한 소스 전체의 자리라야 한다) 그
   사각형은 캔버스 밖까지 뻗는다. 옛 코드는 창 사각형이라 우연히 안 보였다. 더해 W4 가
   `display_back` 을 걷었으므로 Host 의 draw list 는 패널 뒤가 아니라 등록 순서대로 **패널
   뒤에 그려지고**, 그래서 새어 나간 픽셀이 패널 위에 얹혔다. 처방은 ImGuizmo 안을 만지지
   않고 `RenderSceneView` 가 끝날 때 그 뒤에 쌓인 draw 명령의 클립을 캔버스 가시 사각형으로
   되잡는 RAII(`GizmoClipScope`)다 — 조기 반환이 여럿이라 소멸자에 건다.
7. **`EndTabBar` 가 남기는 `ItemSpacing.y`.** 모드 막대를 도크 탭 바에서 Host 본문의
   `BeginTabBar` 로 옮기자 탭 바가 항목이 되어 다음 항목 앞 간격을 내린다. 캔버스는 항목이
   아니라 화면이라 그 간격이 창 배경(검정) 띠로 보였다. 옛 도크 탭 바에는 이 간격이 없다.
   커서를 그만큼 되돌린다.

**게이트가 못 본 이유.** 씬 뷰의 픽셀을 재는 단정이 run-all 에 없다 — 도크 감사는 창의 자리를
세고 캔버스 규약은 좌표를 세지, 어느 draw 명령이 어느 클립으로 나가는지는 아무도 세지 않는다.
둘 다 캡처로 확인했다(격리 워크스페이스 · `object.transform` 으로 프로브를 캔버스 가장자리에
놓고 찍은 것 · 파란 축이 캔버스 아랫변에서 잘린다). draw 명령의 클립을 캔버스 사각형과
맞대는 단정은 W8 의 visual golden 몫으로 넘긴다.

#### W4 후속 — extent 기반 resize · 렌더 배율 (2026-09-14)

계획서가 W4 에서 "두 backend 의 generation/retire 검증 뒤" 로 미뤄 둔 항목이다. 미루는 동안
비용이 실제로 청구됐다 — 사용자가 같은 SampleScene 에서 FPS 반토막을 보고했고, 원인이 이
항목이었다.

**무엇이 틀려 있었나.** 라이브 뷰의 렌더 해상도가 `ScreenResizeBus`, 즉 **창 클라이언트
크기**였다(`EditorMain.cpp` 가 `GetClientRect` 로 채우고 `EnhancedSceneRenderer.cpp:4477` 이
그대로 렌더 타깃 크기로 쓴다). 가운데 Host 의 캔버스는 좌우·아래 패널이 자리를 가져가므로 창
보다 늘 작다. 거기에 `ebd27f3a` 가 에디터 창을 DPI 배수로 키웠다. 그 커밋은 "게임 창에서
클라이언트 크기는 곧 렌더 해상도라 말없이 키우면 픽셀이 네 배가 된다" 고 적고 게임 창만
제외했는데, 에디터도 같은 버스를 통해 똑같이 렌더 해상도였다. 실측: 창 2560x1485 에 캔버스
1924x873 — **픽셀 5.09 배**를 그려서 잘라 버리고 있었다.

**두 엔진이 쓰는 계약.** Godot 은 에디터 3D 뷰를 `SubViewportContainer` 안의 `SubViewport` 로
두고 stretch 가 "the sub-viewport will be automatically resized to the control's size" 를
한다. 표시보다 낮게 그리는 손잡이는 `stretch_shrink` 이고 "divides the sub-viewport's
effective resolution by this value while preserving its scale" 다. Unreal 은 렌더 타깃을
Slate 뷰포트 위젯 크기로 잡고, 에디터의 secondary screen percentage 기본값을
`100 / OS's DPI Scale` 로 둔다 — 문서가 이유를 둘로 적는다(고밀도에서 성능을 일정하게, 그리고
GPU 가 감당 못 할 중간 렌더 타깃을 만들지 않기 위해). 끄는 선택지는
`Disable DPI Based Editor Viewport Scaling` 이고 기본은 꺼짐, 즉 **기본이 보정하는 쪽**이다.

**바꾼 것.**

- **버스의 뜻을 바꿨다.** 에디터에서 `ScreenResizeBus` 는 이제 창이 아니라 **뷰포트 렌더
  해상도**다. Host 가 프레임마다 자기 content region 을 물리 픽셀로 게시하고
  (`viewport_demand::canvasWidth/Height`), `EditorMain::ApplyViewportRenderExtent` 가
  캔버스 × 배율을 버스에 반영한다. 스왑체인은 여기 걸려 있지 않다 — ImGui 셸이 프레임마다
  자기 `GetClientRect` 로 잡는다(`ImGuiHost::BeginFrame`). 버스 하나를 바꾸니 카메라 종횡비 ·
  화면 크기를 따라가는 텍스처 · 게임 UI 배치가 **배선 없이** 함께 따라왔다.
- **렌더 배율을 뒀다.** `render_scale_mode` 셋 — `dpi_auto`(1/DPI, 기본) · `off` · `fixed`.
  범위는 0.25~1.0 이고 1 을 넘기는 값은 거부한다(이 작업이 없애려던 낭비를 되살리므로).
  CLI 는 `editor.renderscale [auto|off|<값>]`.
- **캔버스 정책이 `fill` 이 됐다.** Scene 의 crop 은 타깃이 창 크기였을 때 맞는 정책이었다.
  타깃이 캔버스를 위해 만들어지면 image 사각형 = content 사각형이고, 배율이 1 보다 작을 때
  crop 은 그림을 작게 그리고 가장자리를 검게 남긴다. **덤으로 W4 후속 결함 ⑥(ImGuizmo 가
  SetRect 를 창 클립과 교차하지 않아 기즈모가 패널 위로 새던 것)이 구조적으로 닫혔다** —
  image 가 content 를 넘지 않으므로 넘어갈 픽셀이 없다. `GizmoClipScope` 는 방어로 남긴다.
- **진정과 격자.** extent 는 연속 8 프레임 같을 때만 적용한다(스플리터를 끄는 동안 매 프레임
  파이프라인을 다시 만들지 않기 위해). 격자는 **2** 다 — 처음 8 로 두었다가 되돌렸다. 올림
  격자는 두 축을 다른 비율로 밀어 렌더 종횡비를 캔버스에서 떼어 놓고, 그림은 캔버스 전체에
  펴지므로(fill) 그 어긋남이 그대로 기즈모와 그림 사이의 밀림이 된다(2244x1098 에서 0.5%).

**실측 (Release · SampleScene · 창 2880x1665 · 렌더 스레드가 실제로 그린 프레임).**

| 상태 | 렌더 해상도 | 픽셀 | fps |
|---|---|---|---|
| 이전(창 크기) | 2880x1710 | 4.92 Mpx | 522 |
| extent 만(`off`) | 2244x1098 | 2.46 Mpx | 721 |
| extent + `auto` | 1496x732 | 1.10 Mpx | 903~935 |

**관측을 먼저 고쳐야 했다.** 처음 두 번의 측정이 둘 다 틀렸고 이유가 달랐다. ① `wait` 는
게임 스레드 프레임이라 렌더 스레드와 분리돼 있어 해상도에 반응하지 않는다. ②
`display.sourceFrame` 과 `consumedFrameId` 는 게임 스레드가 매긴 **id** 라 latest-wins 로
버려진 프레임이 있어도 증가분이 같다 — 둘 다 처리량을 못 잰다. 렌더 프레임 **수**는
`framesRendered` 인데 GUI 창에만 있었다. `dx12.live status` 에 `framesRendered` ·
`framesIdle` · `framesInFlight` 를 냈고 그제야 축이 섰다. ③ 그리고 Debug 로 재면 해상도
효과가 **0 으로 보인다** — 프레임당 CPU 5ms 가 GPU 픽셀 비용을 통째로 덮는다. 같은 A/B 가
Release 에서 1.61 배였다.

**판정.** `verify-editor-viewport-extent.ps1` 단정 50 개, run-all 에 이었다. 판정 항목은
캔버스가 창보다 작다 · 렌더 해상도 = 캔버스 × 배율(배율 셋 모두) · 배율 계약(off=1 ·
auto=1/DPI · fixed=준 값) · 파이프라인 수렴 · 범위 밖 배율 거부 · **리사이즈 연타 뒤
generation 수렴**(계획서가 미룬 이유였던 축을 미루는 대신 여기서 잰다) · 정상 종료다.
파이프라인 수렴은 한 번 재지 않고 표본 여섯을 찍어 그 중 하나가 맞으면 통과하고 몇 번째에
맞았는지를 낸다 — 한 번만 재면 게이트가 "얼마나 기다리면 되는가" 를 기계 속도에 달린
상수로 박게 된다.

| 심은 결함 | 붉어진 자리 |
|---|---|
| extent 를 버스에 반영하지 않는다(창 크기로 되돌림) | 렌더 해상도 단정 8 건 + "리사이즈 세대가 늘지 않았다" |
| 렌더 배율을 언제나 1 로 | auto·fixed 배율 단정 3 건 (off 는 그대로 초록 — 맞다) |
| 배율 범위 검사를 걷는다 | "범위 밖 배율이 통과했다" 2 건 |

**게이트가 만든 결함 둘을 적어 둔다.** 처음 판에서 `maxMissingTextureMs` 를 문턱으로 리사이즈
건강을 쟀는데 그 값은 **부팅까지 포함하는 누계 최댓값**이라 18 초가 나왔다 — 첫 라이브 프레임이
그만큼 걸린다는 사실을 잰 것이지 리사이즈를 잰 것이 아니다. 세대 수렴으로 바꿨다. 그리고 범위
밖 배율 거부를 본 실행에 섞었더니 session 이 그 거부를 종료 코드에 적어 "정상 종료" 단정이
남의 이유로 붉었다 — 거부는 따로 띄운다.

**못 잡는 것.** 캔버스 정책을 `fill` 에서 `crop` 으로 되돌리는 변이는 이 게이트가 못 잡는다
(픽셀을 세는 단정이 없다 — W8 visual golden 몫이다). 같은 버스를 쓰는 다른 backend 도
따라오지만 이 페이즈에서 실행으로 확인하지 않는다 — 에디터의 backend 가 아니다.

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
- gizmo/picking/game input/cursor/focus의 단일 owner를 만든다. UI 커서 모양은 ImGui backend,
  게임 lock/clip/visibility는 ViewportHost의 소유권 정책으로 조정한다(§1.8).
  focus loss·Eject·Stop·비정상 종료에서 해제를 각각 단정한다.
  W2-V가 제공하는 오버레이 입력 소비 결과를 연결하고, PlayingPossessed에서 편집 도구 입력을 차단한다.
- Undo Clear 이중 경로(§1.6)를 controller 단일 소유로 정리한다.
- Stop 뒤 prior document/focus/selection 정책을 복원한다.

**판정:** `verify-play-roundtrip.ps1`을 유지하면서 viewport target과 input owner 단정을 추가한다.
`play.state`를 committed·target·input owner까지 내도록 확장하고, **스냅샷 실패를 주입했을 때
UI가 Playing으로 보이지 않는 것**을 명시적으로 단정한다. Play 실패·Alt-Tab·Eject·Pause·Stop
반복에서 cursor/gizmo가 잘못 남지 않는다.

#### W5 착지와 결함 (2026-09-14)

**착지.** Core는 신호만 늘었다 — `SceneManager::IsPlayCommitted()`·`PlayFailureCount()`·`LastPlayFailure()`·
`InjectPlaySnapshotFailure()`, 그리고 `InputManager::SetGameInputOwned()`(커서 의사 `IsCursorHideRequested()`와
적용 `IsCursorHidden()`을 가른다). Editor는 `Editor::PlayModeController`가 상태 머신이다: 게임 스레드에서
입력 갱신 **뒤**, 씬 틱 **앞**에 `Tick()`이 돌아 신호 셋과 Host 게시본(모드·UI 입력 상태·캔버스 클릭)만 읽고
상태를 **유도**한다. 클릭이나 진입 통지 하나로 확정하지 않는다.

- Possess/Eject는 **Host의 표시 모드**다 — Game 모드가 possess, Scene 모드가 eject. 별도 버튼을 만들지
  않은 이유는 모드 막대가 이미 그 뜻이고 승인된 외관을 바꾸지 않기 위해서다. CLI는 `play.possess`/`play.eject`.
  게임 캔버스(Host Game 모드·Game Preview)는 `InvisibleButton`이 되어 클릭이 possess 요청이다 — Ejected에서
  Preview를 누르거나, 글자를 치다 캔버스로 돌아올 때 활성 항목이 바뀌며 소유권이 게임으로 넘어온다.
- 입력 소유자는 프레임마다 하나다: `game ⇔ PlayingPossessed ∧ ¬paused ∧ 창이 전경 ∧ ¬uiWantTextInput`.
  관문은 게임 소비처 셋만 본다(`InputActionManager`·`UIManager`·C# `Api_Input_*`). 에디터 단축키와 씬 카메라는
  같은 장치 상태를 계속 읽되, `EditorMain`의 Ctrl+Z/Y는 게임이 주인인 프레임에 받지 않는다. Player는 관문을
  부르지 않으므로 기본값(소유)이 출하 게임의 입력을 그대로 둔다.
- Stop은 prior를 되돌린다 — Host 모드(요청함), Host 포커스(`request_viewport_focus`), 선택(instanceID —
  백업 YAML이 `m_instanceID`를 싣고 복원이 그것을 쓰므로 논리 신원이 산다). Core의 해제 안전장치는 그대로
  돌고 그 **뒤**에 컨트롤러가 되찾는다. Undo 항목은 만들지 않는다 — 정지는 편집이 아니다.
- Game 타깃 not-ready(게임 카메라 없음)는 `gameTargetReady`로 **보고**하고 막지 않는다 — 카메라를 키 입력으로
  스폰하는 게임이 있다. 캔버스는 W4의 자리 표시(“No Camera rendering”)를 그대로 그린다.
- 검증 손잡이 셋: `play.inject_snapshot_failure`(실패 경로), `play.foreground_override`(게이트는 창을 숨겨
  띄우므로 OS 전경을 만들 수 없다), `play.cursor hide|show`(스크립트의 `SetCursorVisible`과 같은 경로).
  손잡이 없이 재는 것도 하나 있다 — 숨긴 창의 Possessed는 `owner=editor`다(전경 조건이 실제로 걸린다).

**결함.** 계획서가 지목한 것과 실제가 갈렸다(§1.6·§1.8에도 적었다).

1. 통지가 스냅샷 앞이라 실패한 전이가 편집 이력부터 죽였고, 실패해도 요청이 서 있어 Stop 아이콘이 뜬 채
   시뮬레이션이 없었다 → 순서·되돌림·확정 신호.
2. `EditorMain::Update`가 **요청**으로 시뮬레이션 분기를 골라 Play를 누른 프레임에 스냅샷이 뜨기 전 Physics·GameLogic이
   한 틱 돌았다(`Update` → `ApplyPending` 순서). 그 결과가 백업에 섞여 정지 뒤 편집 씬이 한 프레임 어긋난 채
   돌아온다 → **확정**으로 가른다. `IsManagedScriptSimulationActive`(Scene.cpp)는 이미 `요청 ∧ ¬진행`으로 같은
   창을 피하고 있었다 — 손대지 않았다.
3. `WantCapture*` 강제는 죽은 줄이었다(§1.8 실측). 걷었다.
4. 게임 입력에 관문이 없었다 — 계획서에 없던 결함이고 W5의 실체다.
5. 커서 숨김에 주인이 없었다 — `ShowCursor` 카운터가 정지·Eject·Alt-Tab 너머로 남는다 → 의사/적용 분리,
   소유가 아닐 때 적용하지 않고 돌아오면 다시 적용, Stop이 의사를 지운다.
6. 선택은 해제만 됐다(그때의 게이트가 그것을 단정했다) → 복원. `verify-play-selection-undo.ps1` 판정 D가
   “해제”에서 “복원”으로 뒤집혔고 머리말에 두 번 바뀐 이유를 적었다.
7. Undo 이중 경로는 이미 LC6이 닫은 뒤였다 — 고친 것이 아니라 발견이다.
8. 재생 게이트 둘이 개발자의 `active.workspace`를 빌려 띄우고 있었다. 그 파일이 Host 모드를 싣게 된 것이 W4인데
   (W3까지는 실을 값이 없었다), 어떤 실행이 Game 모드로 끝나면 다음 실행의 “재생 전 모드”가 game이 되어 복원
   단정이 엉뚱한 이유로 붉는다 — 변이 ⑧이 그렇게 끝났고 그 뒤의 깨끗한 실행이 붉었다. 두 게이트 모두
   `CREATOR_EDITOR_WORKSPACE_DIR`로 빈 폴더를 받게 했고, 복원 단정의 기준은 상수 `scene`이 아니라
   **재생 전 표본**이다(그 표본이 scene인 것도 함께 단정한다).

**변이.** 다섯을 심어 각각 맞는 자리에서 맞는 이유로 붉는지 봤다.

| 변이 | 붉은 자리(실측) |
|---|---|
| ④ 통지를 스냅샷 앞으로 | 실패 주입 구간 “a failed play transaction cleared the edit undo history (editUndo 1 -> 0)” |
| ⑤ 실패해도 요청을 안 되돌림 | 실패 주입 구간 “state expected 'Stopped' got 'PlayingPossessed'” — 요청이 서 있어 다음 프레임에 다시 들어갔다 |
| ⑥ 소유자가 pause 무시 | `paused` 표본 “cursorHidden expected 'False' got 'True' (owner=game)” — 일시정지에도 게임이 주인이라 커서가 숨은 채다 |
| ⑦ 소유자가 전경 무시 | 첫 실행의 숨긴 창 단정 “a hidden editor window claimed game input: foreground=0 owner=game” |
| ⑧ Stop이 아무것도 안 되돌림 | 첫 실행 “stop did not return … target=game” + 판정 D “stop did not restore the selection” |

변이 러너 자체가 한 번 틀렸다 — ⑤의 역치환이 `return;` 아홉 곳에 걸려 실패했는데 러너가 넘어가 ⑥이
⑤를 품은 채 돌았고, ⑤의 이유로 붉었다. 쌍을 앞 주석 줄까지 넣어 고유하게 만든 뒤 ⑥만 다시 쟀다.

**못 잡는 것.** 기즈모 잔류(`ImGuizmo::IsUsing`이 Eject·Stop 뒤에 남는가)는 CLI로 몰 수 없어 단정이 없다 —
Possessed에서는 Scene 뷰가 그려지지 않아 구조적으로 막히고, Ejected는 W2-V의 오버레이 소유권이 그대로다.
W2-V의 “오버레이 입력 소비 결과 연결”은 그래서 별도 배선이 아니라 모드 하나로 닫혔다. OS 커서 **모양**은
ImGui backend 몫 그대로이고, lock/clip은 구현이 없어 소유권 정책에 걸 것이 visibility뿐이었다. Player의
죽은 스냅샷(E3-6 소속)은 손대지 않았다 — 되돌림은 Player도 타지만 Player는 정지하지 않는다.

### W6 — preset 5종과 layout UX (P2, 2일)

- `S&Box Compact`, `Level Editing`, `UI Editing`, `Rendering & Debug`, `Legacy Unity`를 제공한다.
- Save As/Rename/Delete/Reset와 active workspace 표시를 만든다.
- small window와 DPI 변화에서 minimum central area를 보존한다.

**판정:** 각 preset을 연속 적용해도 orphan dock node와 off-screen floating panel이 없고, 사용자가
수정한 workspace를 preset update가 덮어쓰지 않는다.

#### W6 착지 — preset 다섯과 자리의 정본 이동 (2026-09-15)

W3 이 남겨 둔 한 줄이 이 조각의 전부다: *"남은 것은 자리 **이름**을 `dock_slot` 열거자가
아니라 workspace 선언에서 받는 일이고 그것이 W3·W6이다"*
(`Windows/EditorStandardWindows.h`). 창 선언의 `.dock(...)` 은 이제 **기본 preset 에서의
자리**이고, 다른 preset 이 그 위에 재정의를 얹는다(`Editor/EditorWindow/EditorLayoutPreset.h`).

**기본 preset 은 재정의가 0 이다.** 계획서가 "**현재 외관을 유지하는** 5종" 이라고 적었는데,
기본 배치를 값으로 베껴 적으면 선언과 두 벌이 되고 언젠가 한쪽만 고쳐져 "현재 외관" 이
조용히 갈린다. `sbox_compact` 가 재정의를 하나도 갖지 않는 것은 그래서다 — 오늘의 모습과
같다는 것이 값의 일치가 아니라 **출처의 동일성**으로 선다. 픽셀로 확인했다: 착지 전후
창을 같은 fixture 로 찍어 **392,888 픽셀 중 다른 픽셀 0.**

| preset | 무엇이 달라지는가 |
|---|---|
| `sbox_compact` | 오늘 그대로. 재정의 0 |
| `level_editing` | 뷰포트를 넓힌다. 아래 탭은 Content Browser 하나만 |
| `ui_editing` | 오른쪽 열을 넓히고 Inspector 가 대부분을 갖는다. 입력 맵을 오른쪽 아래로 |
| `rendering_debug` | 떠 있던 관측 창 넷(RenderPass·Debug·Profiler·Log)을 아래 탭으로 모아 연다 |
| `legacy_unity` | Hierarchy 를 **왼쪽 전체 높이**로. 그래서 `dock_slot::left` 를 새로 세웠다 |

**안 쓰는 자리는 만들지 않는다.** `legacy_unity` 는 오른쪽 열을 위아래로 가르지 않고
`sbox_compact` 는 왼쪽을 쓰지 않는다. 그 자리를 그래도 갈라 두면 아무도 안 들어오는 빈
노드가 남고, 그것이 계획서가 금지한 orphan dock node 다. 빌더는 **그 preset 에서 실제로
쓰이는 자리만** 가른다. 같은 이유로 창 감사의 "빈 도킹 자리" 도 뜻이 갈렸다 — 빈 노드는
빌더가 책임지므로 감사에 남는 것은 **어느 preset 도 쓰지 않는 죽은 어휘** 쪽이다.

**게이트가 자기 코드의 결함을 잡았다.** `verify-editor-layout-preset.ps1`(197 단정, run-all
배선)을 처음 돌렸을 때 900x620 에서 오른쪽 열이 `x 900..900` — **폭 0** 이었다. 최소 중앙
영역을 지키느라 옆을 끝까지 줄인 것이다. 도크 트리는 멀쩡하고(노드도 있고 창도 붙어 있다)
고아도 유령도 아니라서 기존 감사가 통째로 못 보던 자리다. 고친 것 둘: ① 패널에도 바닥을
두고(옆 200 · 아래 120 논리 px), 둘이 함께 설 수 없을 만큼 창이 작으면 **규칙을 버리고
비율을 그대로 쓴다**(가운데를 지키려다 패널을 0 으로 만드는 것이 더 나쁘다). ② `editor.dock`
이 `degenerateNodes` 를 센다 — 폭이나 높이가 0 인 보이는 잎의 수.

**파일 스키마 2.** `preset` 을 담는다. **v1 도 계속 읽는다** — 버전을 하나 올렸다고 쓰던
배치를 "복구했습니다" 한 줄과 함께 버리지 않는다. v1 에는 그 필드가 없으므로 기본 preset
으로 읽는다. Reset 은 "기본 preset" 이 아니라 **지금 preset** 의 기본 배치로 돌린다 —
Legacy Unity 를 쓰던 사람이 Reset 을 눌러 S&Box 로 튀면 그것은 복원이 아니라 다른 배치다.

**preset 은 자리를 말하지 열림을 빼앗지 않는다.** 재정의가 열림을 말하지 않으면(`inherit`)
사람이 둔 대로 둔다. 그리고 적용 전에 `before-preset` 백업을 먼저 남긴다 — 계획서 판정의
뒷절(*"사용자가 수정한 workspace를 preset update가 덮어쓰지 않는다"*)이 그것이다.

**재지 못한 축을 적어 둔다.** 변이 일곱 중 여섯은 게이트를 붉게 만들었고, 하나는
**자극조차 못 했다** — 최소 중앙 보존을 걷어도 결과가 한 픽셀도 안 달라진다. 이 기계는
`FontScaleMain × FontScaleDpi = 2.25` 라 하한이 480·2.25 = 1080 · 300·2.25 = 675 이고,
게이트가 쓰는 가장 큰 창(도크 뿌리 1600x955)도 그것을 담지 못한다. 담지 못하면 제품은
규칙을 버리고 비율을 그대로 쓴다(그 판단이 `fit_side` 의 탈출 가지다). 그래서 그 줄은
이 배율에서 실행될 수 없다.

이것을 "게이트가 못 잡았다" 로 적으면 게이트를 억울하게 깎고, 덮어 두면 구멍이 된다.
그래서 게이트가 **건너뛴 회차를 센다** — `[건너뜀] 최소 중앙 보존 축은 이번 실행에서
한 번도 재지 못했다 — 배율 2.25 에서 하한이 창보다 크다(회차 12 건 전부 탈출 가지)`.
배율 1 인 기계에서는 같은 단정이 실제로 서고 같은 변이가 붉어진다. W8 의 DPI 축이
모니터 배율을 바꿔 가며 도는 자리를 만들면 그때 닫힌다.

같은 이유로 **게이트에서 480/300 을 지웠다.** 전에는 그 수를 게이트가 제 파일에 베껴
두고 배율을 1.0 으로 가정했는데, 그러면 제품 상수와 두 벌이 되는 데다(이 조각이 없애려던
바로 그 문제다) 단정이 제품 규칙보다 **낮은 자리**에 서서 위반을 통과시킨다. 이제 상수는
`layout_minimums` 하나뿐이고 `editor.dock` 이 배율을 곱한 실효 하한과 `uiScale` ·
도크 뿌리 크기를 함께 실어 게이트가 그것을 읽는다.

#### W6-2 착지 — 이름 붙인 workspace 여럿 (2026-09-15)

preset 은 **출발점**이고 이쪽은 사람이 만든 것이다. 계획서 W6 의 남은 절
(*"Save As/Rename/Delete/Reset와 active workspace 표시를 만든다"*)에서 Reset 과 active
표시는 W6 이 닫았고, 여기서 나머지를 닫는다.

**파일 이름이 곧 이름이다.** 표시 이름과 파일 이름을 따로 두면 둘을 잇는 표가 생기고,
그 표가 파일과 어긋나는 순간 *"목록에 있다고 적힌 배치를 못 여는"* 상태가 된다. 그래서
`<이름>.workspace` 가 그대로 그 배치이고, 대신 **이름 쪽을 좁혔다** — 빈 이름 · 64 자
초과 · `<>:"/\|?*` · 제어 문자 · 앞뒤의 공백이나 점(탐색기가 조용히 떼어 낸다) ·
장치 이름(CON·NUL·COM1~9…) · `active`(활성 파일의 자리)를 **만들 때** 거절한다.
저장할 때 실패하면 사람은 무엇이 문제인지 모른 채 배치를 잃는다.

**복사해 둔 파일의 정본은 파일 이름 쪽이다.** 사람이 배치 파일을 복사해 두는 일은
실제로 일어나고, 그때 파일 안에 적힌 `name` 은 원본의 것이라 남을 가리킨다. 목록에
보이는 것이 파일 이름이므로 안쪽을 믿으면 *고른 것과 열린 것의 이름이 다른* 상태가
된다. 게이트가 이 회차를 든다 — 에디터가 꺼진 사이에 파일을 복사해 두고 다시 띄운다.

**되돌릴 것을 남긴다.** 같은 이름으로 다시 저장(`before-overwrite`) · 이름 붙인 것을
열기(`before-load`) · 지우기(`before-delete`). 이름 바꾸기는 **복사가 아니라 이동**이고,
같은 이름이 이미 있으면 덮지 않고 거절한다 — 이름을 바꾸다 남의 배치를 지우는 것은
이름 바꾸기가 요구한 일이 아니다.

**지금 쓰는 배치를 지워도 화면은 그대로다.** 지우기는 파일을 지우라는 말이지 배치를
갈아엎으라는 말이 아니다. 이름만 preset 라벨로 돌아가고 `named` 가 거짓이 된다.

**같은 표면이 GUI 와 CLI 로 갈리지 않는다.** 이름에는 공백이 들어가므로 CLI 가 파서가
자른 조각을 도로 붙인다. 안 붙이면 `My Layout` 을 GUI 로는 만들 수 있고 CLI 로는 못
만드는 배치가 생긴다. 없는 이름을 열거나 지우는 것은 **그 명령이** 실패한다 — 대기열에
넣고 다음 프레임의 `status.error` 로 미루면 명령의 종료 코드가 초록이 되어 아무도 못
본다(preset 에 이미 세운 원칙과 같다).

**게이트.** `verify-editor-workspace-named.ps1` — 91 단정, 에디터 5 회 기동, run-all
배선. 왕복 판정은 이름이 아니라 **그 배치를 만든 preset** 으로 한다: 이름만 보면 이름을
바꿔 적기만 해도 통과한다. 변이 9 종 중 여덟이 붉었다.

**재지 못한 축 하나.** 지우기가 배치까지 갈아엎는 변이는 초록이다. 갈아엎어도 같은
preset 으로 같은 트리를 다시 짓기 때문에 `editor.dock` 이 보는 값이 달라지지 않는다.
사람이 손으로 옮긴 배치가 있어야 갈리는데 CLI 로는 패널을 끌 수 없다 — W8 의 visual
golden 이 닫을 자리다.

**W6 이 남긴 게이트 회귀를 여기서 고쳤다.** `verify-editor-workspace.ps1` 이 파일 헤더를
`CreatorWorkspace 1` 로 **베껴 적고** 있었다. W6 이 스키마를 2 로 올리자 그 줄 하나 때문에
212 단정짜리 게이트가 붉어졌다 — 제품이 옳게 움직였는데 게이트가 낡은 것이다. W6 착지
때 이것을 못 본 이유가 더 나쁘다: 그 게이트의 기본 `-Exe` 가 **x64-Debug** 를 가리키고
있어서, 내가 돌린 것은 내 변경이 들어가지 않은 낡은 바이너리였다
(`gate-measures-stale-binary` 의 첫 번째 변종 그대로다). 이제 판 번호를 제품 헤더의
`schema_version` 에서 뽑는다 — 게이트가 상수를 다시 적지 않는다.

**남은 것.** 이름 붙인 배치의 **재시작 넘김**은 `verify-editor-workspace.ps1` 의 몫으로
남겨 두었다. 이 게이트는 한 세션 안의 왕복과, 에디터가 꺼진 사이에 파일을 놓아 둔
회차까지만 본다.

### W7 — Hierarchy/Browser clipping과 presentation cache (P1, 3일)

- 1k/10k/50k hierarchy와 asset fixture를 만든다.
- **flatten presentation cache를 먼저 만들고**, 그 위에 visible-row clipping을 얹는다(§8.3의
  순서 제약). 재귀 `TreeNodeEx` 위에 clipper를 바로 끼울 자리는 없다.
- lowercase/search cache, icon/label formatting cache를 측정 기반으로 적용한다.
  Browser 폴더/자산 목록 스냅샷을 W2-B와 공유한다. W2-B는 탐색·범위·검색/정렬 조건을 소유하고
  W7은 변경 반영·파생 목록·clipping을 소유한다. 폴더만 있는 위치와 전체/최근 범위도 같은 신원을 사용한다.
- **2026-09-11 추가 확정 — 비동기 에셋 썸네일:** 메시·텍스처 등은 대기·로딩 중 유형 아이콘을
  표시하고 Ready 결과가 게시된 다음 프레임부터 같은 타일에 썸네일을 적용한다. 실패·미지원은
  유형 아이콘을 유지한다. UI에서 파일 읽기·생성·GPU 완료를 기다리지 않는다.
  가시 타일 우선 큐·중복 요청 억제·자산/의존성 변경 무효화·메모리 예산과 퇴출을 구현한다.
  CPU 작업과 렌더/GPU 자원 소유 경로를 지키며, 오래된 요청의 늦은 완료는 폐기한다.
  캐시는 안정 Texture 신원을 소유하고 backend `ImTextureID`를 보관하지 않는다.
  세부 계약은 [EditorIconSelectionStudy.md](../analysis/EditorIconSelectionStudy.md)를 따른다.
  이 추가 범위는 아직 미구현이며 위 3일의 기존 clipping 견적에 포함되지 않는다. 착수 시 별도 산정한다.
- `HierarchyStore` 단독 정본 불변식을 source gate로 고정한다. 현재 Hierarchy 창이
  `scene->m_Entities`를 직접 인덱스 순회하는 경로가 이 정본과 어긋나지 않는지 착수 시 확인한다.

**선행:** SceneGraph H3(현재 완료).
**판정:** 결과/선택/drag-drop 의미가 동일하고 p95 CPU·allocation 개선 수치를 기록한다. 캐시만
추가하고 실측 이득이 없으면 제거한다.
썸네일은 로딩 중 입력·스크롤 유지, Ready 직후 교체, 타일 ID/선택/drag-drop 보존,
실패·변경·삭제·늦은 완료와 DX12/Vulkan 자원 수명을 별도로 검증한다.

#### W7-0 착지 — 재는 법과 fixture, 그리고 기준선 (2026-09-14)

계획서가 W7 의 판정으로 적은 *"p95 CPU·allocation 개선 수치를 기록한다. 캐시만 추가하고
실측 이득이 없으면 제거한다"* 는, 그 수치를 낼 창구가 없으면 성립하지 않는다. `profile.stats`
는 프로파일러 **자체** 비용과 용량만 낸다. 그래서 W7 의 첫 조각은 캐시가 아니라 **자**다.

**세운 것.** `EditorPanelCost` 가 슬롯 셋(hierarchy · browser_tree · browser_files)마다 프레임
누적 시간과 **그 프레임에 한 일의 수**(`units` 행/항목, `scans` 디렉터리 스캔)를 센다. 시간만
재면 기계가 빠른 날 캐시가 없는 것이 안 보인다 — 캐시의 목적은 "프레임마다 하던 일을 안 하는
것" 이므로 그 일의 수를 직접 센다. 관측은 `editor.panelcost`, fixture 는
`scene.populate <개수> [fanout]`(저작 `.creator` 가 아니라 `Scene::CreateEntity` 로 만든다),
측정은 `Tools/regression/measure-panel-cost.ps1` 이다. 측정 도구는 **게이트가 아니라서 run-all
에 넣지 않는다** — 판정이 아니라 수치를 남기는 자다.

**곁다리로 하나 더 세웠다.** `editor.window <안정식별자> <open|close|focus>`. Content Browser 가
`AssetBundle`·`ResourceCounter` 와 같은 `dock_slot::bottom` 이라 탭으로 겹치는데, 열려 있어도
선택되지 않으면 ImGui 가 본문을 돌리지 않아 브라우저 비용이 181 프레임 중 **1 프레임**만
잡혔다. `editor.windows` 는 표를 읽기만 하고 여닫는 자리는 메뉴뿐이라 CLI 로 도달할 길이
없었다. 요청만 걸고 UI 스레드가 `draw_windows` 머리에서 소비한다 — 표를 읽는 스레드가 그쪽
하나라 적용도 그쪽이어야 읽기와 쓰기가 갈리지 않는다.

**기준선 (Release · 창 2400x1400 · 평평한 목록 · 512 표본).** 전문은
[EditorPanelCostBaselineW7.md](../analysis/EditorPanelCostBaselineW7.md).

| 엔티티 | hierarchy p95 | hierarchy units | browser_tree p95 | browser_tree scans |
|---:|---:|---:|---:|---:|
| 1,000 | 1.21 ms | 1,000 | 7.56 ms | 24 |
| 10,000 | 10.33 ms | 10,000 | 6.83 ms | 24 |
| 50,000 | 33.27 ms | 50,000 | 4.36 ms | 24 |

**두 가지가 확정됐다.**

① Hierarchy 의 `units` 가 엔티티 수와 **정확히 같다.** 화면에 50 줄쯤 보이는데 50,000 줄을
그린다. 행당 약 0.52 µs 로 선형이고 50k 의 p95 33.3 ms 는 이 패널 하나가 에디터를 30 fps 로
묶는다는 뜻이다 — clipping 이 겨냥할 자리가 숫자로 섰다.

② **계획서에 없던 것이 더 크다.** 브라우저의 폴더 트리가 씬과 무관하게 **매 프레임 24 번**
`directory_iterator` 를 돌린다(열린 폴더 하나당 한 번). 엔티티 1,000 — 즉 실제 프로젝트 규모 —
에서 **브라우저가 Hierarchy 의 5.8 배**이고, 그 비용은 사용자가 아무것도 하지 않아도 나간다.
그래서 착수 순서를 Browser 스냅샷(W7-1) → flatten cache(W7-2) → clipping(W7-3) 으로 둔다.
계획서가 W7 을 "clipping" 으로 적은 것은 2026-08-30 정찰에 이 사실이 없었기 때문이다.

**시간이 아니라 `scans` 를 본다.** `browser_tree` 의 p95 가 7.56 → 6.83 → 4.36 으로 흔들리는데,
씬이 커져서 빨라진 것이 아니라 OS 파일 캐시 온도다. 파일시스템을 재는 시간은 이렇게 흔들리고,
그래서 스캔 수를 따로 센다 — 캐시가 그것을 0 으로 만들었는지는 그 수로만 명확히 판정된다.

#### W7-1 착지 — Browser 스냅샷 (2026-09-15)

브라우저가 **프레임마다 파일시스템을 훑고 있었다.** `ShowDirectoryTree` 가 열린 폴더 하나마다
`directory_iterator` 를 돌리고 재귀했고, `ShowCurrentDirectoryFiles` 가 또 한 번 훑은 뒤 항목마다
`is_directory`(stat)를 부르며 **비교마다 stat 하는 비교자**로 정렬했다. 캐시가 하나도 없었다.
계획서 W7 이 브라우저를 "스냅샷을 W2-B 와 공유한다" 한 줄로만 적어 이 사실이 기록돼 있지 않았다.

**무효화 근거 셋.** §8.3 의 *"invalidation 근거가 없으면 매 frame 전체 cache를 믿지 말고 fail-safe
rebuild한다"* 를 이렇게 읽었다. ① **자기 변경** — 새 폴더·Volume Profile·프리팹 드롭 뒤 즉시
무효화하고 다음 프레임은 예산을 푼다(사람이 방금 만든 폴더가 1 초 뒤에 나타나면 버그로 보인다).
② **나이** — 1 초. 밖에서 파일이 바뀌는 것을 이 경로가 알 방법이 지금은 없다. ③ **프레임 예산 1**
— 낡았다고 한 프레임에 24 개를 몰아 훑지 않는다. 그러면 초당 몇 번씩 스파이크가 생겨 *비용을
줄이려다 p95 를 악화시킨다.* 정상 상태는 프레임당 스캔 ≤1 이고, 폴더 24 개면 각각 1 초에 한 번쯤
갱신된다. 감시자(`EditorDirectoryWatcher`)를 붙이면 ②가 필요 없어져 idle 스캔이 0 이 되는데,
그것은 별도 조각으로 남긴다.

**캐시는 정책을 모른다.** 지원 확장자·검색·유형 필터·정렬 방향은 창이 소유한다(W2-B 의 몫).
캐시가 주는 것은 디스크를 만지지 않고 얻는 목록뿐이다 — 그래서 W2-B3 이 기다리던 "같은 목록
신원" 이 여기서 선다.

**전후 (Release · 같은 경로에서 연달아).** 전문은
[EditorPanelCostBaselineW7.md](../analysis/EditorPanelCostBaselineW7.md).

| 슬롯 | | p95Ms | **scans** |
|---|---|---:|---:|
| browser_tree (1k) | before | 5.132 | **24** |
| browser_tree (1k) | after | 2.000 | **0** |
| browser_files (1k) | before | 0.385 | **1** |
| browser_files (1k) | after | 0.141 | **0** |
| hierarchy (대조군) | before / after | 0.925 / 0.865 | units 1,000 그대로 |

**판정은 `scans` 다.** 시간의 2.6 배는 부차적인 증거다 — 파일시스템을 재는 시간은 같은 날 같은
자로 연달아 재도 OS 캐시 온도로 흔들리고, 흔들리지 않는 것은 "디스크를 몇 번 만졌는가" 다.
50k 표본에서 1 로 나온 것은 나이 1 초가 지난 폴더 하나를 예산대로 다시 훑은 것이고 설계 그대로다.
Hierarchy 의 `units` 가 그대로인 것도 함께 본다 — 움직였다면 측정이 다른 것을 재고 있다는 뜻이다.

**남은 비용의 성격이 바뀌었다.** after 의 browser_tree 는 스캔 0 인데도 p95 2.0 ms 이고, 이제
그것은 syscall 이 아니라 노드 24 개의 ImGui 그리기다. 줄이려면 W7-3 의 clipping 이 필요하다.

#### W7-2·W7-3 착지 — 평탄 목록과 clipping (2026-09-15)

**둘을 함께 착지시킨 이유부터 적는다.** W7-2 만 얹고 재면 이득이 **0** 이다 — 1,000 에서
p95 0.751 → 0.718 ms, 50,000 에서 28.506 → 28.606 ms, `units` 는 양쪽 다 그대로. W7 의
완료 기준이 *"캐시만 추가하고 실측 이득이 없으면 제거한다"* 이므로 이 표만으로는 W7-2 는
지워야 할 것이다. 평탄 목록은 캐시가 아니라 **clipping 이 설 자리**이고, 자리의 값은 그
위에 무엇이 서는지로만 매겨진다. 그래서 하나의 조각으로 묶었다.

**결과.** `hierarchy.units` 50,000 → **14**, p95 30.427 → **0.041 ms**. 1,000 에서도 14 다 —
씬이 50 배가 되어도 그리는 행 수가 같다는 것, 즉 그리는 일이 씬 크기에서 떨어져 나와
패널 높이에만 매인다는 뜻이다. 전문은
[EditorPanelCostBaselineW7.md](../analysis/EditorPanelCostBaselineW7.md).

**없던 값을 먼저 세웠다 — `HierarchyStore::Revision()`.** §8.3 은 *"HierarchyStore
mutation/revision 또는 명시적 scene event로 cache를 무효화한다"* 고 적으며 이 값을
전제했는데 실물에는 없었다. 계층을 바꾸는 모든 자리(`GrowOne`·`Clear`·`ResetSlot`·
`OccupySlot`·`SetParent`·`SetRoot`·`AttachChild`·`DetachChild`·`ClearChildren`·
`SetChildren`)에서 오르는 수를 두고, 파생 목록은 **이 수가 달라졌을 때만** 다시 만든다.
정본을 복제하지 않는다는 규약은 그대로다 — 목록이 담는 것은 슬롯 인덱스·깊이·자식
유무·접힘·띠 번호뿐이고, 이름·아이콘·잠금·활성·선택은 그리는 순간에 정본에서 읽는다.

**근거가 없는 축은 정직하게 fail-safe 로 두었다.** 이름에는 revision 이 없으므로 검색이
켜져 있는 동안에는 매 프레임 다시 만든다. 그래도 옛 경로보다 싸다 — 옛
`IsMatchedRecursive` 는 **행마다** 자기 서브트리를 통째로 다시 훑어 O(n·깊이) 였고, 지금은
한 번의 전위 순회를 거꾸로 훑어 끝난다. DDOL 은 `Object::SetDontDestroyOnLoad` 가 Entity
플래그만 바꿔 revision 이 못 잡는 축이라, O(1) 로 세어 근거에 함께 넣었다.

**접힘의 소유가 ImGui 에서 창으로 넘어왔다.** clipping 을 끼우면 화면 밖 노드는
`TreeNodeEx` 가 호출되지 않으므로 ImGui 는 그 노드가 열렸는지 말할 기회가 없다 —
*"보이는 행만 그린다"* 와 *"열린 노드를 ImGui 가 기억한다"* 는 함께 설 수 없다. 그래서
접힘을 창이 갖고 매 프레임 `SetNextItemOpen` 으로 알려 준 뒤 `IsItemToggledOpen()` 으로
되받는다. 담는 것은 **기본값에서 벗어난 슬롯**뿐이라 새로 생긴 노드는 옛 기본값
(`parentIndex == 0` 만 펼침 = 옛 `DefaultOpen` 조건)을 그대로 따른다. 대가는 한 프레임 —
펼침/접힘이 목록에 반영되는 것은 다음 프레임이다.

**그림이 달라지지 않았다는 것은 픽셀로 받았다.** 깊이 있는 fixture(`scene.populate 60 3`)
로 before/after 창을 각각 찍어 맞댄 결과 **392,888 픽셀 중 다른 픽셀 0.** 들여쓰기를
`TreePush` 에서 `Indent` 로 옮기고 홀짝 띠 번호를 "그린 순서" 가 아니라 목록이 주게 바꾼
것이 이 확인을 받기 위해서다 — clipper 가 앞줄을 건너뛰어도 띠가 어긋나지 않는다.

**덤으로 사라진 것 둘.** ① 재귀가 사라져 계층 깊이가 더는 호출 스택 깊이가 아니다.
② 옛 경로는 DDOL 묶음을 부모와 무관하게 다시 모아 그려, 일반 루트의 자손인 DDOL
엔티티가 두 번 그려질 수 있었다 — 평탄 목록에서는 처음 닿은 자리 하나만 남는다.

#### W7-4 착지 — 평탄 목록 정본 계약 게이트 (2026-09-15)

`Tools/regression/verify-hierarchy-flatten-contract.ps1`, run-all 에 배선. **72 개 단정.**

**왜 런타임 게이트가 아닌가.** W7-2·W7-3 이 걸고 선 두 축이 다 런타임에서 안 보인다.
① `HierarchyStore` 의 변경 자리 하나가 revision 을 안 올리면 **그 종류의 편집 뒤에만**
트리가 낡는다 — 어느 자리가 빠졌는지 모르니 CLI 로 무엇을 재현할지도 모른다.
② clipper 에 준 줄 높이가 띠를 칠하는 높이와 갈리면 **스크롤해야만** 어긋난다. W7-3
착지 때 392,888 픽셀 대조로 0 을 받았지만 그것은 스크롤 0 의 그림이고, 스크롤을 CLI 로
몰 창구가 없다.

**양쪽을 다 소스에서 유도한다.** 정본 컨테이너 넷은 `HierarchyStore` 의 private 절에서,
무효화 근거 넷은 `hierarchy_flat_key` 의 필드에서, 행 종류 셋은 `hierarchy_row_kind` 의
열거자에서 뽑는다. 손으로 적은 식별자 목록도, 못 박은 숫자도 없다. 맞대는 것은 —

| 한쪽 | 다른쪽 |
|---|---|
| 정본을 바꾸는 멤버 함수 10 개 | `++m_revision` 을 가진 함수 10 개 |
| 선언된 무효화 근거 4 개 | `sameGround` 가 실제로 비교하는 근거 4 개 / 창이 채우는 초기자 4 개 |
| 선언된 행 종류 3 개 | 그리는 switch 의 case 3 개 (`default` 금지) |
| 띠를 칠하는 줄 높이 식 | clipper 에 준 줄 높이 식 · 자리를 메우는 `Dummy` 의 높이 |

여기에 revision 의 단조성(대입 0 건)·초기값 비-0(0 은 캐시가 "못 봤다" 로 쓴다)·
읽는 자리의 const·행 구조체에 컨테이너 필드 0·정본의 저장소 이름이 캐시 쪽 소스에
등장 0 을 더한다.

**세우면서 한 번 눈이 멀었다.** 처음 판은 클래스 본문을 훑을 때 `std::vector` 의 `::` 를
접근 지정자로 세어 경계를 잘못 잡았고, 그 타입을 받는 `SetChildren` 이 함수 목록에서
통째로 빠졌다. 그런데 **양쪽에서 같이 빠져** 집합 대조는 초록이었다 — 대조가 맞았다고
대상이 온전한 것은 아니다. 그래서 유도한 집합의 이름을 전부 찍게 했다(열이어야 할
목록이 아홉 줄로 찍히는 것은 눈에 보인다).

**이빨.** 변이 12 종이 각각 **의도한 단정에서** 붉어졌다(종료 코드만이 아니라 실패 문구
까지 맞췄다). SetParent 의 revision 누락 · 초기값 0 · 되감기 대입 · 근거 하나를 비교에서
제거 · 근거를 선언만 하고 안 채움 · 행에 컨테이너 필드 추가 · case 하나 제거 · `default`
추가 · clipper 보폭을 다른 식으로 · `ItemSpacing.y` 를 4.f 로 · `Dummy` 제거 · 캐시가
정본의 저장소 이름을 씀.

**남는 구멍.** 소스 대조라 화면을 안 본다. 높이 식이 같아도 스타일 스택이 중간에 바뀌면
못 잡는다 — W8 visual golden 의 몫이고, 거기에는 **스크롤한 상태**가 들어가야 한다.

#### W7-5 착지 — 스캔 밖에서 디스크를 만지던 자리 (2026-09-16)

**고친 것은 호출 하나가 아니라 세는 단위였다.**

W7-1 이 매 프레임 디렉터리 스캔을 24 → 0 으로 없앴고 그 게이트는 `scans` 를 보고
초록이었다. 그런데 `browser_tree` 는 그 뒤에도 avg 1.375 ms 였다. 계약은
*"브라우저가 프레임마다 디스크를 만지지 않는다"* 인데 계수기는 *"디렉터리를 훑은
횟수"* 를 셌다 — **강제 단위가 계약의 단위와 달랐고**, 그 사이로 계약을 어기는
다른 모양이 그대로 지나갔다.

지나간 것은 `std::filesystem::equivalent` 다. `ShowDirectoryTree` 가 노드마다
*"이 폴더가 프리팹 폴더인가"* 를 디스크에 물었다 — Windows 에서 그 함수는 두 경로를
**실제로 열어**(`CreateFile` + `GetFileInformationByHandle`) 파일 식별자를 비교한다.
목록 캐시를 경유할 수 없는 종류라 `scans` 옆으로 빠져나갔다.

그래서 축을 하나 더 열었다 — **`probes`**(스캔이 아닌 디스크 접촉). `scans` 는
W7-1 의 뜻 그대로 두고(그 게이트가 그 수에 걸려 있다) 판정은 idle 프레임에서
**둘 다 0** 이다.

**고침은 순서다.** 그 비교는 드래그 중에만 뜻이 있는데 `&&` 왼쪽에 있어 평상시에도
노드마다 돌았다. `ImGui::BeginDragDropTarget()` 은 드래그가 없으면 즉시 거짓을
돌려주므로 관문을 앞에 두고 비교를 블록 안으로 옮기면 평상시 비용이 0 이 된다.
둘 다 부수 효과가 없고 `Begin` 이 참일 때만 `End` 를 부르는 짝도 그대로다.

**실측 (Release · ini 를 지운 선언 배치 · 표본 649 프레임 · 노드 24 개)**

| | 전 | 후 |
|---|---|---|
| `browser_tree` avg | 1.375 ms | **0.181 ms** |
| `browser_tree` p95 | 1.829 ms | **0.385 ms** |
| `browser_tree` probes | — (축이 없었다) | **0** |
| `###Editor.ContentBrowser` avg | 2.446 ms | **0.250 ms** |
| chrome 프레임 총계 avg | 1.892 ms | **0.636 ms** |
| `browser_tree` scans | 0 | **0** (W7-1 의 수를 지키면서 새 축을 얹었다) |
| 게임 스레드 `SceneStructureLockWait` | 2.6~3.2 ms (프레임의 90~95%) | **네 표본 중 셋이 0.000** (남은 하나는 18.45 ms 끊김 프레임과 겹친다) |

판정 값은 계획서가 요구한 대로 **둘을 함께** 봤다 — `browser_tree` p95 와 게임
스레드 락 대기. 앞만 내려가고 뒤가 그대로였다면 범인이 다른 데 있다는 뜻이었을 텐데,
둘이 함께 내려갔다.

**세 번 고쳤고, 매번 앞 회차가 남긴 것을 다음 회차가 잡았다.**

**① 어휘 비교가 성립하지 않았다.** `equivalent` 를 `lexically_normal` 대조로 바꿨는데
양쪽이 같은 모양이 아니었다 — 트리 뿌리는 `weakly_canonical` 이고, 비교 대상
`PathFinder::RelativeToPrefab("")` 은 `path / ""` 라 **끝에 구분자가 붙은 날것**이다.
표준의 정규화 절차는 마지막 요소가 `..` 일 때만 후행 구분자를 지우므로
`".../Prefabs/"` 와 `".../Prefabs"` 는 정규화 뒤에도 다르다. `equivalent` 가 그 차이를
덮고 있었다.

> 그대로 뒀다면 **게이트는 초록이고 성능은 좋은데 프리팹 드롭 대상이 죽은** 상태가
> 됐을 것이다. `probes 0` 은 "디스크를 안 만졌다" 를 말할 뿐 "비교가 옳다" 는 말하지
> 않는다. **계측 축은 기능 축을 대신하지 못한다.**

고친 방식: 비교 대상을 뿌리와 **같은 정규형**으로 미리 풀어 둔다
(`m_prefabDirectory`·`m_volumeProfileDirectory`, 프로젝트가 바뀔 때 한 번). 그리고
비교 함수가 후행 구분자를 접는다.

**② 내가 방금 만든 계수기가 이미 눈멀어 있었다.** `Draw()` 가 매 프레임
`weakly_canonical` 1 회 + `is_directory` 2 회를 부르는데, 그 자리는 슬롯 계측 **밖**이라
새 `probes` 축에도 안 잡혔다 — 계수기가 "누군가 통과시켜 준 것" 만 세는, W7-5 가
고치려던 바로 그 모양이다. 셋을 계수되는 헬퍼로 돌리고 뿌리 해석을 **날것 경로가
바뀔 때만** 하도록 캐시했다.

> 맞바꾼 것: 에디터가 떠 있는 동안 Assets 폴더가 밖에서 지워지면 이제 "unavailable"
> 문구 대신 빈 트리가 보인다. 그 대가로 idle 프레임의 디스크 접촉이 0 이 된다. 밖의
> 변경 감지는 감시자(`EditorDirectoryWatcher`)의 몫이고 이 조각의 범위가 아니다.

**③ 계수 통로의 소비자가 0 이었다.** 계수되는 `equivalent` 통로로 두려고 만든
`browser_same_directory_on_disk` 를 부르는 자리가 하나도 없었다 — 생산만 있고 소비가
0 인 그 모양이다. 걷었고, 덕분에 게이트가 더 센 것을 세울 수 있게 됐다:
**브라우저 코드에 `equivalent` 가 하나도 없다.** 남겨 뒀다면 "하나만 있다" 밖에 못
세우고, 그 하나를 다시 부르기 시작하는 변이를 **어느 축도 못 잡았을 것**이다 —
idle 에는 드래그가 없어 런타임 축이 안 보고, 소스 축은 개수가 그대로라 안 본다.

**지나가다 결함 하나를 더 잡았다.** `DrawFolderMenu` 에 중괄호가 없어
`browser_cache_invalidate()` 가 `if` **밖**에 있었다(W7-1, `e790b678`). 들여쓰기는 안에
있는 것처럼 보이고 Editor 는 `/W0` 라 경고도 없다. 그래서 폴더 우클릭 메뉴가 그려지는
**모든 프레임**에 캐시가 통째로 버려졌다 — 메뉴를 열어 둔 동안 브라우저가 매 프레임
전부 다시 훑었다는 뜻이다. 같은 코드의 복제본인 타일 쪽(`:521`)은 중괄호와 함께 적혀
있어 멀쩡했다. **복제된 코드는 한쪽만 틀린다.**

**게이트** `Tools/regression/verify-browser-filesystem-contract.ps1`
(단정 35 건 · run-all 배선 · Release 전용).

런타임 축만으로는 모자라다. 고침의 핵심이 순서인데 **CLI 에 드래그를 만들 창구가
없어서**, 순서를 되돌리는 변이를 idle 표본이 비껴간다. 그래서 소스 축을 함께 세운다.

| | 판정 |
|---|---|
| ① | idle 에서 `browser_tree`·`browser_files` 의 probes 0 (프레임·누계 둘 다) |
| ② | `scans` 가 W7-1 예산 안 |
| ③ | 시간은 **기록만** — OS 파일 캐시 온도로 흔들린다 |
| ④ | 브라우저 코드에 `equivalent` 0. 면제 하나를 이유와 함께 두고 **면제 자체도 단정** |
| ⑤ | 디스크를 만지는 통로 둘 각각에서 계수가 syscall **앞** |
| ⑥ | 드래그 관문이 경로 비교보다 **앞** (소스 축) |
| ⑦ | 그 통로 둘이 **실제로 불린다** — 소비자 0 인 통로를 지키는 계수기는 빈 단정이다 |
| ⑧⑨ | 슬롯 배선과 전역 계수가 살아 있고 통로 수와 맞는다 |

★ ①보다 **먼저** "그려졌는가" 를 단정한다. 안 그려진 패널의 probes 0 을 계약 준수로
읽으면 눈먼 초록이 된다 — Content Browser 가 뒤 탭이면 트리가 아예 안 그려진다.

**게이트를 돌리기 전에 소스 축만 따로 태워 게이트 자신의 결함 둘을 먼저 잡았다.**
`equivalent` 가 한 자리가 아니라 둘이었고(`EditorAssetDatabase.cpp` — 임포트 시 자기
자신 위 복사 검사라 정당하다. 금지하지 않고 이유와 함께 면제로 빼되 수는 남겼다),
관문 탐지가 **이 고침을 설명하는 주석**을 코드로 세고 있었다. 그냥 돌렸으면 "붉은데
제품이 아니라 게이트가 틀린" 회차를 한 번 낭비했을 것이다.

**변이 8 종 — 전부 기대한 단정에서 붉었다**(종료 코드만이 아니라 실패 문구까지 맞췄다).

| | 변이 | 잡은 축 |
|---|---|---|
| M1 | 트리의 드래그 관문 순서를 되돌린다 | 소스 ⑥ |
| M3 | 옛 코드를 통째로 복원한다(비교를 디스크로 · 관문을 뒤로) | **소스 ④** — ①은 못 잡는다 |
| M4 | probes 계수를 syscall 뒤로 옮긴다 | 소스 ⑤ |
| M5 | `equivalent` 를 다른 파일에 새로 심는다 | 소스 ④ |
| M6 | 슬롯 배선(`add_panel_probes`)을 지운다 | 소스 ⑧ |
| M7 | 면제 자리의 `equivalent` 만 없앤다 | 면제 단정 |
| M8 | 계수 통로를 아무도 안 부르게 만든다 | 소스 ⑦ |
| M9 | 뿌리 해석 캐시를 걷는다 | **런타임 ①** — probes 4/프레임 |

M6·M7·M8 셋은 제품이 아니라 **게이트를 무력화하는** 변이다(계측 배선을 지우고, 면제를
낡게 두고, 계수 통로를 죽은 코드로 만든다). 런타임 축만 있었다면 셋 다 조용히 초록으로
지나갔을 것이다 — idle 에서 probes 는 어차피 0 이니까.

**★ M3 이 런타임 축을 통과했다. 그 이유가 이 조각의 원죄와 같다.**

옛 코드를 통째로 복원한 회차에서 `browser_tree` 는 avg **1.398 ms** 로 옛 비용이 그대로
돌아왔는데 **`probes` 는 0** 이었다. 옛 코드는 `equivalent` 를 **직접** 부르므로 계수되는
통로를 지나지 않는다 — **계수기는 자기가 감싼 것만 본다.** W7-1 의 `scans` 가
`equivalent` 를 못 본 것과 정확히 같은 구조이고, 새 계수기를 얹으면서 같은 함정을 한 층
위에서 되풀이한 것이다.

그래서 두 축의 역할을 갈라 적었다.

- **소스 축 ④가 이 계통의 주 판정이다.** 계수기를 **우회하는** 새 호출은 소스로만 잡힌다.
- **런타임 축 ①은 통로를 지나는 호출이 매 프레임 도는가를 본다.** 그런데 착수 시점에
  ①에는 잡을 대상이 없었다 — 통로를 지나는 것은 뿌리 해석뿐이고 그 자리가 슬롯 계측
  **밖**이었다. `Draw()` 의 델타를 슬롯에 실어 보내고 나서야 M9 가 붉어졌다.

둘 중 하나만 두면 반대쪽 변이가 조용히 지나간다.

**★ M3 회차가 A/B 기준선이다.** 계획서의 발견 수치(avg 1.375 · p95 1.829)를 **같은 기계·
같은 회차·같은 조건**에서 재현했다(avg 1.398 · p95 2.005). 다른 날 잰 값을 맞대는 것이
아니라 한 세션 안의 대조다 — 정황이 아니라 인과다.

**게이트를 돌리기 전에 소스 축만 따로 태워 게이트 자신의 결함 둘을 먼저 잡았고**
(`equivalent` 가 한 자리가 아니라 둘이었던 것, 관문 탐지가 이 고침을 설명하는 **주석**을
코드로 세던 것), 돌리는 동안 셋을 더 잡았다 — `-replace '\', '/'` 가 유효하지 않은
정규식이었고(홑따옴표 안 백슬래시 하나는 미완성 이스케이프다, 세 자리), 배선 단정을
**숫자**로 못 박아 배선이 셋이 되자 *어느* 배선이 사라졌는지 못 말했고, 그것을 이름
집합으로 바꾸자 줄 단위로 상태를 들고 도는 순회가 게이트 안에서만 빈 문자열을 냈다.
마지막 것은 원인 추적 대신 **오프셋으로 되짚는 방식**으로 다시 썼다 — 배선 자리마다 그
앞쪽에서 가장 가까운 함수 머리를 집는다. 상태를 들고 도는 순회는 한 줄만 어긋나도 이름이
통째로 비고, 그러면 "전부 빠졌다" 로 보인다.

**남는 구멍.** 드래그 중의 비용은 여전히 못 잰다 — CLI 에 드래그를 만들 창구가 없다.
지금은 관문이 앞에 서 있어 드래그가 없으면 비교가 아예 안 돌지만, 드래그 중에는 노드마다
어휘 비교가 돈다(디스크는 안 만진다). 그 축이 필요해지면 W8 의 통합 행렬에 드래그
시나리오를 넣어야 한다.

#### W7-6 착지 — 아이콘을 비동기 썸네일로 (2026-09-16)

**판정문은 있는데 그 값을 읽을 자가 없었다.**

계약은 *"썸네일은 실제 GPU 사용 가능 시점을 따른다"* 고 적었다. 그런데 그 시점을
물을 수단이 없었다. `RegisterTexture` 는 업로드 전에도 0 이 아닌 id 를 돌려준다 —
DX12 는 프레임이 열려 있지 않으면 서술자 칸만 예약하고 널 SRV 를 써 둔 채 id 를
낸다. 그 반환값을 *"됐다"* 로 읽으면 한 프레임이 빈 그림으로 나가고, **그 한
프레임은 어떤 계수기에도 남지 않는다.**

그래서 코드를 쓰기 전에 자를 먼저 세웠다.

```cpp
// IImGuiRendererBackend.h
// 그 텍스처의 **픽셀이 실제로 GPU에 올라가 있는가**.
// ★ RegisterTexture 의 반환값으로는 이것을 알 수 없다.
virtual bool IsTextureReady(Texture* texture) const = 0;
```

DX12 는 슬롯의 `uploaded` 로, Vulkan 은 서술자 집합의 존재로 답한다. 기제는 다르고
묻는 것은 같다. PHASE 21 의 판정은 DX12 지만 **미루는 것은 판정이지 배선이 아니다** —
한 팔에만 자를 달면 다음 페이즈가 없는 자를 물려받는다.

##### 자를 세우자 잠금이 드러났다

준비 전까지 유형 아이콘만 그리는 타일은 텍스처를 **등록하지 않는다.** 그리고
등록되지 않은 텍스처는 **업로드되지 않는다.** 그림이 영원히 안 나오는 고리다.

`EditorImGuiTexture::Prime()` 이 그것을 끊는다 — 그리지 않고 등록만 해서 업로드를
일으킨다. 이 고리가 실재한다는 것은 변이 M1 이 증명했다(아래).

##### 캐시 키는 계약이 적은 것과 다르다

계약은 자산 GUID 를 키로 적었다. 쓰지 않았다. GUID 를 얻으려면 타일마다 `.meta` 를
읽어야 하고, **그것은 W7-5 가 방금 닫은 결함을 그대로 되살린다.** 대신 경로 해시 +
`revision`(`last_write_time ^ file_size`)을 쓴다. 이 값은 목록 스캔이 이미 들고 있는
것이라 새 디스크 접촉이 0 이다. 대가는 하나뿐이다 — 이름을 바꾸면 한 번 더
디코딩한다.

무효화도 UI 사건에 걸지 않았다. 같은 경로인데 `revision` 이 다른 항목이 있으면 그
파일은 바뀐 것이고, **목록 스캔이 이미 그렇게 말하고 있다.** 사건을 잡는 쪽(삭제
메뉴·감시자)에 걸면 놓치는 경로가 생기지만, 이쪽은 새 키가 생기는 모든 경우를 덮는다.

##### 실측 (Release · fixture 셋을 뿌리에 올린 회차)

```
requests 3 · decoded 2 · published 2 · failed 1
bytes 65600 = 128*128*4 + 4*4*4          산술과 한 바이트도 안 어긋난다
entries 3 · ready 2                       실패분도 표에 남아 재요청되지 않는다
deduped 1689 · servedThumbnails 1122 · servedIcons 570
browser_tree / browser_files  scans 0  probes 0    W7-5 계약이 그대로다
imgui-error 0 줄
```

`bytes` 가 산술과 정확히 맞는 것이 실패 경로가 **자극됐다**는 증거다. 첫 실패
fixture 는 IDAT 만 망가뜨렸는데 WIC 가 삼켜 8x8 을 냈고, 그때 `bytes` 가 256 컸다 —
그 256 이 아니었으면 "실패 0 건" 을 계약 준수로 읽었을 것이다. 그래서 fixture 를
IHDR 자체가 없는 것으로 바꿨고, 게이트는 바이트를 **등호로** 잰다.

##### 자극할 수 없는 절이 하나 있었다

계약의 *"예산을 넘으면 오래 안 쓴 것부터 버린다"* 를 만들 방법이 없었다. 목록은
clipper 로 **보이는 타일만** 요청하므로 파일을 수백 개 뿌려도 기본 예산 48 MB 에
닿지 않고, 예산은 `constexpr` 이라 밖에서 낮출 수도 없었다. 또 판정문에 자가 없는
모양이다.

`thumbnail_set_budget_bytes` 와 `editor.thumbnail budget <bytes|default>` 로 그 자를
열었다. 기본값은 그대로 48 MB 다.

그 창구로 재 보니 알아 둘 동작이 하나 나왔다. **예산이 작업 집합보다 작으면 캐시가
튄다** — 버리고 → 다시 요청하고 → 다시 디코딩하기를 프레임마다 반복한다(게이트
회차에서 300 프레임에 `evicted 144~194`). LRU 의 당연한 귀결이고 기본 예산에서는
닿지 않는 구간이지만, 예산을 낮추는 사람이 있다면 이것을 알아야 한다.

##### 게이트 — `verify-browser-thumbnail-contract.ps1` (대조 56 checks)

이 게이트의 **절반은 자극이다.** 브라우저 뿌리에는 이미지가 0 이라 기본 배치로 한
바퀴 돌리면 썸네일이 한 번도 요청되지 않고, 장부가 전부 0 이며 모든 단정이 통과한다.
그 초록은 *"지킨다"* 가 아니라 *"묻지 않았다"* 다. 추적되는 fixture 셋
(`Tools/regression/fixtures/browser-thumbnails`)을 뿌리에 올려 축소·통과·실패 세
경로를 만들고 `finally` 에서 지운다.

무효화·축출은 프로세스를 다시 띄우면 자극할 수 없다(캐시가 프로세스 안에 있다).
그래서 한 회차 안에서 단을 셋으로 나누고, 장부 A 직후에 `scene.save` 로 표지 파일을
만들게 해서 **에디터가 도는 중에** 원본을 다시 쓴다. 결과 JSONL 은 줄마다 flush
되지만 에디터가 독점으로 열고 있어 밖에서 읽을 수 없다 — 동기점을 거기 걸려다
sharing violation 으로 한 번 죽었다.

런타임이 못 보는 것 셋은 소스로 잰다. ① 어느 스레드에서 디코딩하는가(메인에서
풀어도 그림도 장부도 같다) ② 예산 창구가 실물인가 ③ Vulkan 팔이 배선돼 있는가.

##### 변이 다섯, 다섯 다 잡았다

| 변이 | 잡은 축 | 나온 문장 |
|---|---|---|
| M1 `Prime` 제거 | 런타임 | `ready 0 · awaitingUpload 2 · servedThumbnails 0` |
| M3 같은 경로 무효화 제거 | 런타임 | `원본을 다시 썼는데 무효화가 0 건이다` |
| M5 예산 비교를 상수로 | 런타임+소스 | `예산이 50331648 다 — budget 40000 이 먹히지 않았다` |
| M6 Vulkan 팔 은퇴 | 소스 | `ImGuiVulkanShell.cpp 에 IsTextureReady 가 없다` |
| M7 동기 디코딩 자리 추가 | 소스 | `thumbnail_run_generator 호출이 2 자리다` |

★ **M6 은 처음에 통과했다.** 검사가 `IsTextureReady` 를 부분 문자열로 찾아
`IsTextureReadyRetired` 를 배선으로 셌다. 경계(`(?![A-Za-z0-9_])`)를 세우고 다시
돌려 잡았다 — 이름이 살아 있는 것과 **그 이름이 그것인** 것은 다르다.

`lateDropped`(늦은 완료 폐기)는 기록만 하고 판정하지 않는다. 진행 중인 작업이 있는
바로 그 순간에 무효화가 닿아야 서는 축이라 프레임 단위로 확률적이다. **자극하지
못한 것을 초록으로 적지 않는다.**

##### 남은 것

- **모델 렌더 썸네일**(`.fbx`·`.gltf`·프리팹의 오프스크린 렌더). 지금 생성기는 이미지
  디코딩 하나뿐이고, 모델은 유형 아이콘에 머문다.

  **정찰 완료(2026-09-16) — 산정 4~5일.** 코드는 쓰지 않았고 전문은
  `docs/analysis/EditorIconSelectionStudy.md` §모델 렌더 썸네일 정찰에 있다. 요지:
  draw 밀봉도 재질 변환도 **씬을 요구하지 않는다**(`BuildRHIModelMeshView` 는 inline
  자유 함수고, 재질은 `ModelSceneInstantiation.cpp:160-167` 의 네 줄이 이미 한다).
  ★ 썸네일 패스는 **라이브 프레임 그래프 안**에 들어가야 한다 — 재질 밀봉이
  `EnhancedFrameContext`·`EnsureShaderMetaVariant`·`sceneEpoch` 에 밀착돼 있다.
  그 덕에 "프레임 밖 패스의 펜싱" 이라는 유일한 큰 위험이 사라졌다.
  ★ 결과는 **구워서 `Library/Thumbnails/` 에 둔다**(`.meta` 는 추적 대상이라 안 된다).
  구운 뒤에는 그냥 PNG 라 지금 도는 `generator::texture` 가 그대로 판다 — 런타임
  경로 추가분이 0 이고, **게이트가 결정적이 되어 아래의 픽셀 골든 제약이 사라진다.**
  ★ GPU 잔류(공유 핸들)는 가능하지만 하지 않는다 — Vulkan 은 라이브 뷰조차
  리드백이고, 굽기로 가면 CPU 픽셀이 필요해 잔류의 이점이 사라진다(대체재다).
  남은 미지수 하나: 썸네일 재질의 셰이더가 그 프레임의
  `EnhancedShaderMetaFrameSnapshot` 에 없다 — 착수 첫날에 판가름 난다.
- 서브에셋 축(`subasset`)은 키에 자리만 있고 항상 0 이다. 텍스처 아틀라스·머티리얼
  슬롯이 생길 때 채운다.

### W8 — 통합 회귀와 legacy 강제 배치 은퇴 (P0, 2일)

**2026-09-13 외관 기준은 현재 승인 상태로 고정한다. 완료 gate 는 DX12 이고, 그것이 이 페이즈의 backend 전부다.**
새 시안 제작 대신 현 상태의 자동 golden·회귀를 만든다. 완료된 개별 검증은 유지하되 전체 통합과 구분한다.

- DX12, DPI, restart, damaged ini, Play 왕복, Game Preview, preset matrix를 자동화한다.
  W2-I의 인스펙터 배치·Transform 2안, W2-V의 연속 resize·오버레이 입력 분리,
  W2-B의 내부 분할·탐색/검색·New·자산 선택/drag-drop을 함께 판정한다.
- 3D 씬 렌더 영역을 제외한 chrome crop visual golden을 만든다. 씬뷰 전체를 제외하지 않고
  W2-V의 툴바·방향 기즈모·HUD를 포함한다. FPS 등 변동 숫자는 마스킹하되 표시 영역의 경계·배치는 검사한다.
- 이미 제거된 `BuildInitialDockLayout`의 ContentsBrowserStyle 분기가 재유입되지 않는지 확인하고, 핵심 ToolPanel `NoMove` 잔재를 정리한다.
- ImGui internal adapter version canary를 CI에 넣는다.

**판정:** DX12에서 검증 레이어 오류/비정상 종료 0, layout·Play·시각·성능 gate 통과 후에
PHASE 21을 완료로 표시한다. 다른 backend 는 이 판정에 들어오지 않는다 — 대상이 아니다.

#### W8-1 착지 — 어댑터 판 canary와 legacy 잔재 (2026-09-15)

W8 의 네 줄 중 둘을 닫는다. 나머지 둘(통합 행렬, chrome crop visual golden)은 W2 계열이
아직 `progress` 라 그 뒤에 선다.

**계획서가 지목한 legacy 둘은 이미 죽어 있었다.** `ContentsBrowserStyle` 은 소스 전체에서
0 건이고, `NoMove` 는 중앙 뷰포트 선언 하나뿐이며 그것은 의도된 것이다(끌어 옮기면 중앙
노드가 빈다 — 창 자가 검증이 이미 그 성질을 단정한다). 남은 일은 **다시 들어오지 못하게
막는 것**이었고, 그래서 이 조각의 legacy 축은 삭제가 아니라 게이트다
(`plan-target-may-be-already-dead` 가 말하는 그 자리다).

**진짜 구멍은 다른 데 있었다.** `IMGUI_CHECKVERSION()` 은 **출하 구성에서 힘이 0**이다.
그 안은 `IM_ASSERT` 로만 말하는데 그것이 `assert()` 이고 Release 는 `NDEBUG` 라 통째로
사라진다. 게다가 매크로는 `bool` 을 돌려주는데 호출부가 값을 버리고 있었다. 즉 헤더와
라이브러리가 갈려도 아무 일도 일어나지 않는다.

이것이 추상적인 위험이 아닌 이유: 이 기계에는 **imgui 설치본이 둘이고 판이 다르다**
(전역 classic 1.91.7 · 매니페스트 1.92.8). 헤더를 한쪽에서 라이브러리를 다른 쪽에서
가져오면 `ImGuiIO` 배치가 갈린 채 컴파일이 통과하고 기동에서야 엉뚱하게 죽는다.

고친 것 둘:
- 반환값을 받아서 **던진다.** 배치가 갈린 채로 에디터가 뜨는 일이 없다.
- `ImGui::GetVersion()` — **도는 코드가 말하는 판** — 을 스냅샷에 싣고 `editor.dock` 이
  헤더 매크로와 함께 내보낸다. 전처리기가 박은 값은 헤더만 증언하므로, 링크된 것이
  무엇인지는 실행해서 물어야 안다.

**게이트.** `verify-imgui-adapter-canary.ps1` — 29 단정, run-all 배선.
- 소스에 흩어진 판 상수를 **전부** 뽑아 맞댄다(`static_assert(IMGUI_VERSION_NUM == N)` 과
  `expected_imgui_version_num`). 한쪽을 게이트 파일에 적어 두면 제품이 판을 올릴 때
  게이트가 조용히 낡는다 — W6-2 에서 `CreatorWorkspace 1` 이 실제로 그랬다.
- 유도한 집합의 **이름을 전부 찍는다.** 대조가 맞았다고 대상이 온전한 것은 아니다.
- `static_assert` 가 **컴파일되는 `.cpp`** 에 있고 그 파일이 `Editor.vcxproj` 에 실려
  있는지 본다. 헤더에만 있고 아무도 포함하지 않으면 빌드를 못 멈춘다.
- 판 문자열 `1.92.8` 에서 번호 `19280` 을 **따로 유도해** 검산한다.
- legacy: `ContentsBrowserStyle` 0 건, `no_move` 를 실은 선언은 전부 `central`,
  `default_traits` 도 중앙 아닌 역할에 그 성질을 흘리지 않는다.

**재지 못한 축 하나.** 감사가 빈 값을 "같다" 로 읽게 만드는 변이는 초록이다. 게이트가
감사 플래그만 보는 것이 아니라 헤더·런타임 문자열을 **직접** 맞대기 때문이고, 진짜 판
불일치를 만들려면 설치본을 섞어 빌드해야 하는데 하네스가 할 수 없다. 이 초록은 게이트의
구멍이 아니라 게이트가 감사보다 앞에 서 있다는 뜻이다.

**남은 W8.** DX12·DPI·restart·damaged ini·Play 왕복·Game Preview·preset 행렬의 통합
자동화와, 3D 렌더 영역을 제외한 chrome crop visual golden이다. 후자는 **스크롤한
Hierarchy 상태를 반드시 포함해야 한다** — W7 의 clipper 보폭 불일치는 스크롤해야만
드러나고 스크롤 0 픽셀 대조는 그것을 통째로 못 본다.

#### W8-2 착지 — chrome crop visual golden과 Hierarchy 끌어오기 (2026-09-15)

W8 의 셋째 줄을 닫는다. 남는 것은 통합 행렬 하나다.

**마스킹에 필요한 제품 변경은 없었다.** 착수 전 정찰에서 "씬 클립 사각형이 어디에도
게시되지 않는다" 고 적었는데 틀렸다 — W4 가 이미 `editor.sceneview` 로
`imageMin/Max · clipMin/Max · 툴바 상자 둘 · 기즈모 원반`을 전부 내보내고 있었다.
그래서 게이트는 **좌표를 한 줄도 제 파일에 적지 않는다.** 가릴 자리(3D 이미지
사각형)와 남길 자리(계획서가 포함하라고 한 툴바·방향 기즈모)를 둘 다 제품이 게시한
값에서 읽는다.

**대신 스크롤에 닿을 길이 없었다.** 선택이 스크롤을 끌지 않았고 스크롤 CLI 도 없다.
그래서 제품 변경 둘을 함께 넣었다.

- **선택한 줄을 화면 안으로 끌어온다.** `HierarchyWindow` 헤더에
  `m_requestScrollToSelection` 이 **선언만 되어 있고 읽는 곳도 쓰는 곳도 0** 이었다 —
  이름만 있고 동작이 없던 자리다. clipper 는 화면 밖 줄을 밟지 않으므로
  `ImGuiListClipper::IncludeItemByIndex` 로 그 줄을 따로 청구하고, **그 줄을 그리는
  자리의 커서**에서 스크롤을 정한다(보폭을 다시 계산하면 clipper 의 보폭과 두 벌이
  된다). 이미 온전히 보이면 흔들지 않고, 선택이 **바뀐** 프레임에만 선다.
- **조상을 펼친다.** 조상이 하나라도 접혀 있으면 그 줄은 목록에 **아예 없다** —
  스크롤로는 닿을 수 없다. 접힘 판정식은 `Rebuild` 와 `ExpandAncestors` 가
  `IsSlotExpanded` 한 함수를 공유한다(두 벌이 되면 한쪽만 고쳐져도 조용하다).

**게이트.** `verify-editor-chrome-golden.ps1` — 21 단정, 회차 4, run-all 배선.
회차는 선택 없음 · 목록 아래쪽 선택 · **같은 상태 재촬영** · 접힌 조상 밑의 선택이다.
재촬영 회차는 골든 파일을 갖지 않는다 — 한 실행 안에서 같은 상태가 두 번 다르게
찍히면 골든 대조가 무엇이 나오든 뜻이 없으므로, 그 축을 골든보다 **앞에** 세웠다.
회차끼리 **달라야 한다**는 단정도 둔다(같으면 골든이 초록이어도 그 축은 자극되지
않은 것이다). 환경이 골든과 다르면 틀렸다고 말하지 않고 **건너뛰되 수를 찍는다.**

**표지는 `--result-file` 로 못 삼는다.** 그 파일은 `_wfopen_s` 로 배타로 열려 도는
동안 밖에서 읽히지 않는다. 처음 두 번의 시도가 전부 **종료 뒤의 파일**을 읽어,
"선택 전" 이라고 찍은 그림에 이미 선택이 들어 있었다. 지금은 `scene.save` 가 만드는
**파일의 존재**가 표지다. 프로젝트 트리 밖에 쓰고, 회차마다 **같은 파일 이름**을 다른
폴더에 쓴다 — `scene.save` 는 씬 이름을 파일 이름으로 바꾸고 그 이름이 Hierarchy
머리 행에 그려지므로, 이름이 회차마다 다르면 그 행 하나 때문에 회차 대조가 저절로
달라져 단정이 동어반복이 된다.

**★ 자가 두 번째로 틀려 있었다.** `PrintWindow` 는 **창 전체**(비클라이언트 프레임
포함)를 DC 원점부터 그리는데 하네스가 **클라이언트 크기** 비트맵을 주고 있었다.
그래서 왼쪽·위로 프레임 두께(배율 1.5 에서 9~10 px)만큼 밀린 그림이 담기고
오른쪽·아래가 잘렸다. 눈으로는 티가 안 나고, ImGui 좌표로 계산한 가리기가 통째로
밀린 채 **초록**이었다 — 씬 이미지 오른쪽 끝이 764 인데 `editor.renderscale` 만
바꿔도 x=773 까지 화소가 변했고 774 부터는 하나도 변하지 않았다. 창 크기로 찍고
`ClientToScreen` 오프셋으로 잘라 내도록 고쳤다(공용 `capture-window.ps1` 도 같이).
고친 뒤에는 화소 764~765 가 테두리이고 766 부터 패널 배경이다 — `editor.dock` 이
적는 Hierarchy 창 `x0=766` 과 정확히 맞는다.

**변이 6 종.** 다섯이 붉었고 하나는 자극 불가다.
- 끌어오기 제거 · `IncludeItemByIndex` 제거 → `revealed` 골든이 붉다.
- 조상 펼치기 제거 → `nested` 골든이 붉다(그 줄이 목록에 아예 없다).
- 게이트가 툴바까지 가리게 하면 붉다 — 계획서가 포함하라고 한 그 자리가 실제로
  판정에 들어 있다는 증거다.
- 3D 그림을 안 그리면 붉되 **가린 자리 때문이 아니라** 반투명 툴바와 기즈모 원반이
  뒤의 그림을 섞어 들이기 때문이다. 차이가 씬 이미지 **안쪽**에서 난다.
- **자극 불가 하나:** 매 프레임 끌어오게 만드는 변이(과잉)는 초록이다. 하네스가
  스크롤을 굴리지 않아 과잉 끌어오기가 그림에 나타날 자리가 없다.

**남은 W8.** 없다 — W8-3 이 통합 행렬을 닫았다.

---

#### W8-3 착지 — 통합 행렬, 그리고 판정이 잴 수 없는 것이었다 (2026-09-15)

W8 의 마지막 줄을 닫는다.

**축마다 게이트는 이미 있었다.** resize 는 `verify-editor-viewport-extent`, preset 은
`verify-editor-layout-preset`, 재시작·손상 ini 는 `verify-editor-workspace`, DPI 는
`verify-editor-theme`, Play 왕복은 `verify-play-roundtrip`. 그래서 착수 전에 이 조각을
"이미 있는 것들을 한 번 더 도는 일" 로 적어 두었는데, 그것이 아니었다.

**★ 판정문이 출하 구성에서 잴 수 없는 것이었다.** W8 의 판정은 *"현재 범위인 DX12 에서
검증 레이어 오류/비정상 종료 0"* 이다. `CREATOR_DX12_VALIDATION=basic` 은 Release 에서도
디버그 레이어를 켠다 — 실측으로 `[DX12 검증] DebugLayer=on` 이 두 줄 찍힌다(셸과 라이브
렌더러가 각각 디바이스를 만든다). 그런데 `DX12DeviceResources::DrainDebugMessages` 가
**통째로 `#if defined(_DEBUG)`** 였고, 라이브 경로의 호출부 둘(`ImGuiDx12Shell` 의 프레임
끝, `EnhancedSceneRenderer` 의 슬롯 은퇴)도 같은 가드 안이었다. **레이어를 켜 놓고 아무도
큐를 읽지 않았다.** 디버그 레이어의 비용만 내고 판정은 못 하는 상태였고, 그 위에서 "오류
0" 을 말하면 빈 집합을 성공으로 읽는 것이다 — W8-1 의 `IMGUI_CHECKVERSION()` 과 **같은
모양의 결함**이다(출하 구성에서 힘이 0 인 검사).

**제품 변경.**

- `Engine/RenderEngine/RHI/RHIValidationLedger.{h,cpp}` — 프로세스 범위 장부.
  `layerEnabled · mode · devices · drains · messages · problems · retained[] ·
  droppedMessages`. **세는 자리를 `DrainDebugMessages` 안 한 곳으로 모은다** — 큐를 비우는
  길이 그 함수뿐이므로 여기로 들어오지 않는 메시지는 애초에 없다. 호출부마다 세면 어느
  자리가 안 세는지 알 수 없다(셸과 라이브 렌더러는 **서로 다른 InfoQueue** 를 갖는다).
- `DX12DeviceResources` — `ID3D12InfoQueue` 를 **들고 있는다**(매 프레임 QueryInterface 를
  없애고, "큐가 있는가" 를 밖에서 물을 자리를 만든다). `DrainDebugMessages` 의 `_DEBUG`
  가드를 걷었다 — 꺼진 실행의 비용은 **포인터 하나 검사**다. 초기화 끝에서
  `declare_layer` 로 **실물**(큐를 얻었는가)을 선언한다. 환경 변수가 basic 이라고 적혀
  있어도 큐를 못 얻었으면 그 실행의 "오류 0" 은 아무것도 뜻하지 않는다.
- 라이브 드레인 호출부 둘의 `_DEBUG` 가드 제거.
- `dx12.validation [reset]` — 장부를 읽는다. `problems != 0` 이면 명령 자체가 실패한다.
  수만 내면 "몇 건" 만 알고 **무엇인지** 모르므로 문구를 최대 16 줄 함께 싣는다.
  `reset` 은 **수만** 비운다 — 레이어 선언은 그 실행의 성질이라 구간마다 달라지지 않는다.

**게이트.** `verify-editor-integration-matrix.ps1` — 214 단정, 축 12, 에디터 기동 11 회,
run-all 배선. 세션은 표면 왕복(한 세션 안에서 resize 3 회 → Game Preview 왕복 → Play
왕복) · preset 5 종 순회 · 재시작 2 회차(폴더를 일부러 공유한다) · 손상 ini 2 벌 ·
DPI(user scale 1.0/1.5) · 대조군 1.

**눈먼 초록을 막는 장치 셋.**

1. **판정은 넷이 함께 서야 성립한다** — `layerEnabled == true` · `mode == basic` ·
   `drains > 0` · `problems == 0`. `problems == 0` 하나만 보면 레이어가 꺼진 실행도 통과한다.
2. **대조군 세션.** 같은 이진·같은 스크립트, 환경 변수만 `CREATOR_DX12_VALIDATION=off`.
   여기서 `layerEnabled` 가 거짓이고 `drains` 가 0 이어야 한다. 이것이 없으면
   `layerEnabled` 가 늘 참인 상수여도 게이트는 초록이다.
3. **축마다 `reset`** — 문제가 났을 때 "어딘가에서 났다" 가 아니라 **어느 축**인지 말한다.

**★ 변이를 설계하다 게이트의 구멍을 먼저 찾았다.** 처음엔 축마다 `reset` 으로 시작했는데,
그러면 **부팅 구간**(디바이스 생성 · 스왑체인 · 파이프라인 상태 물체 · 셰이더 적재)의
오류를 첫 `reset` 이 지워 버린다 — DX12 오류가 가장 잘 나는 구간이다. `reset` **앞의**
독서(`surface/boot` · `preset/부팅`)를 축으로 세웠고, 실제로 변이 ③이 거기서 잡혔다.

**변이 4 종, 넷 다 잡았다.**

| 변이 | 자리 | 결과 |
| --- | --- | --- |
| ① `DrainDebugMessages` 를 다시 Release 에서 무력화 | `DX12DeviceResources.cpp` | `surface/boot : 큐를 한 번도 비우지 않았다(drains=0)` |
| ② `declare_layer` 가 끈 실행도 켠 것으로 적음 | `RHIValidationLedger.cpp` | `control-off: 레이어를 껐는데 layerEnabled 가 참이다` |
| ③ 디바이스 생성 직후 UPLOAD 힙 자원을 `RENDER_TARGET` 상태로 | `DX12DeviceResources.cpp` | `surface/boot` — problems **2** 건, retained 에 해당 ERROR 두 줄(디바이스가 둘이라 두 번) |
| ④ ③ 위에 `Cmd_dx12_validation` 의 `Fail` 길을 닫음 | `RenderDebugCommands.cpp` | 게이트 자신의 `problems == 0` 이 따로 붉었다 |

③과 ④는 **양 팔을 따로 친 것**이다 — 제품이 실패로 보고하는 길과 게이트가 수를 읽는
길이 각각 서 있는지 본다. 한쪽만 치면 다른 쪽이 죽어도 초록이다.

**실측(둘 다 문제 0).** Release·Debug 양쪽에서 12 축 전부 `problems=0 · messages=0`,
종료 코드 0. 축마다 `drains` 는 113~321 로 프레임 수를 따른다. 즉 **에디터의 이 경로들에는
실제로 DX12 검증 오류가 없다** — 이제 그것이 측정된 문장이다.

**못 잡는 것.** 픽셀(chrome golden 의 몫), 실제 모니터 DPI(하네스가 못 바꾼다),
마지막 독서 **뒤** 해체 중에 나는 오류. (다른 backend 는 못 잡는 것이 아니라 대상이
아니다 — 에디터는 DX12 로 뜬다.)

**CLI 표.** `dx12.validation` 이 새 명령이라 `cli_registry.golden.tsv` 를 갱신했다
(122 → 123 · 이름 131 → 132).

---

## 10. 의존성과 병행

```text
W0 → W1 → W2 → W2-I
          W2 + W4 canvas 정본 → W2-V
          W2 + M1 → W2-B  (B3 목록 연결은 W7 스냅샷 계약을 선행)
W0 → W3 → W4 → W5
          W4 → W6
H3 + W0 ─────→ W7
W2 + W2-I + W2-V + W2-B + W5 + W6 + W7 → W8

M0 → M1 → M2          (부록 A. 2026-09-11 셋 다 착지)
M1 ──────→ W3         (Window 메뉴 재열기 계약의 선행. 선행 충족)
M2 ──────→ W0 canary  (여섯째 관측 커맨드 editor.menu를 승계한다. 그 커맨드는 섰다)
W0 ──────→ W1·W3      (2026-09-11 전량 착지. 관측 다섯 · 크롬 게이트 · fixture 여섯 · 기준선)
W5 ──────→ editor.viewport  (읽을 committed·input owner 신호를 W5가 만든다. §1.9)

M3 ──────→ M4 → W3    (부록 B. M3 착지 완료. M4가 창 선언을 세우고 W3이 그 위에 ID를 얹는다)
M3 ──────→ W0 기준선  (2026-09-11 떴다. 다만 **골든은 아니다** — 시작 크기와 리사이즈가
                       닫히기 전의 그림이라 W8이 다시 뜬다)
M3 ──────→ W1 재계수  (2026-09-11 다시 셌다. 91 → 57. W1 항목에 반영)
```

**권장 착수 순서(2026-09-10).** 의존만 보면 여러 배열이 가능하지만, 아래 순서가 같은 파일을 두 번
헤집지 않고 golden을 한 번만 뜬다.

| 순 | 슬라이스 | 이 자리인 이유 |
|---|---|---|
| 0 | **M3 — 셸 크롬 (착지 완료)** | 제목표시줄·배치·스킨을 함께 정리한다. 최초에는 별도 재생 행을 뒀으며, 2026-09-12 사용자 결정으로 재생 버튼을 최소화 버튼 앞의 박스로 옮기고 해당 행을 제거했다. 독스페이스는 작업 영역 크기를 사용한다. 창 본문은 건드리지 않아 W3의 대상 수는 줄지 않는다 |
| 1 | **W0 전반 — 관측 커맨드 · 크롬 게이트 (착지 완료)** | 순서가 실제로는 거꾸로 돌았다 — `editor.windows`·`editor.menu`가 먼저 나서 TSV 관례를 **그 둘이 정했고**, 여기서 선 셋이 그 형식을 따랐다. 결과는 같다(형식이 하나). 순회 결정화는 M4가 펌프를 은퇴시키며 먼저 해소했다. `editor.viewport`는 읽을 신호가 없어 W5로 갔다 |
| 2 | **M0 — 선언 어휘와 목록 배관 (착지 완료)** | 새 폴더뿐이라 동작 변화 0. 1과 파일을 공유하지 않아 **병행 가능** |
| 3 | **M1 — registry와 그리기 배선 (착지 완료)** | 메뉴 구조를 바꾸는 마지막 슬라이스. 빈 뿌리를 그리지 않으므로(부록 A.6) 픽셀 중립이고, **W1보다 먼저** 두어야 `MenuBarWindow.cpp` 2,716줄을 구조와 토큰으로 두 번 헤집지 않는다 |
| 4 | **M2 — 게이트와 `editor.menu` (착지 완료)** | M1의 표가 있어야 덤프할 것이 생긴다. W0 canary가 쓸 여섯째 커맨드를 여기서 낸다 |
| 5 | **W0 후반 — fixture · inventory · screenshot · 성능 기준선 (착지 완료)** | "golden을 한 번만 뜬다"는 전제가 **틀렸다.** 시작 창 크기를 고를 수 없고(`App.cpp` 하드코딩) 런타임 리사이즈가 배치를 재배열하므로, 여기서 뜬 캡처는 골든이 될 수 없다. 기준선으로만 남기고 golden은 W8이 뜬다. fixture는 예상대로 여섯 벌 다 게이트에 물렸다 |
| 5.5 | **M4** — 창 선언(부록 B.3) | M3이 셸에 프레임 소유를 준 뒤라야 선언에 담을 것이 정해진다. W3보다 **먼저**여야 한다 — W3의 안정 ID는 선언의 한 필드가 되고, 표시 상태 저장소 셋을 합치는 자리도 여기다. W0 후반 골든 뒤에 두어 골든을 두 번 뜨지 않는다 |
| 6~ | W1 → W2 → W2-I·W2-B, W3 → W4 → W5·W6, W2 + W4 canvas → W2-V, W7, W8 | W2-I는 인스펙터, W2-V는 씬뷰 배치, W2-B는 브라우저 탐색/생성을 담당한다. W4 canvas와 W7 목록 계약을 먼저 공유한다. W3의 선행 M1·M4는 이미 끝났으며 공유 파일 편집을 조정한다 |

**(2026-09-11 갱신)** 1~5.5와 W0 후반이 착지했고, W1 구현·자동 회귀와 실행 중 사용자 배율
왕복을 확인했다. W1의 Material Symbols 적용·Live Code 자리표시자 제거는 착지했고 실제 OS DPI
100↔150% 왕복 검증을 남겨 `progress`로 유지한다. W7에는 비동기 Browser 썸네일 범위를 추가했다.
W2 후속에는 W2-I·W2-V·W2-B를 추가했다. W3 이후 작업과의 의존·공유 파일 조정은 위 그래프와 §9를 따른다.

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
| backend | DX12(에디터의 backend 는 이것 하나다) | presentation key 오류, validation error, crash |
| mode | Edit, Entering, Play, Pause, Eject, Stop | wrong target, gizmo/game input 동시 활성 |
| focus | click panel, Alt-Tab, modal, Game Preview | cursor lock 잔존, focus stealing |
| scale | Hierarchy/Browser 1k/10k/50k | 빈 측정, semantic mismatch, p95 회귀 |
| style | normal/hover/active/focus/disabled/error | token drift, nav/focus 정보 소실 |
| Inspector 배치 (W2-I) | content 폭 240/320/480/720 logical px, 전환 경계, 배율 100/125/150/200%, 긴 필드·중첩·Import Settings | 가로 잘림, 열 불일치, 읽을 수 없는 숫자 폭, 보조 버튼 유실, 편집 중 ID/Undo 손실 |
| 공간 컴포넌트 (W2-I, 2안) | 일반·Empty·UI·Canvas 두 모드, 개별 조작·엔티티 활성 왕복, 저장/재로드 | 인스턴스당 중복 헤더/본문, Canvas 공간 정보 누락, 개별 비활성화 허용, 엔티티 활성 전이 훼손 |
| 씬뷰 배치 (W2-V) | 폭 320/480/720/1024·높이 180/320/640 logical px, 배율 100/125/150/200%, 원점 이동·100회 이상 연속 리사이즈 | 좌우 정렬 불일치, 잘림·기즈모/HUD 겹침, 도구 유실, 전환 진동, 표시/hit rect 불일치 |
| 씬뷰 입력 (W2-V/W5) | 누른 채 resize, 팝업·텍스트·기즈모·drop·terrain·카메라, focus loss, Play/Eject | 클릭 관통, UI/씬 중복 소비, 다른 명령으로 release 전달, 게임 입력 침범 |
| Browser 배치/탐색 (W2-B) | 폭 320~1440 logical px·배율 100~200%, 내부 splitter·접기·긴 경로·루트/Up·뒤로/앞으로·workspace 복원 | 고정 폭/잘림, 검색/경로 유실, 트리와 목록의 위치 불일치, 다른 프로젝트 이력 재사용 |
| Browser 생성/검색 (W2-B/W7) | New/우클릭·취소/실패·늦은 완료, 폴더만 있는 위치, 최근/전체·유형·정렬·목록/타일·동명 자산 drag | 생성 경로/GUID 오류, 빈 결과 원인 은폐, 선택/신원 손실, 중복 탐색 cache·매 프레임 전체 순회 |

필수 자동화 후보:

- `Tools/regression/verify-editor-workspace.ps1` — **구현됨**. W3 migration·저장/복구 계약까지 실어 6 fixtures/212 checks다(에디터 16회 기동). 형식만 보는 `verify-editor-workspace-storage.ps1`(169 checks)을 앞에 두어 형식 결함을 에디터를 띄우기 전에 거른다. 둘 다 run-all에 있다. W4 central 계약(Host 비폐쇄·모드 재시작 왕복·보이지 않는 타깃 미생산)은 W4-①②③ 구역으로 들어갔다.
- `Tools/regression/verify-editor-theme-golden.ps1` — 신규
- `Tools/regression/verify-editor-viewport-mode.ps1` — 신규
- `Tools/regression/verify-editor-viewport-overlay.ps1` — W2-V 신규 후보. 명령별 배치·표시 모드·입력 소비 관측과 실제 입력/리사이즈 주입을 선행한다.
- `Tools/regression/verify-editor-content-browser.ps1` — W2-B 신규 후보. 내부 rect·탐색/검색 상태·결과 신원·생성 완료 관측과 조작 주입을 선행한다.
- 기존 `verify-play-roundtrip.ps1` — **W5 확장**. 첫 실행(stdout 정규식)에 committed·state·owner·target·foreground
  단정을 얹고, 두 번째 실행(jsonl, 순서 목록)이 실패 주입·소유자 9표본·커서·타깃 복원·확정 전 거부를 잰다.
  `verify-play-selection-undo.ps1` 판정 D는 복원을 단정한다. `cli_registry.golden.tsv` 113→118.
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
| **DPI 자체 구현이 ImGui 1.92 경로와 이중화** | `FontScaleMain/FontScaleDpi` 채택, obsolete 소스 게이트와 실행 HWND PMv2·DPI 일치 검사(§3.2) |
| **`WantCapture*` 강제를 걷자 입력 라우팅이 바뀜** | W5에서 계측 먼저, 지혈은 그 뒤. 라우팅 근거를 문서화한 뒤 걷는다(§1.8) |
| **UI 커서 모양 복원을 게임 캡처 해제로 오인** | 모양 변경 제한은 제거됐지만 lock/clip/visibility는 W5 소유권에 남는다. 해제 경로 4종을 각각 단정(§1.8) |
| **씬뷰 resize 때 도구가 겹치거나 클릭이 씬으로 관통** | W2-V의 실측/overflow·기즈모/HUD 공간 예약과 동일 프레임 hit rect, press/release 소유권·연속 resize 검증 |
| **Browser 외부 docking만 고치고 내부 고정 폭/탐색 부재를 남김** | W2-B 내부 splitter·경로/이력·New·검색 범위의 별도 완료 기준, W3/W7와 공동 검증 |
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
   인스펙터 전 경로는 W2-I의 공통 열·폭별 줄 전환 규칙을 사용한다. Transform 계통은 2안에 따라
   실제 컴포넌트별로 상단에 한 번만 표시하며 개별 비활성화·제거 제한과 엔티티 전체 활성 전이를 보존한다.
4. `ViewportHost`는 central에 항상 존재하며 닫거나 ToolPanel로 대체할 수 없다.
   씬뷰 툴바·방향 기즈모·HUD는 W2-V의 공통 canvas 배치를 사용하며 창 이동·연속 resize에도
   정렬·도달성·입력 분리를 유지한다. W4의 이미지/기즈모/picking과 좌표 정본이 같아야 한다.
5. Hierarchy/Inspector/Browser/Console/Profiler/Game Preview는 자유롭게 dock/close/reopen된다.
   Browser는 W2-B의 내부 폭 조절·경로/이력 이동·New·검색 범위/필터·보기 도구를 제공하며,
   W7 목록/썸네일을 소비해도 자산 선택·생성 대상·drag-drop 신원을 보존한다.
6. title/icon/번역 변화가 workspace identity를 깨뜨리지 않는다. **DockBuilder 지정 이름과 실제
   `Begin` 이름의 불일치가 0이며, 게이트가 이를 직접 단정한다**(§1.4의 현존 결함 청산).
7. clean/legacy/custom/corrupt layout의 save/load/reset/migration이 자동 검증된다.
8. Play는 중앙 Game canvas로, Eject는 Editor canvas로, Stop은 편집 상태로 돌아오며 gizmo/input/cursor
   owner가 겹치지 않는다. **스냅샷 실패 시 UI가 Playing으로 보이지 않는다**(§1.6).
9. optional Game Preview는 기존 두 표시 타깃을 재사용하고 두 번째 카메라 정본을 만들지 않는다.
10. theme/docking CPU 회귀가 §8 gate 안이며, single-view/clipping 이득은 실제 수치로 기록된다.
11. DPI, Play 왕복, visual golden, large-data 성능 gate가 모두 통과한다(backend 는 DX12 하나다).
12. `verify-imgui-obsolete-surface.ps1`이 통과하고(§3.2의 ABI 정정), `ViewportsEnable`은
    꺼진 채로 남아 있음을 게이트가 단정한다(§3.2, §1.7).
13. `editor.*` 관측 커맨드 5종이 서 있고, 에디터 chrome 회귀 3종이 **변이로 이빨을 증명한**
    상태로 CI에 있다(§1.9, §11).
14. 메뉴 등록 배선이 서 있다 — 동작 하나를 상단·팝업의 원하는 카테고리에 붙이는 데 필요한 편집이
    **선언 한 줄**이고, 표면 오타와 문맥 타입 불일치가 컴파일 오류이며, 목록 누락은 기동 게이트가
    잡는다(부록 A). `editor.menu` 덤프가 골든과 함께 CI에 있다.
    **(2026-09-11 충족 — 단, 골든은 아니다.)** 배선·덤프·게이트가 run-all 에 있고 변이로 이빨을
    증명했다(A.10·A.11). 덤프를 **골든으로 박지는 않았다** — 항목을 하나 더할 때마다 골든이 이유
    없이 붉어지면 사람이 숫자만 고치고 지나간다. 대신 불변식을 단정한다(경로 충돌 0 · 이름 없는
    항목 0 · 항목 0인 선언자 0 · 표면마다 그리는 자리 있음). 골든이 필요해지는 것은 W0 후반의
    canary가 "메뉴 불변식"을 dock 불변식과 함께 뜰 때다.
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
        inspector_component, behavior_tree_node, animator_node,
    };
}
```

**(2026-09-11 정정)** 처음에는 `scene_view`가 여기 있었다. M1이 배선하러 가 보니 **그 자리가
없다** — 씬 뷰의 오른쪽 버튼은 카메라 시점 조작이고 같은 버튼이 기즈모 단축키를 막는 변별자로도
쓰인다. 게이트를 통과시키려고 UI를 새로 만드는 것은 순서가 거꾸로여서 열거자를 뺐다. 자세한
근거는 A.10.

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
  **(2026-09-11 — 셋의 행방이 갈렸다.)** ①은 **원리적으로 불가능하다**: 목록이 등록의 유일한
  출처라 줄을 지우면 표와 기대치가 같이 줄고 프로그램 안에 기준이 없다. 잡히는 것은 "항목을 0개 낸
  선언자"이고, 줄을 지우는 쪽은 "선언자 0이면 붉다"는 하한이 막는다. ②는 **소스 대조**로 옮겼다 —
  그리는 자리는 C++ 호출 지점이고 팝업이 열려야 한 번 도는 코드라 표에 흔적이 없다. ③만 런타임
  감사가 본다. 자세한 것은 A.11.
- 변이로 이빨을 증명한다. 문맥 타입 불일치는 **컴파일 오류**이므로 런타임 게이트가 아니라 빌드 게이트로
  세고, 그 사실을 게이트 파일 머리에 적는다. 정책을 assert로만 적으면 Release에서 강제력이 0이 된다.

### A.6 그리기 배선과 스레드 규약

그리는 자리는 표면으로 물어본다. **(2026-09-11 착지)** 상단 여섯, 팝업 여섯 — 합 열둘이다.
`scene_view`는 자리가 없어 열거에서 뺐다(A.3 정정 · A.10).

| 표면 | 그리는 자리 | 부르는 것 |
|---|---|---|
| 상단 File·Edit·Settings·Window·Help | 각 인라인 메뉴의 `EndMenu` 직전 | `append_top_menu_items` |
| 상단 Tools(신설, 선언 전용) | `Settings` 뒤, `Window` 앞 | `draw_top_menu_root` |
| Hierarchy 팝업 | `HierarchyMenu` | `draw_popup_menu_items<hierarchy>` |
| Content Browser 폴더 둘 | `ContentFolderTreeMenu` · `ContentFolderAreaMenu` | `…<content_browser_folder>` |
| Content Browser 자산 둘 | `ContentAssetTreeMenu` · `ContentAssetTileMenu` | `…<content_browser_asset>` |
| Inspector 컴포넌트 팝업 | `ComponentMenu` | `…<inspector_component>` |
| 행동 트리 노드 팝업 | `NodeMenu`(MenuBarWindow) | `…<behavior_tree_node>` |
| 애니메이터 노드 팝업 | `NodeMenu`(AnimatorEditorWindows) | `…<animator_node>` |

Content Browser는 같은 이름 `"Context Menu"` 팝업이 **세 벌**이었다. ImGui 팝업 id 는 창과 id
스택에 묶이므로 타일 쪽에서 연 팝업이 트리 쪽 `BeginPopup`에 먼저 걸릴 수 있었다. 호스트별로
이름을 갈라 넷이 전부 구별된다(위 표). 기존 인라인 항목은 **Delete 하나만** 빼고 그대로 뒀다 —
그 예외의 근거는 A.7.

그리는 자리 이름이 게이트의 사정권을 정한다. 게이트가 소스에서 호출을 뽑아 열거자와 맞대므로
(A.11), 호출 자리마다 다른 지역 도우미를 쓰면 게이트가 그 이름들을 손으로 알고 있어야 한다.
그래서 "항목 0이면 구분선조차 넣지 않는다"는 규칙은 호출 자리가 아니라 **그리기 계층의 함수**
(`append_top_menu_items`)가 든다.

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
  **(2026-09-11 예외 하나.)** Content Browser의 `Delete`는 이관했다. 계획서가 실측으로 적어 둔
  결함이 정확히 그 항목이어서다 — 확인도 Undo도 없이 `file::remove`를 부르고, 그 코드가 **두 군데
  복제**돼 있었다(`:458`·`:531`). `confirm` 어휘를 둔 이유가 이 자리이고, 선언 하나가 복제 둘을
  지우고 확인을 더한다. 한 항목이므로 "일괄 이관 금지"와 다투지 않는다. 나머지는 그대로 남았다.

### A.8 슬라이스와 공수

새 폴더 `Editor/EditorMenu/`에 둔다. 유니티 청크가 같은 폴더끼리만 묶이므로
(`CombineFilesOnlyFromTheSameFolder`) 기존 청크 재편을 피한다. 레지스트리 구현부는 `Commands/` 전례대로
`IncludeInUnityFile=false`로 두어 각 TU가 자기 include를 소유하는지 매 빌드가 검증하게 한다.

| 슬라이스 | 내용 | 공수 |
|---|---|---|
| M0 | `editor::` 선언 어휘(표면 열거·문맥 특성·짧은 별칭·수정자)와 중앙 목록 배관 | 1일 **(착지)** |
| M1 | registry와 그리기 배선 12곳, Content Browser 팝업 세 벌 통합 | 1.5일 **(착지)** |
| M2 | 부팅 등록, 누락·충돌·미배선 게이트, `editor.menu` 덤프와 골든, 변이 증명 | 1일 **(착지)** |
| | 합계 | **3.5일** |

M0은 W0과 병행 가능하고, **M1은 W3의 선행**이다. W3의 close/reopen 계약은 이 표가 서기 전에는
하드코딩 메뉴를 하나 더 만드는 것으로 끝난다.

**(2026-09-11)** 셋 다 착지했다 — 기록은 A.9(M0) · A.10(M1) · A.11(M2). W3의 선행이 충족됐다.

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
   **(2026-09-11 폐기됨 — M1이 고쳤다.)** 이 전제는 검사를 부팅 전 한순간에만 돌 수 있게 만들었고,
   그 말은 도는 세트에 넣을 수 없다는 뜻이었다. M1이 목록을 채우자 예고대로 깨졌다. 이제 제품 표를
   옆으로 치우고(`stash_menu_registry`) 합성 선언 위에서 돌고 되돌린다. 부팅 순서 제약도 없다.

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

---

### A.10 M1 착지 기록 — 그리는 자리 열둘과 팝업 세 벌 통합 (2026-09-11)

**먼저 계약이 틀린 데가 하나 있었다.** A.3 은 팝업 호스트 일곱을 예정했는데, 배선하러
가 보니 `scene_view` 의 **그리는 자리가 없다.** 씬 뷰의 오른쪽 버튼은 카메라 시점
조작이고(`SceneViewWindow` 의 `IsMouseDown(Right)` → `EditorCameraRig::HandleMovement`),
같은 버튼이 기즈모 단축키를 막는 변별자로도 쓰인다. 오른쪽 클릭 컨텍스트 메뉴를 끼우면
카메라 조작과 다툰다.

게이트를 통과시키려고 UI 를 새로 만드는 것은 순서가 거꾸로다 — 열거자가 먼저 있고 자리를
짜 맞추는 것이 아니라, **자리가 있어서 열거자가 있는 것**이다. 그래서 열거자를 뺐고, 그리는
자리는 **열둘**이 됐다(상단 여섯 · 팝업 여섯). 씬 뷰 컨텍스트 메뉴가 필요해지면 그것은
ViewportHost(W4·W5)가 입력 소유권을 정리하며 할 결정이고, 그때 열거자 한 줄과 그리는 자리
한 곳을 더하면 된다.

**문맥 타입이 .cpp 경계에서 사라지지 않는다.** 그리기는 호스트마다 문맥 타입이 다른데
ImGui 를 아는 곳은 `EditorMenuDraw.cpp` 하나다(M4 가 창 쪽에서 쓴 구성과 같다). 보통 여기서
`void*` 나 `std::function` 으로 지우게 되고, 그러면 CT6-a 가 걷어낸 이중 타입소거가 새
소비자로 되살아난다.

지우지 않고 **잘랐다.** .cpp 는 그리기에 필요한 납작한 값(`menu_item_view`)만 받고 눌린
항목의 **색인**을 돌려준다. 그 색인으로 `invoke` 를 부르는 것은 타입을 아는 헤더의
템플릿이다. 함수 포인터는 선언된 타입 그대로 남고, 경계를 넘는 것은 `string_view` 와
`size_t` 뿐이다.

**확인을 같은 프레임에 끝낸다.** `confirm` 을 모달로 물으면 "눌렸다"와 "실행한다"가 서로
다른 프레임에 놓이고, 그 사이 문맥 값(자산 경로·엔티티 신원)을 들고 있어야 한다. 그 보류
저장소는 호스트마다 타입이 달라 한 군데 두려면 또 타입소거가 필요해진다. 그래서 하위 메뉴
한 겹을 씌워 **두 번 누르게** 만들었다 — 확인 문구가 보이고, 같은 프레임에 끝나므로 보류
상태가 아예 없다.

**정렬 키는 (부모 경로, order, 라벨)이다.** 부모 경로를 맨 앞에 두는 것이 요점이다: 같은
하위 메뉴의 항목이 반드시 인접해져서 중첩이 한 번만 열리고 한 번만 닫힌다. `order` 를 앞에
두면 같은 하위 메뉴가 두 번 열릴 수 있다. 그래서 `order` 는 계약대로 "같은 하위 메뉴 안
안정 정렬"이고(A.4), 하위 메뉴끼리의 순서는 경로순이다.

**팝업 세 벌 통합.** Content Browser 에 `"Context Menu"` 라는 **같은 이름의 팝업이 세 벌**
있었다. ImGui 팝업 id 는 창과 id 스택에 묶이므로, 타일 쪽에서 연 팝업이 트리 쪽
`BeginPopup` 에 먼저 걸릴 수 있는 상태였다. 호스트별로 이름을 갈랐다.

| 자리 | 새 이름 | 호스트 | 문맥 |
|---|---|---|---|
| 디렉터리 트리 | `ContentFolderTreeMenu` | 폴더 | 클릭한 폴더 |
| 타일 빈 영역 | `ContentFolderAreaMenu` | 폴더 | 지금 보는 폴더 |
| 파일 트리 | `ContentAssetTreeMenu` | 자산 | 클릭한 파일 |
| 파일 타일 | `ContentAssetTileMenu` | 자산 | 그 타일의 파일 |

**배선의 첫 쓸모는 복제를 지운 것이었다.** 계획서가 실측으로 적어 둔 결함 —
Content Browser 의 Delete 가 확인도 Undo 도 없이 `file::remove` 를 부르고 그 코드가 **두
군데 복제**돼 있다 — 를 여기서 고쳤다. `confirm` 어휘가 있는 이유가 바로 이 자리여서,
"기존 항목 이관 금지"(A.7)의 예외로 뒀다. 선언 하나가 복제 둘을 지우고 확인을 더한다.

**태운 항목은 열하나다.** 이관이 아니라 **배선이 실제로 쓸모를 낸 것**만 실었다.

| 무엇 | 몇 | 왜 |
|---|---|---|
| 신원 복사 | 7 | CLI 명령은 전부 신원을 인자로 받는데 화면에서 그것을 얻을 방법이 없었다. 정찰이 "명령 98 중 66 이 GUI 에 도달하지 않는다"고 셌던 간극의 가장 값싼 절반이다 |
| Tools 진단 | 2 | M4 가 만든 관측(`editor.windows`·`editor.selftest`)을 메뉴에서 부른다. 새 Tools 뿌리가 실제로 서는지도 이 둘이 증명한다 |
| 자산 삭제 | 1 | 인라인 복제 둘을 대체하고 확인을 더한다 |
| `.meta` 경로 복사 | 1 | `enabled` 술어가 제품에서 실제로 갈리는 유일한 자리(`.meta` 짝이 있을 때만) |

같은 "복사" 동작이 호스트마다 다른 것을 집는다는 점에서, 문맥 타입을 호스트가 정한다는
계약이 여기서 그대로 값을 한다.

**M0 이 예고한 대로 자가 검사가 깨졌다.** M0 착지 기록의 네 번째 항이 "M1 이 목록을 채우는
순간 전제가 깨진다"고 적어 두었고, 그대로 됐다. 검사는 "표가 비어 있을 때만" 돌 수 있었고
그 말은 부팅 전 한순간에만 돈다는 뜻이었다. `stash_menu_registry` / `unstash_menu_registry`
를 달아 제품 표를 옆으로 치우고 합성 선언 위에서 돌게 고쳤다 — 창 쪽이 M4 4단계에서 쓴
장치와 같고, **조기 반환 경로에도 되돌리기가 달려 있다.** 되돌리기 실패는 제품 메뉴를
없애는 일이라 판정 항목(⑥)으로 넣었다.

---

### A.11 M2 착지 기록 — `editor.menu` 와 표면 대조 게이트 (2026-09-11)

**런타임이 볼 수 없는 것을 먼저 갈랐다.** A.5 는 게이트 셋을 예정했는데 그중 **둘은 표에
흔적이 남지 않는다.**

| 예정 | 어디로 갔나 |
|---|---|
| ① 목록에서 선언자를 빼면 그 항목만 사라진 것을 잡는다 | 목록이 등록의 **유일한 출처**라 줄을 지우면 표와 기대치가 같이 줄어든다. 프로그램 안에 기준이 없다. 잡을 수 있는 것은 "항목을 0개 낸 선언자"(`silent_declarers`)이고, 줄을 지우는 쪽은 게이트가 "선언자 0 이면 붉다"로 막는다 |
| ② 표면마다 그리는 자리가 하나 이상 | 그리는 자리는 C++ 호출 지점이고 팝업이 실제로 열려야 한 번 도는 코드다. **소스 대조**로 옮겼다 |
| ③ 같은 표면 안 경로 충돌 0 | 런타임 감사가 본다(`path_conflicts`) |

②의 해법이 이 슬라이스에서 가장 값이 나갔다. 게이트가 `EditorMenuSurface.h` 의 열거자와
트리 전체의 `draw_popup_menu_items<...popup_host::X>` 호출을 **양쪽 다 소스에서 뽑아**
맞댄다. 손으로 적은 목록이 없다 — 두 벌이 되면 한쪽이 낡아도 아무도 모른다. 같은 이유로
상단 뿌리는 `append_top_menu_items` / `draw_top_menu_items` / `draw_top_menu_root` 호출과
**함께** 찾는다. `top_menu_root::x` 가 어디에 나오든 세면 자가 검사나 감사의 언급이 그리는
자리로 잡혀 대조가 공허해진다. 그리고 이 때문에 "항목 0 이면 구분선도 넣지 않는다"는 규칙을
호출 자리의 지역 도우미가 아니라 **그리기 계층의 함수**로 옮겼다 — 규칙이 제자리를 찾은
것이 게이트의 사정권을 넓힌 셈이다.

**기대치의 출처가 하나다.** `EDITOR_MENU_LIST` 를 두 번째로 소비해
`editor_menu_declarer_names` 배열을 만든다. 등록이 쓰는 출처와 감사가 쓰는 출처가 같으므로
이름을 손으로 두 번 적을 자리가 없다.

**감사가 보는 넷.**

| 판정 | 무엇이 어긋났는가 |
|---|---|
| `path_conflicts` | 같은 표면 안에서 같은 하위 경로가 두 번 — 어느 쪽이 눌렸는지 사용자가 알 수 없다 |
| `unnamed_items` | 하위 경로가 빈 항목 — 이름 없는 메뉴가 선다 |
| `silent_declarers` | 중앙 목록에 있는데 항목을 하나도 내지 않았다 |
| 선언자 동수 | 표에 실린 선언자 수가 목록 길이와 같은가 |

경로 충돌은 **표면 안에서만** 본다. File 과 Edit 에 같은 경로가 있는 것은 충돌이 아니다 —
서로 다른 메뉴여서 다툴 자리가 없다. 반대로 한 뿌리의 전역·선택 두 목록은 한 메뉴로 합쳐
그려지므로 같은 바구니에서 본다. 여기서 나누면 **화면에서 겹치는 경로를 놓친다.**

**게이트는 하나로 합쳤다.** 창 쪽 게이트(`verify-editor-declaration-wiring.ps1`)에 메뉴
단정과 표면 대조를 얹었다. 에디터를 한 번만 띄우면 되고, `editor::` 선언 배선이라는 한
주제를 한 자리에서 본다. 단정은 16 → 32 가 됐다. 개수는 여전히 못 박지 않는다.

**변이 다섯.** 제품에 결함을 **하나씩만** 심고 에디터를 통째로 다시 빌드해 게이트를 태웠다.
결함을 몰아 넣으면 한 단정이 눈멀어도 다른 단정이 가려 준다 — M4 4단계에서 실제로 겪은
양식이라 여기서는 처음부터 하나씩 세웠다.

| 변이 | 게이트가 내놓은 줄 |
|---|---|
| 같은 표면에 같은 경로 둘 | `path_conflicts=1 [content_browser_asset:Copy path]` |
| 하위 경로가 빈 항목 | `unnamed_items=1 [hierarchy:copy_entity_identity]` |
| 항목 0개를 내는 선언자를 목록에 더함 | `declarers=1/2` · `silent_declarers=1 [mutation_empty_menus]` |
| 애니메이터 팝업의 그리는 자리를 걷음 | `popup_host enumerators with no draw site: animator_node` |
| 자가 검사가 치운 표를 되돌리지 않음 | `메뉴 / 치워 둔 제품 표를 되돌리지 못했다` |

다섯 전부 붉고 무변이·복원 대조는 초록(`declared=25 bound=22`, 메뉴 항목 11, 표면 배선 팝업
6 / 상단 6, 단정 32)이었다. 붉은 줄이 전부 **고칠 곳을 가리킨다** — M4 4단계에서 게이트의
단정 순서를 고쳐 둔 것이 여기서 그대로 값을 했다.

**넷째 변이가 가장 값이 나갔다.** 그리는 자리 한 줄을 걷었을 때 런타임 감사는 **아무 말도 하지
않는다** — 표는 그대로고 항목 수도 그대로다. 소스 대조만이 그것을 본다. 런타임이 못 보는 것을
침묵으로 덮지 않고 다른 관측으로 옮긴 판단이 옳았다는 증거다.

**게이트가 증명하지 못하는 것을 따로 쟀다.** 중첩 메뉴가 실제로 그려지는지는 게이트 사정권
밖이다. 그리기는 메뉴가 열려야 한 번 도는 코드여서 헤드리스 실행에서는 `BeginMenu`/`EndMenu`
짝이 단 한 번도 맞춰지지 않는다. 그래서 임시 계측을 심어 Tools 메뉴를 프레임 90에서 강제로 열고
그린 항목 수를 찍었다(M4가 씬 뷰를 `SetWindowFocus`로 강제한 것과 같은 수법).

**첫 측정이 절반만 증명했다.** `items=2 submenus_opened=0`이 113프레임 내내 찍혔다. 항목 둘이
표에서 그리기로 넘어간 것은 확인됐지만 `Diagnostics` 하위 메뉴는 **한 번도 열리지 않았다** —
아무도 그 위에 올리지 않았으니 ImGui가 `false`를 돌려주는 정상 동작이다. 그 말은 증명된 것이
**닫힌 가지뿐**이라는 뜻이다. 닫힌 하위 메뉴 아래를 건너뛰고 `EndMenu`를 부르지 않는 경로는
맞는데, 열린 쪽의 짝은 안 돌았다. 그 짝이 어긋나면 ImGui 상태가 깨지는 종류의 결함이다.

그래서 계측을 한 겹 더 내려 하위 메뉴도 강제로 열고 그 안에서 그려진 잎을 따로 셌다(계수는
호출마다 0으로 되돌렸다 — 누적이면 숫자가 프레임 수를 재는 것이 되어 뜻을 잃는다).

```text
[PROBE] items=2 submenus_opened=1 leaves_drawn=2      (113프레임, 전부 동일 · 종료 0)
```

읽는 법은 이렇다. 항목 둘이 표에서 그리기로 넘어갔고, `BeginMenu("Diagnostics")`가 참을
돌려줘 **열린 가지가 돌았고**, 그 안에서 잎 둘이 그려졌고, 113프레임 동안 ImGui 단정이 걸리지
않고 종료 코드가 0이다 — `BeginMenu`/`EndMenu` 짝이 맞는다는 뜻이다. 확인 뒤 계측을 걷고
무변이로 다시 빌드했다(`restore_build_ok`). 상시 게이트로 만들려면 입력 주입이 필요하고,
그것은 W0 후반의 screenshot·canary가 들 장비다.

## 부록 B. 셸 크롬과 창 선언 계약 (`editor::`)

- 추가: 2026-09-10. 부록 A와 같은 `editor::` 계통이고 같은 관례를 쓴다 — 파일명 PascalCase,
  식별자 snake_case, 선언 함수 `for_editor()`, 중앙 목록 하나가 인스턴스화와 등록을 겸한다.
- B.1~B.2는 **착지 기록**이고 B.3~B.4는 아직 서지 않은 **계약**이다.

### B.1 M3 착지 기록 — 셸이 프레임을 소유한다 (2026-09-10)

**2026-09-12 후속 변경.** 상단은 `엔진 아이콘 → 메뉴 → 가운데 제목 → [Play/Stop · Pause/Resume] → 최소화/최대화/닫기` 순서다.
제목 행 20px·글꼴 12px·아이콘 14px·재생 박스 56×16px는 사용자 배율과 모니터 DPI를 적용하기 전 기준값이다.
창이 좁으면 여섯 메뉴 뿌리를 한 메뉴 안에 넣고, 제목이 겹칠 때는 제목만 생략한다.
메뉴 행과 도크 호스트의 테두리 선을 제거하고, 재생 박스 왼쪽에서 caption hit 영역을 끝낸다.
Play 트랜잭션·Undo 정책은 기존 SceneManager/PlayModeController가 소유한다.
아래는 최초 착지 기록이며, 후속 화면·검증 결과는 [EditorTitleBarValidation.md](../analysis/EditorTitleBarValidation.md)에 둔다.

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
**(2026-09-11 W1 정정.)** 이 자리에 있던 "에디터 자체는 이미 per-monitor v2 매니페스트를
켜고 있어 손댈 것이 없었다"는 단정은 틀렸다. 당시 본 `Academy_4Q.exe.manifest`는 제품 빌드에
연결되지 않았고 실제 EXE의 `#1` manifest에는 DPI 선언이 없었다. 이번 W1이
`CreatorEditor.manifest`를 빌드에 연결하고 실행 HWND PMv2 게이트를 추가했다(§3.2).
화면 캡처 도구의 크롭 원인 판정은 유지하되, 그것으로 제품 DPI awareness까지 증명하지 않는다.

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
`static_assert`로 막는다. 예외가 하나 남아 있었다 — Tile 스타일의 Content Browser는 팝업
드로어라 도크하지 않았고, 조건이 매 프레임 갈리는 값이라 선언의 정적 자리로는 적을 수 없었다.
**(2026-09-11)** 스타일 분기를 걷으면서 그 예외도 사라졌다. 이제 순회에 예외가 없다.

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
