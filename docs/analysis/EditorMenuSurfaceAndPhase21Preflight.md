# 에디터 메뉴 표면 감사 · PHASE 21 재정찰 (2026-09-10)

- 기준 트리: `c86ab960` + 미커밋 변경(에디터 UI 계층에는 변경 없음)
- 대조 정본: [CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)(처분표 · api 95 + split 3),
  [CommandSurfaceImplementation.md](CommandSurfaceImplementation.md)(GUI↔CLI 공통 편집표),
  `Editor/EngineEntry/CommandCore/CommandDescriptorSeeds.cpp`(seed 217),
  [EditorWorkspaceRedesignPlan.md](../plans/EditorWorkspaceRedesignPlan.md)(PHASE 21 · 2026-08-30 정찰)
- 세 질문에 답한다. ① CLI/API에는 있는데 에디터 GUI 메뉴에 없는 기능은 무엇인가. ② 임의 동작을
  상단 메뉴·팝업 메뉴의 특정 카테고리에 등록하기 쉬운 확장 구조가 있는가. ③ PHASE 21 착수 전제가
  지금도 서 있는가.

---

## 0. 결론

1. **GUI 메뉴는 3개(File·Edit·Settings) 19항목이 전부고, Window·Tools·Help 메뉴는 없다.** 제품
   명령 98개 중 GUI에 대응 동작이 있는 것은 32개, 나머지 66개는 GUI에서 닿을 수 없다. 그중 편집·상태
   변경 계열 16개가 실질 공백이고(§1.2), 50개는 조회·진단이다(§1.3). 반대로 GUI에만 있고 CLI에 없는
   동작도 30여 종이다(§1.4) — PHASE 21 W0의 관측 표면 설계는 이 목록을 그대로 쓴다.
2. **메뉴 확장 기구는 0이다.** `ImGui::MenuItem("…")` 리터럴과 인라인 본문이 4개 파일에 흩어져 있고,
   순회할 표·registry·contributor 인터페이스가 없다. 다만 필요한 부품은 전부 다른 이름으로 이미 있다
   (§2.2) — 창 registry, 명령 registry와 seed 표, 스레드 안전 실행 창구, 공통 편집 계층, C# 표식 호출.
   메뉴 registry 하나(1~2일)를 세우면 "경로 문자열 + 콜백" 등록이 가능하고, seed 표에 꼬리 필드
   하나를 더하면 명령이 자기 메뉴 위치를 선언할 수 있다(§2.3).
3. **PHASE 21 전제 17항 중 15항 그대로, 2항 정정.** 에디터 chrome 계층은 8-30 이후 실질 변경 0이다
   (`EditorRenderer.cpp`·ImGui host 커밋 0). 정정은 DPI(프로세스가 매니페스트로 PMv2를 이미 선언 →
   OS 보정 없이 1:1 렌더 중)와 ImGui 위치(ThirdParty가 아니라 vcpkg 1.92.8)다. 신규 발견 9건은 §3.2.
   **착수 가능하나 W0 착수 전에 결정 2건이 필요하다**(§3.4).

---

## 1. CLI에는 있고 GUI에는 없는 기능

### 1.1 방법과 모수

- 명령 모수: 처분표 `api` 95 + `split` 3 = **98**(Editor 제품 명령). seed에는 있으나 처분표에 없는
  14개는 Player 전용 5(`player.*`)와 Probe 9(`model.async`, `render.pbr.*`)라 GUI 대조 대상이 아니다.
- GUI 모수: 상단 메뉴 19항목 + 툴바 16 버튼 + 팝업 MenuItem 68(하드코딩) + 데이터 구동 루프 10곳
  + 전역 단축키 5. 전수 목록은 §1.5 표.
- 판정 기준: "GUI에 같은 결과를 내는 사용자 조작이 있는가". 드래그 드롭·OS 파일 드롭·단축키도
  GUI로 센다(단 §1.2에서 메뉴 부재는 따로 표기).

