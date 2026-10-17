# 네이티브 제목표시줄과 ImGui 크롬 연결

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 표시와 시스템 동작

`ComposeEditorWindowTitle()`은 projectName을 UTF-8에서 wide 문자열로 바꿔 `<프로젝트> - Creator Engine`을 만든다. 이름이 없으면 Creator Engine이다. FPS·해상도·backend는 제목에 섞지 않고 관련 진단 표면에서 본다. 메뉴 줄은 제목·Play와 시스템 버튼이 공유하며 제목이 메뉴 또는 Play 영역과 겹치면 제목을 그리지 않는다. 좁은 창에서 버튼을 덮어 읽을 수 없게 만드는 것보다 명시적인 생략을 선택한다.

`LayoutEditorTitleBar`의 논리 폭은 시스템 버튼 각각 32, Play 56, 사이 간격 8, Play inset 2에 scale을 적용한다. 높이는 테마가 계산한 행 높이를 받는다. 제목줄은 최소 45 physical pixels를 확보하지만 큰 글꼴/safe area로 더 커질 수 있다. “어떤 DPI에서도 정확히 45 logical pixels”가 계약은 아니다.

최소화·최대화/복원·닫기 버튼은 Presentation에서 동기 ShowWindow를 호출하지 않고 HWND에 WM_SYSCOMMAND를 게시한다. 실제 창 조작은 메시지 펌프가 있는 창 소유 스레드가 처리한다. 테두리 없는 Player 창 정책과 별도의 Editor 전용 구현이다.

## Non-client 영역과 hit test

`Attach`는 이미 생성된 HWND에 SWP_FRAMECHANGED를 적용해 non-client 계산을 다시 한다. WM_NCCALCSIZE에서 상단 native caption을 없애되 Windows가 계산한 좌·우·하단 resize frame은 유지한다. 최대화 때는 상단 border 두께만 남겨 제목이 화면 밖으로 잘리거나 예전 caption 높이의 빈 띠가 남지 않게 한다.

WM_NCHITTEST는 먼저 Windows의 frame 판정을 따른다. 상단 caption을 제거하며 client가 된 위쪽 resize band와 두 corner는 DPI별 system metric으로 복원한다. 최대화 상태에서는 위쪽 resize를 적용하지 않는다. 나머지 제목 행의 빈 drag 영역만 HTCAPTION으로 응답한다. 메뉴·Play·시스템 버튼 영역을 통째로 caption으로 만들어 클릭이 창 drag로 빠지지 않아야 한다.

WndProc는 ImGui 컨텍스트를 읽지 않는다. Presentation은 title height와 drag 좌우 bounds를 atomic 값으로 게시하고 WndProc는 그 값만 읽는다. WM_DPICHANGED의 RECT 포인터는 메시지 호출 동안만 유효하므로 다른 스레드 큐에 포인터를 보관하지 않고 즉시 SetWindowPos에 사용한다.

## 수용 기준

텍스트가 긴/빈/한글 프로젝트, 좁은 창, user scale/DPI 조합, 메뉴 끝과 drag 경계, 버튼 press, 상단과 두 corner resize, 최대화/복원, 더블클릭 caption 및 모니터 이동을 실제 창에서 확인한다. hover 색이나 screenshot만으로 native hit-test를 인증하지 않는다. HWND 소유 스레드 메시지와 Presentation geometry의 시점 차이도 반복 resize에서 관찰한다.

이전 제목표시줄 검증은 당시 디자인과 OS 조건의 역사 기록이다. 현재 수정의 정적 근거는 위 두 경로의 연결이며 새 UI 실행은 없다. 테마 검사는 [W1 테마](EditorThemeW1Validation.md), 크기 전환과 표시 준비는 [DX12 resize](EditorW1Dx12ResizeValidation.md), 작업공간 저장은 [정본](../plans/EditorWorkspaceRedesignPlan.md)에 둔다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorWindowChrome.h](../../Editor/EngineGUIWindow/EditorWindowChrome.h)
- [Editor/EngineGUIWindow/EditorWindowChrome.cpp](../../Editor/EngineGUIWindow/EditorWindowChrome.cpp)
- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
- [Editor/EngineEntry/EditorSettingsStore.cpp](../../Editor/EngineEntry/EditorSettingsStore.cpp)
- [Editor/EngineEntry/CreatorEditor.manifest](../../Editor/EngineEntry/CreatorEditor.manifest)
