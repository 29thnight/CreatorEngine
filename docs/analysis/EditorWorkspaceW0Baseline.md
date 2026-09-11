# PHASE 21 W0 기준선 — 창 목록 · 스타일 지점 · 성능 · 셸 캡처

2026-09-11. 계획서 `docs/plans/EditorWorkspaceRedesignPlan.md`의 W0 후반 산출물이다.

W7·W8의 성능 판정과 W1의 토큰 이주가 전부 **"W0 기준선과 비교"**를 전제한다. 그 기준선이
없으면 판정문이 추정치가 된다. 여기 적힌 수는 전부 실측이고, 재지 못한 것은 재지 못했다고
적었다.

측정 대상은 `Bin/x64-Release/Editor/CreatorEditor.exe`(2026-09-11 13:00 빌드), 커밋
`031cf238` 직후 상태다.

---

## 1. 선언된 창 스물다섯

`editor.windows`가 낸 표다. `declared=25 bound_bodies=22 orphan_bodies=0 bodyless_windows=0
duplicate_ids=0 empty_dock_slots=0 audit=clean`.

| 선언자 | 창 | 역할 | 도크 자리 | 닫힘 | 기본 열림 |
|---|---|---|---|---|---|
| `editor_panel_windows` | Hierarchy | panel | right_upper | 아니오 | 예 |
| | Inspector | panel | right_lower | 아니오 | 예 |
| | AssetBundle | panel | bottom | 아니오 | 예 |
| | Content Browser | panel | bottom | 아니오 | 예 |
| | Resource Counter | panel | bottom | 예 | 아니오 |
| `editor_tool_windows` | RenderPass · LightMap · CollisionMatrixPopup · TextureType Selector · SelectMaterial | panel | floating | 예 | 아니오 |
| `editor_viewport_windows` | Scene · Game | central | center | 아니오 | 예 |
| `editor_authoring_windows` | Behavior Tree Editor · BlackBoard Editor | panel | center | 예 | 아니오 |
| | InputActionMaps | panel | floating | 예 | 아니오 |
| `editor_diagnostic_windows` | FrameProfiler | panel | floating | 예 | 아니오 |
| | Log · RenderPass Debug | panel | floating | 예 | 아니오 |
| `editor_dialog_windows` | About Creator Engine · Build Scene Setting · Grid Settings | panel | floating | 예 | 아니오 |
| | Model loading | **transient** | floating | 아니오 | 예 |
| `editor_animator_windows` | Event · Animation Controllers · AvatarMask | panel | floating | 예 | 아니오 |

읽을 것 셋.

- **기본으로 열리는 창은 일곱**이다(Hierarchy · Inspector · AssetBundle · Content Browser ·
  Scene · Game · Model loading). 나머지 열여덟은 닫힌 채 뜨고, 그래서 `FindWindowByName`이
  널을 돌려준다 — 감사가 그것을 결함으로 읽지 않는 이유다.
- **`Model loading`만 `transient`**다. 유일하게 상태를 저장하지 않는(`persist=0`) 창이다.
- 도크 자리를 가진 창은 아홉이고 나머지 열여섯은 떠 있다. 떠 있는 창은 감사에서
  `dock_exempt`로 빠진다.

메뉴 표면은 `editor_core_menus` 한 선언자에서 항목 열하나가 나온다(top 3 · popup 6 ·
top_selection 2). `editor.menu`가 정본이다.

---

## 2. `PushStyleColor` / `PushStyleVar` 지점 **쉰일곱**

W1의 입력이다. 계획서 본문은 **91건**이라고 적고 있는데 그것은 M3 이전의 수다 — M3가 메뉴
행을 표로 옮기며 창별 예외가 줄었다. 다시 셌다.

`PushStyleColor` 31 + `PushStyleVar` 26 = **57**. (`Editor/ImGuiHelper/`는 제외했다. 그쪽은
위젯 구현이라 W2의 대상이지 토큰 이주의 대상이 아니다.)

