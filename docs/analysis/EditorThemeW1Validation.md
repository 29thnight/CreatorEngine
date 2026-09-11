# PHASE 21 W1 — 테마·폰트·DPI 구현과 검증

2026-09-11. [정본 계획](../plans/EditorWorkspaceRedesignPlan.md)의 W1 후속 기록.

## 구현

- `EditorTheme.h/.cpp`의 14개 semantic color와 logical-pixel geometry가 ImGui style의 정본이다.
  창 본문의 공통 override도 이 토큰을 소비한다. 영상 캔버스의 검정 배경·0 padding,
  정규화 정렬 값, node-editor 자체 데이터 시각화는 별도 의미를 가진 예외다.
- `ApplyEditorTheme`는 기준 style부터 다시 구성하고 geometry에 사용자 배율×OS DPI를 한 번 적용한다.
  `FontScaleMain`은 사용자 배율, `FontScaleDpi`는 모니터 DPI이며 `NewFrame` 전에 모두 설정한다.
  `ConfigDpiScaleFonts`를 켜서 ImGui의 main viewport DPI 갱신과 연결했다. OS multi-viewport는 끈 상태다.
- `WM_DPICHANGED`의 제안 RECT는 창 소유 스레드에서 즉시 적용한다. 수명이 짧은 `lParam` 포인터를
  PresentationThread의 메시지 큐로 넘기지 않는다. 제목표시줄 테두리 판정도 해당 창 DPI를 쓴다.
- **기존 매니페스트 판정 정정:** 계획 부록 B.5의 "에디터는 이미 per-monitor v2라 손댈 것이 없다"는
  결론은 틀렸다. 오래된 `Academy_4Q.exe.manifest`는 빌드에 연결되지 않았으며, 실제 기존 EXE의
  리소스 `#1`에서 추출한 manifest에는 DPI 선언이 없었다. 이번 W1에서 최소
  `Editor/EngineEntry/CreatorEditor.manifest`의 `SMI/2016/WindowsSettings`·`PerMonitorV2` 선언을
  `CreatorEditor.vcxproj`의 `AdditionalManifestFiles`로 연결했다. 검증은 파일 존재에 그치지 않고
  실행 HWND의 `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` 여부를 확인한다.
- Win32 backend가 물리 client/mouse/framebuffer 좌표를 소유한다. framebuffer 배율에 DPI를 다시 곱하지 않는다.
- Inter 4.1 static regular와 OFL 라이선스를 `Resources/Editor/Fonts`에 포함하고 빌드 산출물로 배포한다.
  본문·heading은 같은 Inter를 쓰고, 본문 크기와 icon 크기·baseline 보정은 별도 토큰으로 전달한다.
  필수 본문 폰트는 번들 파일이 없으면 시스템 후보, 마지막에는 ImGui 기본 폰트로 내려간다.
  선택 폰트는 후보가 없으면 현재 기본 폰트를 사용한다. 한글 폰트는 기존 별도 후보를 유지한다.
  monospace ImGui 소비자는 현재 없으므로 별도 폰트 체인은 그 소비자가 생길 때 추가한다.
  배율 변경은 동적 atlas에 맡기며 기존 atlas를 비우거나 직접 rebuild하지 않는다.
- `editor.theme`는 토큰·스타일 값, OS/viewport/font DPI, geometry 일치, 실제 폰트 출처를 보고한다.
  `editor.selftest`에는 전역 ImGui 문맥을 건드리지 않는 지역 style 계약 검사가 연결돼 있다.

## Override inventory

HEAD `8fb4267f`와 작업 트리의 `.cpp/.h/.inl`을 같은 기준으로 비교했다.
아래는 주석·문자열을 제외한 `ImGui::PushStyleColor/Var`의 정적 호출 지점 수이며 실행 횟수가 아니다.

| 범위 | 변경 전 Color / Var | 변경 후 Color / Var | 합계 |
|---|---:|---:|---:|
| `Editor/EngineGUIWindow` | 24 / 23 | 17 / 15 | 47 → 32 |
| `Editor` 전체 | 26 / 25 | 19 / 17 | 51 → 36 |