### 1.2 GUI에 없는 편집·상태 변경 명령 — 16개 (실질 공백)

| 명령 | CommandClass | GUI 현황 | 비고 |
|---|---|---|---|
| `prefab.create` | EngineService | 메뉴 없음. Content Browser의 Prefab 폴더에 SCENE_OBJECT 드롭만 (`ContentsBrowserWindow.cpp:180`) | Hierarchy 팝업에 "Create Prefab" 부재 |
| `prefab.update` | EngineService | **없음** | 프리팹 정의 갱신은 CLI 전용 |
| `script.reload` | EngineService | **없음**. 상태바 LiveCode 버튼은 `BeginDisabled(true)` 자리만 유지 (`MenuBarWindow.cpp:599`) | 핫리로드가 GUI에서 불가 |
| `script.invoke` | EngineService | **없음** | `[EngineCallable]` 표식 호출. §2.3의 C# 메뉴 씨앗 |
| `model.load` / `model.loadcached` | EngineService | 메뉴 없음. OS 파일 드롭만 (`App.cpp:221`, `WM_DROPFILES`) | "Import Model" 항목 부재 |
| `camera.editor match/follow` | EngineService | **없음**. `G`(선택을 카메라로) · `F`(선택 프레이밍)만 (`SceneViewWindow.cpp:584,596`) | 역방향(에디터 카메라를 게임 카메라로)은 CLI 전용 |
| `assets.unload` | EngineService | **없음** | |
| `scene.ddol` | EngineService | **없음** | |
| `scene.flag` | EngineService | **없음** | dirtytraversal·bonecache 진단 플래그 |
| `gc.collect` | EngineService | **없음** | |
| `window.resize` | EngineService | **없음** | 해상도 회귀용 |
| `pix.capture` | EngineService | **없음** | |
| `lifecycle.trace on/off/clear` · `mem.hook` · `mem.reset` | EngineService | **없음** | 계측 토글 |
| `gpu.baseline` | EngineService | 부분 — Resource Counter 창의 "현재를 기준선으로" (`ResourceCounterWindow.cpp:40`)는 다른 스냅샷 |
| `cli.drain.budget` | EngineService | **없음** | 서비스 전용이라 GUI 대상 아님 |

### 1.3 GUI에 없는 조회·진단 명령 — 50개

GUI 창이 일부 겹치는 것은 괄호에 적었다.

`ai.status` · `animator.status` · `assets.modeldiag` · `bt.status` · `commands.list/describe/selftest` ·
`component.list`(Add Component 팝업이 같은 표를 순회) · `crash.status` · `dump.list/show` · `dx12.live` ·
`gc.stats` · `gpu.census`(Resource Counter 부분) · `help` · `lifecycle.registry/dump` · `light.proxy` ·
`log.flush` · `mem.delta/stats` · `object.describe` · `pipeline.nodes`(Pipeline Setting 창 부분) ·
`play.state` · `prefab.objectguid/overrides/status` · `profile.stats`(FrameProfiler 창 부분) ·
`render.rtinfo/shadowinfo` · `scene.dump/hierarchycheck/transformdigest/bonedump/sparseresolver/selection` ·
`scene.transformstats/transformwritestats/transformpull` · `script.status` · `ui.status/rect/hitbox` ·
`undo.state` · `window.info` · `cli.echo.args`

### 1.4 반대 방향 — GUI에만 있고 CLI에 없는 동작 (W0 관측 표면 입력)