| 파일 | Color | Var | 합 |
|---|---:|---:|---:|
| `EngineGUIWindow/MenuBarWindow.cpp` | 13 | 7 | **20** |
| `EngineGUIWindow/SceneViewWindow.cpp` | 6 | 4 | 10 |
| `EngineGUIWindow/InspectorWindow.cpp` | 4 | 4 | 8 |
| `EngineGUIWindow/HierarchyWindow.cpp` | 3 | 2 | 5 |
| `EngineGUIWindow/ContentsBrowserWindow.cpp` | 1 | 3 | 4 |
| `EngineGUIWindow/ImGuiDrawHelperRectTransformComponent.cpp` | 2 | 1 | 3 |
| `EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp` | 0 | 2 | 2 |
| `EngineEntry/EditorAssetPresentation.cpp` | 1 | 1 | 2 |
| `EditorWindow/EditorWindowHost.cpp` | 1 | 1 | 2 |
| `EngineGUIWindow/EditorRenderer.cpp` | 0 | 1 | 1 |

계획서가 "48건이 `MenuBarWindow.cpp`에 몰려 있으므로 그 파일을 먼저 친다"고 적은 것도 낡았다.
지금 그 파일의 몫은 **20**이고 여전히 가장 크지만 전체의 35%다. 재계수 명령은 이렇다.

```bash
grep -rn 'ImGui::PushStyleColor\|ImGui::PushStyleVar' --include='*.cpp' --include='*.h' \
  Editor/ --exclude-dir=ImGuiHelper | wc -l
```

---

## 3. 성능 기준선 (Release)

`editor.dock`을 20프레임 간격으로 열두 번 불러 표본을 떴다. Debug로 재지 않은 이유는
저장소 규약이다 — Debug는 방향까지 뒤집는다.

**★ 띄우기 전에 `imgui.ini`를 지웠다.** 이것을 빠뜨리면 재는 대상이 달라진다. `EditorRenderer.cpp:296`이
`resetRequested || !file::exists(iniPath)`라 **ini가 있으면 도크 빌더가 돌지 않고** 배치를 그
파일이 정하기 때문이다. 처음 잰 값은 디스크에 있던 개발자의 옛 ini(창 13 · 노드 11) 위에서 나온
것이라 틀렸다. 정정 폭이 작지 않다.

| | 옛 ini 위 (틀림) | 선언이 세운 배치 (정본) |
|---|---:|---:|
| 정점 | 2,978 | **1,688** |
| 인덱스 | 7,071 | **3,858** |
| draw command | 13 | **12** |
| UI CPU 중앙값 | 1.90 ms | **0.38 ms** |

옛 ini 쪽이 다섯 배 비쌌던 것은 그 파일이 Log·FrameProfiler·Behavior Tree Editor 같은 창을
열어 둔 상태였기 때문이다. 아래는 전부 **정본** 값이다.

**기하는 완전히 결정적이었다.** 열두 표본 전부 같은 값이다.

| 값 | 측정 |
|---|---:|
| ImGui 정점 | 1,688 |
| ImGui 인덱스 | 3,858 |
| ImGui draw command | 12 |

**UI CPU는 흔들린다.** `BeginRender`부터 `EndRender`까지이고 GPU 제출과 Present는 빠진다.

| 통계 | ms |
|---|---:|
| 최솟값 | 0.26 |
| 중앙값 | 0.38 |
| 평균 | 0.37 |
| 최댓값 | 0.46 |

**★ 이 수가 어떤 배치에서 나온 값인지가 수 자체만큼 중요하다.** 축이 어긋나면 나중에
"회귀"로 오독된다. 측정 상태는 **ini 없이 띄워 `BuildInitialDockLayout`이 세운 배치**이고,
도크 노드 7 · 리프 4 · 도킹된 창 6 · 중앙 노드 0 · `imguiScale` 0.8 · 창 1920×1111이다.
창을 더 열거나 사용자 ini로 뜨면 셋 다 정당하게 바뀐다 — 위 표가 그 폭을 보여 준다.