W0의 57건은 Editor 전체에서 주석 6건을 포함한 수다. 범위와 주석 기준이 달라 47과 바로 비교하지 않는다.
EngineGUIWindow 밖 4건은 asset preview와 창 선언의 background/padding 적용 지점이며 호출 수는 유지했다.
별도 `ed::` node-editor 스택 20건도 이 표에 섞지 않는다.

남은 예외는 영상/도크의 0 padding, 검색 아이콘과 입력란의 간격 0, 정규화된 `(0,.5)` 정렬,
충돌 행렬의 헤더 각도, 노드 핀의 밀착 배치다. Scene overlay는 Panel/Primary 토큰에 투명도만 더한다.
`TextDisabled=#999999`의 불투명 색상 대비는 Canvas 6.23:1, Panel 4.37:1, PanelRaised 3.21:1,
Selection 2.74:1이다. 이는 토큰 RGB의 계산값이며 ImGui disabled alpha가 적용된 화면 대비 검증은 아니다.

## 검증 범위

`verify-editor-theme.ps1`은 DX12/Vulkan 각각 사용자 배율 100→150→100%로 세 번 기동한다.
제품에서 실제 Inter 소비, 실행 HWND PMv2 상태, DPI 출처 일치, geometry 및 색상 연결을 확인한다.
설정과 ini는 원본 바이트로 복원한다.
이 자동 검사는 시작 배율 검증이다. 별도 Debug UI 검증에서는 Settings의 ImGuiScale을
한 프로세스 안에서 100→150→100%로 바꿔 팝업·글꼴·아이콘·간격의 확대와 복귀를 확인했다.
증거는 `Artifacts/phase21-w1-live-user100.png`, `phase21-w1-live-user150.png`,
`phase21-w1-live-user100-return.png`다. 두 배율 축의 독립·결합 계약은 지역 style selftest도 확인한다.

실제 모니터 둘은 모두 144 DPI(150%)다. OS 설정을 변경하지 않았으므로 실제 DPI 100↔150% 왕복은 미검증이다.
지역 style의 합성 DPI 검사와 실제 모니터 왕복을 같은 증거로 취급하지 않는다.

최종 검증 결과:

| 검사 | 결과 | 로컬 증거 (`Artifacts/`) |
|---|---|---|
| VS18/v145 Debug x64 Build | 통과, 경고 0 / 오류 0 | `phase21-w1-pmv2-debug-build.log` |
| VS18/v145 Release x64 Build | 통과, 경고 1 / 오류 0 | `phase21-w1-pmv2-release-build.log` |
| Debug/Release 실제 EXE manifest 추출 | PMv2 선언과 asInvoker 병합 확인 | `phase21-w1-embedded-{debug,release}.manifest` |
| Debug DX12/Vulkan 테마 | 6기동 / 103검사 통과 | `phase21-w1-pmv2-theme-debug/` |
| 지역 style selftest | 매 기동 183검사 통과 | 위 폴더 `*.jsonl`의 `themeReport` |
| Debug 창·메뉴 선언 배선 | 33검사 통과 | `phase21-w1-declaration-debug/` |
| Debug workspace | ini 6종 / 108검사 통과 | `phase21-w1-workspace-debug/` |
| Release DX12/Vulkan 테마 | 6기동 / 103검사 통과 | `phase21-w1-pmv2-theme-release/` |
| obsolete surface | 939 source / 27 call site / 위반 0 | `verify-imgui-obsolete-surface.ps1` 실행 |
| 정적 형식·등록 | diff check, XML 5개, PowerShell 4개, 신규 파일 등록 통과 | 프로젝트·스크립트 소스 |

Debug/Release의 실제 OS/viewport/font DPI는 모두 1.5이며 사용자 배율 1.0→1.5→1.0에서
본문 크기가 24→36→24px로 돌아왔다. Inter 실소비, icon 병합, 누락 glyph 0, fallback probe,
토큰 mapping과 geometry 일치도 통과했다. 테마 검사 및 선언/workspace 묶음 종료 후
설정과 사용자 ini를 원본 바이트로 복원했다.

Release 경고 1건은 기존 `LNK4229 /DELAYLOAD:vulkan-1.dll`이다.
W0 화면·성능 수치는 이전 스킨과 폰트의 기준선으로 보존하며,
W1 결과를 W8의 최종 visual golden 또는 성능 회귀 판정으로 대신하지 않는다.
