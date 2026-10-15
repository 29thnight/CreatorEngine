# W0 역사 기준선과 현재 비교 절차

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 보존된 기준선의 정체

W0는 2026-09-11, `031cf238` 직후 Release Editor와 당시 초기 도킹 배치를 기록한 기준선이다. 당시 창 선언 25개·본문 22개, 기본 배치의 ImGui 정점 1,688/인덱스 3,858/draw command 12, UI CPU 중앙값 0.38ms가 기록되었다. 이 수는 현재 registry 개수·최소 비용·통과 기준이 아니다. current ViewportHost, theme, workspace 저장소와 추가 창이 생긴 뒤 같은 숫자를 강제하면 정상 변화를 회귀로 판정한다.

당시 UI 시간은 BeginRender~EndRender이며 GPU 제출/Present를 포함하지 않았다. 옛 ini를 그대로 사용한 실행과 ini 없는 초기 배치 실행은 열린 창이 달라 비교 모수가 달랐다. baseline의 핵심은 낮은 수치 자체가 아니라 측정 대상의 배치를 재현하는 조건이다. 현재 `EditorPanelCost`의 shell/present 장부와 당시 단일 UI CPU 값을 합치거나 같은 범위라고 가정하지 않는다.

## 그대로 남기는 이미지

다음 네 파일은 W1 테마 및 고정 중앙 ViewportHost 도입 전의 역사 이미지다. 이번 작업에서 바꾸지 않았다.

- [1920 요청·user scale 0.8](images/w0-shell/shell-fresh-1920x1080-scale0.8.png)
- [1920 요청·user scale 1.2](images/w0-shell/shell-fresh-1920x1080-scale1.2.png)
- [2560 요청·user scale 0.8](images/w0-shell/shell-fresh-2560x1440-scale0.8.png)
- [2560 요청·user scale 1.2](images/w0-shell/shell-fresh-2560x1440-scale1.2.png)

원래 기록의 actual client는 각각 1920×1111 또는 2560×1471이며 OS 150%에서 user scale을 바꾼 캡처다. 실제 OS DPI 왕복 검사가 아니고, 당시 큰 창의 검은 viewport도 포함한다. 현재 chrome의 visual golden으로 승격시키거나 이미지의 검은 영역을 최신 resize 실패 증거로 제시하지 않는다.

legacy ini 자료는 [fixture 안내](../../Tools/regression/fixtures/imgui-ini/README.md)에 있다. 손상/과거 경로 fixture는 이주·복구 입력이며 정상 default workspace의 정본은 아니다.

## 현재 측정의 출발점

창 목록은 `RegisterEditorWindowManual.h`와 stable ID registry, 실제 결속은 host/audit로 확인한다. default preset 또는 named workspace를 명시하고 `editor.windows`, `editor.dock`, `editor.state`, `editor.panelcost`, `editor.viewport`를 같은 실행에서 수집한다. 창 open 상태와 실제 그려진 본문은 다르므로 숨겨진 탭·tool occlusion도 기록한다.

현재 저장은 RuntimeData의 Editor/Workspaces를 사용한다. 테스트는 `CREATOR_EDITOR_WORKSPACE_DIR` 및 legacy ini override로 사용자 배치와 격리한다. 예전처럼 사용자 ini를 삭제하는 방법을 새 기본 절차로 요구하지 않는다. preset, viewport 모드, named/active workspace, monitor fingerprint와 actual client/DPI/user scale을 함께 기록해야 초기 배치 비교가 의미가 있다.

성능은 [패널 비용 계약](EditorPanelCostBaselineW7.md)의 최근 표본/누계/중첩 범위를 따른다. GPU는 현재 제공되는 live timing의 view/frame/extent provenance를 확인한다. W0 당시 “타깃별 GPU 관측 수단 없음”이라는 조사 결과를 지금도 없는 기능이라고 재사용하지 않는다. 반대로 GPU pass sum을 전체 프레임 시간으로 바꾸어 쓰지 않는다.

## golden과 새 수용

현재 저장소의 [chrome 환경](../../Tools/regression/golden-editor-chrome/environment.json), [성능 golden](../../Tools/regression/editor-chrome-perf.golden.json), base/nested/revealed 이미지는 기존 측정 증거다. `verify-editor-chrome-golden.ps1`과 `verify-editor-chrome-perf.ps1`의 실제 비교 범위를 읽고 환경을 맞춘다. golden 갱신은 코드 변경의 이유와 새 실행 증거를 검토하는 별도 작업이며 문서 최신화 과정에서 숫자·이미지를 덮어쓰지 않는다.

아직 필요한 비교 축은 스크롤된 Hierarchy, 검색/캐시 무효화 spike, 여러 DPI 및 좁은 창, 실제 UI 입력과 workspace 복구다. W0 화면 하나 또는 현재 소스의 clipper 존재가 이 모든 수용을 완료하지 않는다. 이번 개정은 역사 자료의 역할과 현재 재현 경로만 정리했으며 새 기준선 측정은 없다.

## 구현 근거

- [Editor/EditorWindow/RegisterEditorWindowManual.h](../../Editor/EditorWindow/RegisterEditorWindowManual.h)
- [Editor/EditorWindow/EditorWindowRegistry.cpp](../../Editor/EditorWindow/EditorWindowRegistry.cpp)
- [Editor/EditorWindow/EditorWindowHost.cpp](../../Editor/EditorWindow/EditorWindowHost.cpp)
- [Editor/EngineGUIWindow/EditorPanelCost.cpp](../../Editor/EngineGUIWindow/EditorPanelCost.cpp)
- [Editor/EngineGUIWindow/EditorWorkspaceStore.cpp](../../Editor/EngineGUIWindow/EditorWorkspaceStore.cpp)
- [Tools/regression/verify-editor-workspace.ps1](../../Tools/regression/verify-editor-workspace.ps1)
- [Tools/regression/verify-editor-chrome-golden.ps1](../../Tools/regression/verify-editor-chrome-golden.ps1)
- [Tools/regression/verify-editor-chrome-perf.ps1](../../Tools/regression/verify-editor-chrome-perf.ps1)
- [Tools/regression/fixtures/imgui-ini/README.md](../../Tools/regression/fixtures/imgui-ini/README.md)