밖에서 읽는 방법은 이렇다.

```bash
editor.dock
```

`[AUDIT] uiCpuMs=... imguiVertices=... imguiIndices=... imguiDrawCommands=...` 줄이 나오고,
같은 값이 결과 JSON의 `uiCpuMs` · `imguiVertices` · `imguiIndices` · `imguiDrawCommands`에도
실린다.

### 재지 못한 것 — 타깃별 GPU ms

계획서 §11이 함께 요구했지만 **잴 수단이 저장소에 없다.** GPU 타임스탬프 질의 표면이 0이고
(`GpuTimer`·`TimestampQuery` 류의 심볼이 `Engine/RenderEngine/`과 `Editor/` 전체에 없다),
`profile.stats`가 내는 것은 CPU 프로파일러의 **건강 상태**이지 구간 시간이 아니다.

추정치로 채우지 않는다. 이 수를 실제로 필요로 하는 것은 W4의 view demand 판정
("Game Preview가 닫힌 single-view 상태에서 불필요 target 생산 여부가 계측된다")이므로 그
슬라이스가 계측 표면을 함께 세워야 한다. `editor.viewport`를 W5로 넘긴 것과 같은 판단이다.

---

## 4. 셸 캡처 넷

`docs/analysis/images/w0-shell/`에 있다. 해상도 둘 × user scale 둘.

**넷 다 `imgui.ini`를 지우고 띄웠다** — `fresh` 접두어가 그 뜻이다. 성능 기준선과 같은 이유이고,
빠뜨리면 선언이 세운 배치가 아니라 그 기계에 남아 있던 배치를 찍게 된다.

| 파일 | 요청 | 캡처된 클라이언트 rect | `imguiScale` |
|---|---|---|---:|
| `shell-fresh-1920x1080-scale0.8.png` | 기본(리사이즈 없음) | 1920×1111 | 0.8 |
| `shell-fresh-1920x1080-scale1.2.png` | 기본(리사이즈 없음) | 1920×1111 | 1.2 |
| `shell-fresh-2560x1440-scale0.8.png` | 2560×1440 | 2560×1471 | 0.8 |
| `shell-fresh-2560x1440-scale1.2.png` | 2560×1440 | 2560×1471 | 1.2 |

**★ 이 넷은 아직 유니티식 2×3 배치다.** S&Box식 **고정 중앙 `ViewportHost` + 주변 자유 도킹**은
W4가 세운다(§4.2). 지금 `BuildInitialDockLayout`은 `DockBuilderAddNode`를 `DockSpace` 플래그
없이 부르고 중앙을 아무도 지정하지 않아, 감사가 `central=0`을 찍는다. 그러니까 이 캡처는
**목표 배치가 아니라 목표 이전의 모습**이고, 그것이 기준선의 정의다.

**이것은 user scale 캡처이지 DPI 캡처가 아니다.** 계획서 §3.2가 요구한 구분이다. 찍은
기계의 주 모니터는 2560×1440이고 OS 배율이 **150%**인데, 프로세스가 매니페스트로
`permonitorv2`를 선언하므로 OS 가상화가 없다 — 에디터는 보정 없이 물리 픽셀 1:1로 그린다.
즉 이 넷은 전부 "DPI 보정 0" 상태의 그림이고, 진짜 DPI 왕복 캡처는 W1이 `FontScaleDpi`
경로를 채택한 뒤에야 의미가 생긴다.

`imguiScale` 기본값이 0.8이라 "100%"를 0.8로, "150%"를 1.2로 잡았다. 배율을 바꾸는 CLI가
없어서 `Dynamic_CPP/ProjectSetting/EngineSettings.asset`의 `imguiScale` 한 줄을 띄우기 전에
고쳤다(캡처 뒤 되돌린다).