| 영역 | GUI 동작 | 위치 | CLI |
|---|---|---|---|
| 재생 | **Pause 토글** | `MenuBarWindow.cpp:494` `ToggleGamePaused` | `play`/`stop`만 등록(`SceneObjectCommands.cpp:1222`). pause 없음. `play.state`가 읽기만 |
| 생성 | Directional/Point/Spot Light 하위 타입 | `HierarchyWindow.cpp:135-151` | `object.create <이름> Light`는 타입만, 하위 타입 없음 |
| 생성 | UI Image / Text | `HierarchyWindow.cpp:172,176` | UI 생성 명령 없음(`ui.*`는 편집만) |
| 편집 | Copy / Paste(복제) | `HierarchyWindow.cpp:108,112` | `object.duplicate`가 근사 |
| 편집 | Reset Transform · Add Layer · meta Save · 스크립트 인스턴스 재생성 | `InspectorWindow.cpp:1028,794,493,567` | 없음 |
| 뷰 | 기즈모 모드 Q/W/E/R · Snap · Ortho/Persp · Grid · 카메라 설정 · Wireframe(Ctrl+W) · GameView 숨김(F5) · PVD(F9) | `SceneViewWindow.cpp:167-181,258-322`, `EditorMain.cpp:526-536` | 없음 |
| 자산 | Create Volume Profile · Delete 파일 · 탐색기 열기 | `ContentsBrowserWindow.cpp:256-548` | 없음 |
| 저작 창 | Behavior Tree · Blackboard · InputActionMaps 편집기의 Create/Open/Save 전부 | `MenuBarWindow.cpp:947-2600` | `bt.status`·`blackboard.authoring.probe`(commandlet)만 |
| 설정 | Pipeline Setting 튜닝 · Render Backend 선택 · Build Settings 시작 씬 · Collision Matrix Save/Load · ImGuiScale · Content Browser 스타일 | `MenuBarWindow.cpp:372-452,195-204,2656-2672` | `render.backend status`는 조회만("변경은 Settings") |
| chrome | Output Log · ProfileFrame 토글 · 기즈모 콜라이더 디버그 | `MenuBarWindow.cpp:549,555,619` | **editor chrome 관측 명령 0**(§3.1 #14) |
| 컴포넌트 | Material Instantiate/Picker · Terrain 레이어/마스크/저장 · Animator 편집 대부분 | `ImGuiDrawHelperMeshRenderer.cpp:73-84`, `ImGuiDrawHelperTerrainComponent.cpp:107-334`, `ImGuiDrawHelperAnimator.cpp` | `animator.param`·`render.matmode`만 공유 |

### 1.5 GUI 표면 전수 (요약)

| 표면 | 항목 수 | 위치 |
|---|---|---|
| 상단 메뉴 File | 6 (New Scene·Save·Save As·Load Scene·GameBuild·Exit) | `MenuBarWindow.cpp:228-321` |
| 상단 메뉴 Edit | 5 (LightMap·Effect Editor·Behavior Tree·Blackboard·InputAction Maps) | `:325-365` |
| 상단 메뉴 Settings | 6 + 하위 2 + DragFloat 1 | `:369-459` |
| 메뉴바 툴바 | 3 (Play/Stop·Pause·Browser 스타일) | `:467-510` |
| 상태바 툴바 | 5 (Content Drawer·Output Log·ProfileFrame·LiveCode[비활성]·Debug) | `:534-619` |
| Scene View 툴바 | 8 | `SceneViewWindow.cpp:249-322` |
| Hierarchy 팝업 | 13 | `HierarchyWindow.cpp:98-186` |
| Content Browser 팝업 | 4개 팝업 9항목(3벌이 사실상 복제) | `ContentsBrowserWindow.cpp:252,330,447,527` |
| Inspector 팝업/버튼 | Add Component(데이터 구동 2루프)·Remove·Reset Transform·Tag/Layer 추가 등 | `InspectorWindow.cpp:379-1325` |
| BT/Blackboard/InputAction/Animator/Log 팝업 | 40여 | `MenuBarWindow.cpp:1558-2625`, `ImGuiDrawHelperAnimator.cpp` |
| 전역 단축키(메뉴 항목 없음) | 5 (Ctrl+Z·Ctrl+Y·F5·Ctrl+W·F9) | `EditorMain.cpp:227-536` |

Edit 메뉴에는 Undo/Redo/Copy/Paste가 없다(Hierarchy 팝업과 단축키에만 있다). `AssetBundle` 창은
어느 메뉴에서도 열 수 없다(imgui.ini에 남아 있을 때만 보인다).

### 1.6 대조 중 드러난 GUI 결함 (요청 범위 밖 · 기록만)

- 본문이 비어 있는 항목 3: Hierarchy `UI > Button`(`HierarchyWindow.cpp:180`), Animator `Copy Contorller`
  (`ImGuiDrawHelperAnimator.cpp:251`), FSM `Add State`(`InspectorWindow.cpp:1058`).
- 도달 불가 조건 2: `Create Volume Profile`의 가드가 `X.empty() && equivalent(X, VolumeProfilePath())`
  (`ContentsBrowserWindow.cpp:254,546`). 같은 파일 `:333`은 `!X.empty()`로 옳다.
- `GameBuild`는 시작 씬이 비어 있으면 **조용히 무동작**(`MenuBarWindow.cpp:302`). 비활성 표시가 없다.
- Content Browser `Delete` 두 곳이 확인·Undo 없이 `file::remove`(`:458,:531`).
- Save/Save As 본문이 메뉴(`:235-278`)와 단축키 핸들러(`:747-800`)에 **두 벌** 복제돼 있다.
  `MenuItem`의 `"Ctrl+S"` 문자열은 표시용이고 바인딩이 아니다. `Ctrl+Shift+N`도 표시만 있다.
- LightMap Window·Render Debug는 stub 창(전자는 "unavailable" 문구, 후자는 Pipeline Setting으로 redirect).

---

## 2. 메뉴 확장성

### 2.1 현재 — 기구 0

- 상단 메뉴: `MenuBarWindow::RenderMenuBar()`(`MenuBarWindow.cpp:217-523`)에 `BeginMenu("File"|"Edit"|"Settings")`
  세 리터럴과 인라인 본문. Save 하나가 25줄이다.
- 팝업: Hierarchy는 `BeginPopup("HierarchyMenu")`(`HierarchyWindow.cpp:98`) 하드코딩. Content Browser는
  같은 이름 `"Context Menu"` 팝업이 **세 벌**(`:252,:447,:527`) + `"Directory Context"`(`:330`).
- `Contributor|MenuEntry|MenuRegistry|RegisterMenu|AddMenuItem|MenuExtension|ToolMenu|MenuPath` 전역 grep:
  메뉴 관련 0건(`IRenderFeatureContributor`만 매치).
- `ICustomEditor.h`는 구현체 0·호출자 1(`InspectorWindow.cpp:358` dynamic_cast)인 죽은 확장점.
  `ExternUI.h`는 extern 함수 선언 목록이고 등록이 아니다.
- 데이터 구동으로 메뉴를 그리는 곳은 팝업 안에만 10곳(Add Component가 `ComponentFactorys->m_componentTypes`와
  `ClrHost::GetComponentTypeNames()`를 순회 — `InspectorWindow.cpp:393,421`). **동작(action) registry는 없다.**
- 단축키 registry도 없다. `IsKeyPressed` 인라인 검사가 5개 파일에 흩어져 있고, 입력 API가 넷
  (`InputManagement`, `ImGui::IsKeyPressed`, `ImGui::IsKeyDown`, `VK_F5` raw)이다.
- C# 쪽 attribute는 `[SerializeField]`·`[EngineCallable]` 둘뿐(`ScriptCore/EngineCallable.cs:35`).
  `[MenuItem]` 류 없음.

### 2.2 이미 있는 부품

| 필요한 것 | 있는 것 | 위치 |
|---|---|---|
| 문자열 키 콜백 registry + 매 프레임 pump | `ImGuiRegister`(창 단위) · `EditorRenderer::Render` | `ImGuiRegisterClass.h:5,101` · `EditorRenderer.cpp:250-259` |
| 유니티 빌드에서 안전한 명시 등록 관례 | `Registrar` + `RegisterXxxCommands(Registrar&)` 순서 호출 | `Commands/CommandRegistrar.h:30-66` · `ConsoleCommandSystem.cpp:4297-4303` |
| 이름·요약·usage·비용·Undo 여부가 붙은 동작 목록 212개 | `CommandRegistry` + `kSeeds` | `CommandRegistry.cpp:65` · `CommandDescriptorSeeds.cpp:33` |
| 스레드 안전·게임 스레드 위임 실행 | `ConsoleCommandSystem::EnqueueStructured(args, completion)` | `ConsoleCommandSystem.h:119` |
| GUI·CLI가 이미 함께 쓰는 편집 계층 | `EditorObjectOperations` / `EditorProjectOperations` | `EditorObjectOperations.h:19-55` |
| 표를 순회해 MenuItem을 내는 전례 | Add Component 팝업 | `InspectorWindow.cpp:386-413` |
| C# 표식 메서드 호출(승인 경계 포함) | `[EngineCallable]` + `script.invoke`(`executesUserCode=true`) | `EngineCallable.cs:35` · seed `:235` |

### 2.3 최소 기구와 변경 지점

> 확정 설계는 [EditorWorkspaceRedesignPlan.md](../plans/EditorWorkspaceRedesignPlan.md) **부록 A**로
> 옮겼다. 아래는 그 설계를 유도한 초안이다. 확정본과 다른 점 둘: 표면을 경로 문자열 하나로 두지 않고
> 상단 카테고리와 팝업 호스트의 **닫힌 열거형 둘**로 갈랐고, 등록은 런타임 호출이 아니라
> `for_editor()` 컴파일 타임 선언과 중앙 목록 하나로 세웠다. 후자의 근거는 Editor가 StaticLibrary이고
> `/WHOLEARCHIVE`가 없어 참조되지 않는 TU의 자기 등록자가 링커에 탈락한다는 실측이다.

**신설** `Editor/EngineGUIWindow/EditorMenuRegistry.{h,cpp}` — `IncludeInUnityFile=false`
(`Commands/*.cpp`·`EnhancedRenderDebugWindow.cpp` 전례, `Editor.vcxproj:129-182`).

```text
EditorMenuEntry { path "Tools/Assets/Reimport" | "Hierarchy/Create/Prop"; label; shortcutHint(표시용);
                  enabled(); invoke(); order }
Register / Unregister / DrawRoot("File"|"Edit"|"Settings"|"Tools"|"Window"|"Hierarchy"|"ContentBrowser.Asset"|...)
RegisterCommandMenuItem(path, commandName, args)  →  invoke = EnqueueStructured({name,args...}, [](auto&,auto&){})
```

**수정**
- `MenuBarWindow.cpp` `RenderMenuBar()`: 각 `EndMenu`(`:321,:365,:459`) 직전에 `DrawRoot`, `EndMainMenuBar`(`:523`)
  앞에 registry 전용 `Tools`·`Window` 메뉴. 기존 인라인 항목은 그대로 두고 **덧붙이기만** 한다.
- `HierarchyWindow.cpp:186` `EndPopup` 앞, `ContentsBrowserWindow.cpp:266,340,473,553` 네 곳(폴더/자산 루트를
  나누고 세 벌 복제를 이 기회에 합친다).
- `EditorMain.cpp:173-205` 창 생성 블록에서 `EnsureRegistryPopulated()` 뒤 seed 순회 등록.

**선택 — 명령이 메뉴 위치를 선언**: `DescriptorSeed` 꼬리에 `const char* menuPath = "";`
(`CommandDescriptorSeeds.h:62` 뒤). 위치 지정 집합 초기화라 기존 212행 무편집, 정렬 `static_assert`
(`CommandDescriptorSeeds.cpp:294`)는 `name`만 보므로 영향 0. 소비는 `ConsoleCommandSystem.cpp:4207-4249`의
seed→descriptor 복사에 한 줄. `commands.list` TSV에 열을 더하면 `verify-cli-registry-golden.ps1` 골든 재생성 필요.

**주의 3건**
1. **스레드**: 메뉴 콜백은 PresentationThread에서 `m_sceneStructureMutex` 아래 실행된다
   (`EditorMain.cpp:320-372` → `PresentFrame` → `OnGui` → `RenderMenuBar`). 오늘 File>Save는 그 스레드에서
   `SceneManagers`를 직접 만진다(`MenuBarWindow.cpp:243`). registry 동작은 `EnqueueStructured`로 게임 스레드에
   넘기는 쪽이 옳다.
2. **큐 선택**: completion을 넘기지 않으면 배치 큐로 가서 `CommandSession::Batch()`에 누적되고 exit code를
   오염시킨다(`ConsoleCommandSystem.cpp:638,856,872` — LC5가 HTTP에서 고친 그 결함). GUI 호출은 **빈 completion을
   반드시** 넘기거나 origin 플래그를 새로 둔다.
3. **단축키는 별건**: registry는 `shortcutHint`를 표시할 뿐 바인딩하지 않는다. 진짜 바인딩은 별도 registry가
   필요하고, 동작 ID를 가진 메뉴 registry가 그 선행조건이다.

**공수**: registry + 두 루트(상단·Hierarchy) + Content Browser 통합 = 1~2일. seed `menuPath` 선택지 +0.5일.

### 2.4 PHASE 21과의 관계

계획서 §1.3(`:130`)과 W3(`:774`)은 "Window 메뉴를 통한 재열기 계약"을 요구하는데 **Window 메뉴 자체가
없다.** 또 창 표시 상태 저장소가 둘로 갈라져 있다 — `MenuBarWindow` bool 10개와 `ImGuiRenderContext::m_opened`
(§3.2 #17). 메뉴 registry는 W3의 선행 부품이며, 두 저장소 통합과 같은 슬라이스에서 다루는 것이 맞다.

---

## 3. PHASE 21 재정찰 (2026-08-30 정찰 대비)

### 3.1 17항 판정

| # | 8-30 주장 | 9-10 판정 | 현재 근거 |
|---|---|---|---|
| 1 | Content Browser dock 이름 공백 불일치 | **그대로** | `EditorRenderer.cpp:167`(공백 2) vs `ContentsBrowserWindow.cpp:103`(공백 1). 14건 중 유일한 불일치 |
| 2 | imgui.ini 4벌, Content Browser entry 둘 | **그대로** | 4벌 전부 entry 2개(DockId 다름) |
| 3 | `SetGameStart` 즉시 true, 스냅샷 실패 시 미복구 | **그대로**(위치 이동) | `SceneManager.cpp:275-282`, `:1545-1548` |
| 4 | DPI 배관 0 | **정정** | 코드 0은 맞으나 매니페스트가 `permonitorv2`(`Academy_4Q.exe.manifest:26`) — OS 보정 없이 물리 픽셀 1:1. `DisplayFramebufferScale=(1,1)`은 `ImGuiHost.cpp:106` 리사이즈 분기 안에서만 |
| 4' | ImGui 1.92.8 ThirdParty | **정정** | vcpkg 1.92.8(`vcpkg_installed/.../imgui.h:32`). `FontScaleMain/FontScaleDpi/ConfigDpiScaleFonts` 존재(`:2374,2375,2536`). `io.FontGlobalScale` 2곳(`EditorRenderer.cpp:88,120`)은 obsolete 블록 덕에 컴파일 |
| 5 | `BeginFrame`이 WantCapture*·ConfigFlags 매 프레임 덮음 | **그대로** | `HostImGuiPresentation/RHI/ImGuiHost.cpp:114-118` |
| 6 | `m_contexts` unordered_map | **그대로** | `ImGuiRegisterClass.h:101`, pump `EditorRenderer.cpp:252-256` |
| 7 | `gizmoWindowFlags` 죽은 `|= NoMove` | **그대로** | `SceneViewWindow.cpp:194/213` |
| 8 | PushStyleColor 48 + Var 43 = 91, MenuBar 48 | **그대로** | 동일 수치 |
| 9 | ImGuiListClipper 0 | **그대로** | |
| 10 | 비-UTF8 9파일 | **8파일** | `DrawYamlNodeEditor.cpp`는 이제 UTF-8. 나머지 8 그대로 |
| 11 | `ImGuiContext.h`가 `imgui_impl_dx11.h` include | **그대로** | `:5`. host는 DX12/Vulkan shell만(`ImGuiHost.cpp:48-52`) |
| 12 | FA4·FA6 공존 | **그대로** | FA6 15파일, FA4 1(`ProfilerWindow.cpp:10`). `ICON_MIN/MAX_FA` 값 충돌, 같은 폴더 유니티 청크 위험 |
| 13 | Console/Profiler는 bool 토글 | **그대로** | `MenuBarWindow.h:20-29` bool 10개 |
| 14 | editor chrome CLI 표면 0 | **그대로** | `editor./imgui./layout./dock./workspace.` 0. `window.info`는 OS 클라이언트 크기만 |
| 15 | verify-editor-workspace 없음 | **그대로** | 127 verify 중 0. `capture-window.ps1`(PrintWindow PNG)은 재사용 가능 |
| 16 | — | 8-30 이후 에디터 UI 계층 커밋 20, 전부 타 시스템 파급. `EditorRenderer.cpp`·ImGui host **0** | |
| 17 | — | Window 메뉴 없음. `EditorSettingsStore`는 5키(browser style·imguiScale·startup scene·backend 2)만 저장, 창 상태 0 | `EditorSettingsStore.cpp:194-219` |

### 3.2 신규 발견 9건 (8-30 정찰에 없음)

1. **Tile 분기는 Content Browser·AssetBundle을 dock하지 않는다**(`EditorRenderer.cpp:184-189`, 6창만). Tile이
   기본값(`EditorPreferences.h:33`)이라 새 설치에서 Browser는 이름 결함과 무관하게 **항상 떠 있다**.
2. **dock 지정에 없는 창 15+**: FrameProfiler·Log·InputActionMaps·Build Scene Setting·RenderPass(2)·Resource
   Counter·LightMap·CollisionMatrixPopup·Animator 3·Select Audio Clip·자산 picker 2. `BuildInitialDockLayout`은
   워크스페이스의 부분집합만 기술한다.
3. **ini는 한 번 생기면 재생성 경로가 없다**(`EditorRenderer.cpp:238-246` 부재 시에만 빌드). 손상 layout을
   치유할 코드가 없고, browser style을 바꿔도 기존 ini에는 영향 0. W3의 reset/version 키가 필수다.
4. **Play 실패의 파급이 더 넓다**: `PlayModeEvent.Broadcast(true)`가 `CaptureSceneSnapshot()` **앞**에서 발화해
   (`SceneManager.cpp:1538-1540`) 구독자가 Undo 스택을 비우고 game mode로 바꾼다(`EditorPlayModeController.cpp:25-52`).
   스냅샷이 실패해도 Undo는 이미 파괴됐고 `m_isEditorSceneLoaded`는 무조건 true(`:347-357`).
5. **`play.state`는 `ScenePhase`를 내지 않는다**(`SceneObjectCommands.cpp:1034-1057`). 위 실패 상태가 CLI에서
   성공과 구분되지 않는다. W0 관측 명령이 ScenePhase를 실어야 W5 게이트가 이빨을 갖는다.
6. **`GetContext(name)`이 `operator[]`로 유령 창을 만든다**(`ImGuiRegisterClass.h:95-98`). 오타 이름이 빈
   `m_name` entry를 pump 맵에 영구 삽입하고 `ImGuiContext.h:45-48`이 조용히 건너뛴다. #1의 공백 결함이
   `MenuBarWindow.cpp:515,536`을 지나면 정확히 이 경로다.
7. **multi-viewport가 반만 배선**: `PlatformHasViewports`를 매 프레임 OR하지만 `ViewportsEnable`은 어디서도
   켜지지 않고 `EndFrame`(`ImGuiHost.cpp:135-139`)은 그 플래그를 본다. §1.7 "지원 계약 없음"의 실체.
8. **창 표시 상태 저장소가 둘**: `MenuBarWindow` bool 10개(비영속) vs `ImGuiRenderContext::m_opened`
   (popup은 등록 시 강제 true, "테스트용 임시" 주석 `ImGuiRegisterClass.h:43,58`). 동기화 0. 매 기동 시 모든
   패널이 닫힌 상태로 시작하고 geometry만 ini에서 복원된다.
9. **`HostImGuiPresentation/`은 8-24 E7-b 재배치분**이지 신규가 아니다. 8-30 정찰이 `ImGuiHelper`에서 host를
   찾아 위치를 잘못 적었다(`ImGuiHost.cpp:113~118` → 실제 `HostImGuiPresentation/RHI/ImGuiHost.cpp:114-118`).

### 3.3 계획서 슬라이스별 영향

| 슬라이스 | 영향 |
|---|---|
| W0 | `editor.*` 5종은 여전히 0이라 신설 그대로. **`editor.viewport`(또는 `play.state`)에 `ScenePhase`를 추가**해야 W5 실패 상태를 단정할 수 있다(#5). `capture-window.ps1`을 screenshot 기구로 재사용. GUI 전용 동작 목록(§1.4)이 "관측해야 할 상태"의 정본이다 |
| W1 | DPI 절 재서술: 프로세스는 이미 PMv2라 "인식 채택 여부 판정"이 아니라 **"이미 인식 중인데 보정이 0"**이 문제다. `WM_DPICHANGED` 처리 + `FontScaleDpi`/`ConfigDpiScaleFonts` 채택이 최소 경로. `Fonts->Build()`(`EditorRenderer.cpp:84`)도 1.92 동적 아틀라스에서 legacy |
| W3 | Window 메뉴 부재(§2.4) → **메뉴 registry가 선행 부품**. 두 표시 상태 저장소 통합(#8), ini 재생성/버전 키(#3), `GetContext` `operator[]` → `find`(#6), Tile 분기 dock 누락(#1) |
| W4 | 15+ 미도킹 창(#2)을 ToolPanel 인벤토리에 넣지 않으면 "자유 도킹"이 부분집합에만 적용된다 |
| W5 | 신호 순서: Broadcast → Snapshot → Phase 를 **Snapshot → Phase → Broadcast**로 바꾸지 않으면 committed 신호가 있어도 Undo는 먼저 죽는다(#4). `bdb22b0d`(9-4)가 이 순서의 상류를 옮겼다 |
| W2 | 비-UTF8 대상 8(9→8). FA4 유일 소비자 `ProfilerWindow.cpp`의 `ICON_FA_TIMES`·`ICON_FA_PAINT_BRUSH`는 FA6에 없는 이름 |

### 3.4 착수 판정

**진입 가능.** 8-30 기준선이 그대로라 계획서 §1을 다시 쓸 필요는 없고, §1.9·§3.2·W0·W3·W5에 위 정정을
반영하면 된다. 단 W0 착수 전에 결정 2건이 선다.

1. **메뉴 registry를 PHASE 21 안(W3 선행)으로 넣는가, 별도 슬라이스로 두는가.** W3의 "Window 메뉴 재열기"는
   registry 없이는 또 하나의 하드코딩 메뉴를 낳는다. 권고: W0과 병행 가능한 **W3-pre(1~2일)**로 둔다.
2. **DPI를 W1에서 실제로 채택하는가.** PMv2 매니페스트가 이미 켜져 있어 "하지 않는다"는 선택이 곧 150% 모니터에서
   1:1 렌더를 유지한다는 뜻이다. 계획서 W1 판정문("채택하지 않으면 명시")은 이 사실을 전제로 다시 적어야 한다.