### 캡처가 드러낸 것 둘

**① 에디터는 시작 창 크기를 고를 수 없다.** `App.cpp:100-101`이 클라이언트 1920×1080을
**하드코딩**한다. 설정 파일에 `lastWindowSize`가 저장돼 있지만 **읽는 코드가 없다** —
`Tools/build.ps1:1138`이 템플릿에 그 키가 있는지 검사할 뿐이고, 설정 로더가 읽는 키는
`projectName` · `build` · `startupSceneName` · `imguiScale` 넷뿐이다. 죽은 키다.

그래서 2560×1440 캡처는 "그 크기로 뜬 셸"이 아니라 **1920으로 뜬 뒤 런타임에 리사이즈한
셸**이다. `window.resize`가 유일한 경로이기 때문이다.

**② 그 리사이즈가 뷰포트를 검게 만든다.** 2560 캡처에서 Scene·Game 영역이 통째로 검다.
이미 올라와 있는 미결 건("창 크기 변경 뒤 씬 뷰포트가 검게 남는 문제")과 같은 증상이고, 이
캡처가 그 증거다. 배치 자체는 무너지지 않는다 — 오른쪽 열과 아래 열의 비율이 유지된다.

**★ 여기서 한 번 틀렸다가 실측으로 고쳤다.** 처음에는 "리사이즈가 배치를 재배열한다"고
적었다. 그때 캡처에서 Content Browser가 가운데 세로 기둥이 되고 Inspector가 오른쪽 끝으로
밀렸기 때문이다. 그런데 그 캡처는 **ini를 지우지 않고 찍은 것**이었고, 열이 갈린 것은
리사이즈가 아니라 그 기계에 남아 있던 옛 배치 탓이었다. ini를 지우고 같은 리사이즈를 태우니
배치는 그대로였고 검은 뷰포트만 남았다. **한 캡처에 원인 둘이 섞여 있으면 눈으로는 갈리지
않는다 — 변인을 하나씩 지워야 한다.**

**따라서 이 넷을 visual golden으로 쓰지 마라.** W8이 "viewport를 제외한 chrome crop visual
golden"을 만들 때 시작 크기 문제와 검은 뷰포트가 먼저 닫혀 있어야 한다. 지금 이 넷의
용도는 **W1·W4 전의 모습을 남기는 것**뿐이다.

---

## 5. `imgui.ini` fixture

`Tools/regression/fixtures/imgui-ini/`로 옮겼다. 출처와 표는 그 폴더의 `README.md`에 있고,
게이트가 여섯 벌을 태운 기준선은 커밋 `031cf238`의 메시지에 있다.

한 줄만 옮겨 적는다 — **실물 넷이 전부 git 추적 밖이었다.** 막고 있던 것은 `Bin/`을 막는
`.gitignore:94`가 아니라 그 아래의 전면 `*.ini`(:511)였다.

---

## 6. 뒤 슬라이스로 넘긴 것

| 항목 | 넘긴 곳 | 이유 |
|---|---|---|
| 타깃별 GPU ms | W4 | 계측 표면이 저장소에 0이다. 그 수를 실제로 쓰는 판정이 W4에 있다 |
| 진짜 DPI 왕복 캡처 | W1 이후 | 보정 경로가 서기 전에는 재도 같은 그림이다 |
| chrome crop visual golden | W8 | 시작 크기와 리사이즈가 먼저 닫혀야 한다 |
| `undocked`/`ghost`를 legacy ini에서 0으로 | W3 | 이주가 없으면 낡은 파일이 배치를 정한다 |
| 손상 ini 복구 | W3 | `damaged-halfwritten.ini`가 `undocked=4`를 낸다 |
| 시작 창 크기 설정 | 미배정 | `lastWindowSize`가 죽은 키다. 어느 슬라이스도 이것을 적지 않았다 |
