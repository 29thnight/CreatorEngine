# 에디터 워크스페이스: 선언·저장·뷰포트·입력 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

<a id="phase21-current-status"></a>
## 1. 현재 구조와 이 문서의 역할

이 문서는 누적 W0~W9 작업 일지 대신 현재 Editor의 책임 분리를 설명한다. 메뉴와 창은 명시적 중앙 목록에서 등록되고, 표시 상태는 window_table이 소유한다. 중앙 창은 ViewportHost 하나이며 Scene/Game은 그 안의 모드다. workspace는 stable ID와 도킹 ini, panel open 상태, 모드, preset 및 모니터 환경을 함께 저장한다. 현재 Editor ImGui 호스트는 DX12이며 Player의 backend 선택과 분리된다.

현재 없는 기능도 명확하다. 하단 Cmd는 입력 UI만 있고 실행·이력·완성이 연결되지 않았다. Revision Control은 비활성 안내다. Render Frame Debugger의 frame capture/event inspection은 미구현이며 Quality preset도 미구현이다. 반면 RenderPass의 compiled graph 읽기와 Scene 대상 없는 material 자산 저작은 이미 존재하므로 과거의 미래 요구로 남겨두지 않는다. 상세 설정은 [렌더 표면 정본](EditorRenderingSurfacesPlan.md)에 있다.

## 2. 기동과 표시 상태의 소유권

메뉴는 `RegisterEditorMenuManual.h`의 중앙 목록, 창은 `RegisterEditorWindowManual.h`의 중앙 목록에서 선언을 실제 registry로 내린다. static library에서 독립 TU 자기 등록이 링크 누락되는 위험을 피하기 위한 구조다. 선언을 추가하는 것과 중앙 목록·draw body를 연결하는 것은 모두 필요하다.

`window_entry`는 stable_id, 표시 label, role, dock slot, traits, sizing, padding/background, availability, draw 함수와 open 값을 가진다. open은 표시 상태의 정본이다. stable ID는 `###Editor.*` 형식이며 아이콘·표시 이름·번역이 바뀌어도 배치 신원을 유지한다. 없는 ID 조회는 nullptr이고 잘못된 이름이 유령 창을 생성하지 않는다.

직접 open/close는 UI 스레드 경로다. 게임 스레드 명령은 `queue_window_request`로 open/close/focus를 예약하고 UI가 프레임 시작에 적용한다. 탭이 겹친 창은 open=true여도 선택되지 않으면 본문이 안 돌 수 있으므로 focus와 open은 다르다. 셸은 Begin/End와 창 단위 계측을 소유하고 draw body는 본문만 그린다.

## 3. 프레임과 좌표 경계

`EditorRenderer`는 workspace BeforeFrame, theme/host frame 시작, dockspace와 메뉴, 등록된 창, 상태 게시 및 frame 종료를 조율한다. `ImGuiHost`가 실제 HWND client 크기로 shell resize를 요청하고 Win32 backend가 display/framebuffer 좌표를 설정한다. PMv2의 client 좌표에 OS DPI를 framebuffer 배율로 다시 곱하지 않는다.

`ApplyEditorTheme`는 매번 기본 style에서 userScale×dpiScale을 적용해 누적 scaling을 피한다. 글꼴은 FontScaleMain과 FontScaleDpi가 각 축을 담당한다. 대부분 geometry는 logical px이지만 title row의 45px은 최종 framebuffer의 최소 높이다. 본문 16px 글꼴/20px control과 24px tab의 padding 계약을 섞지 않는다.

WndProc는 ImGui context를 읽지 않는다. 제목 표시줄의 drag 좌표는 atomic으로 전달하고 시스템 창 명령은 WM_SYSCOMMAND를 게시한다. WM_DPICHANGED의 suggested RECT는 메시지 수명 안에서 창 소유 스레드가 즉시 적용한다. message queue에 포인터를 넘겨 나중에 사용하지 않는다.

## 4. ViewportHost와 렌더 수요

ViewportHost는 닫을 수 없는 중앙 창이며 Scene/Game tab을 그린다. 별도 Game Preview는 선택적 패널이다. UI가 발표하는 viewport_demand에는 모드, game target 수요, canvas physical extent, DPI/render scale, focus/hover, ImGui capture 상태와 game canvas click 누계가 들어간다. App의 view 제작자가 이 게시본을 읽는다.

현재 gameTarget은 Host Game 또는 Game Preview가 실제 그려졌을 때 켜진다. editorTarget은 항상 true로 남아 있으므로 “숨은 Scene과 Game을 대칭으로 모두 끈다”는 설명은 틀리다. 수요 억제와 카메라 부재로 view를 만들지 못한 경우는 계수를 구분한다.

Host는 content region×DisplayFramebufferScale로 canvas 크기를 측정한다. 그 프레임에 host가 그려지지 않았다고 0 extent를 게시하지 않고 마지막 유효값을 보존한다. render scale은 dpi_auto(1/DPI), off(1.0), fixed 정책이다. 낮은 render scale에서 표시 영역 자체가 줄어들면 안 된다.

`ViewportCanvas`가 content/image/clip/UV를 한 번 계산한다. Scene은 extent에 맞는 타깃을 fill하고 Game은 종횡비를 유지하는 letterbox를 사용한다. picking은 SourceUV, gizmo는 같은 image rect를 사용한다. overlay는 화면에 보이는 content를 기준으로 놓고 입력 소유를 먼저 판단한다. 오래된 crop 설계는 [호환 설명](../analysis/EditorSceneCropValidation.md)에 남긴다.

## 5. workspace 저장·복구

`EditorWorkspaceStore`는 창 등록 완료 후 생성되어야 한다. 빈 window table은 기동 순서 오류로 거부한다. 기본 파일은 RuntimeDataPath의 `Editor/Workspaces/active.workspace`이며 `CREATOR_EDITOR_WORKSPACE_DIR`로 테스트 위치를 격리할 수 있다. legacy ini는 migration 입력이고 backend가 별도로 ini를 쓰지 않도록 store가 ownership을 가진다.

현재 schema는 3, 읽을 수 있는 가장 오래된 schema는 1, 파일 상한은 4MiB다. v1에는 preset이 없고 v3 이전에는 monitors가 없다. 화면 구성 fingerprint를 모르면 저장된 외부 viewport 좌표를 신뢰하지 않는다. `ViewportPos`/`ViewportId`를 제거해 화면 밖 창을 복구하면서 도킹 정보는 보존한다. 창 display label은 legacy alias를 통해 stable ID로 이주한다.

Save/Load/Reset/Open/Close/ApplyPreset 및 named workspace SaveAs/Load/Rename/Delete는 단일 pending action mailbox를 거친다. 이미 요청이 있으면 추가 요청을 거부한다. 파일 쓰기는 atomic publication과 backup 경로를 사용한다. 이름은 64자 이하의 안전한 파일명이며 active, Windows 장치명, 경로 구분자·예약 문자 등을 거부한다. 삭제는 현재 화면 배치를 없애는 동작과 다르다.

preset은 sbox_compact, level_editing, ui_editing, rendering_debug, legacy_unity 다섯 개다. central 창은 preset으로 닫을 수 없고 transient는 dock하지 않는다. Rendering & Debug preset이 외부 ProfilerViewer 프로세스를 자동 실행하지 않으며 명시적인 Profiler 열기만 viewer launch를 요청한다.

<a id="w2-inspector-layout"></a>
## 6. Inspector와 공통 위젯

`property_sheet`와 `measure_property_layout`은 가용 content 폭, 고정 라벨 폭, 값 최소 폭, 보조 버튼 공간을 계산한다. 값 편집은 표준 ImGui 위젯 및 기존 Undo 경로를 사용한다. 좁은 폭은 inline→stacked, 필요 시 축별 stacked로 바뀌며 hysteresis와 active 편집 중 모드 유지로 드래그 도중 ID/위치가 흔들리지 않게 한다.

컴포넌트 패널, 섹션, 축 필드, mode button은 각각 작은 공통 구현을 가진다. 이 계층은 엔티티 수정 권한이나 자산 저장을 대신하지 않는다. 선택 이력은 scene/generation handle 128개, 아이콘은 stable preset ID, 편집 잠금은 제품 mutation 경로에서 검사한다. [Inspector 배치](../analysis/EditorInspectorLayoutW2I.md), [컴포넌트 탐색](../analysis/EditorAddComponentBrowserValidation.md), [선택·잠금](../analysis/EditorEntityNavigationValidation.md)을 참조한다.

## 7. Content Browser·Hierarchy·로그 비용

브라우저는 directory snapshot의 generation, 명시 invalidation, age와 재스캔 예산을 사용한다. cache hit가 아무 디스크 호출도 없다는 보장은 아니므로 scans와 probes를 따로 센다. 검색·필터·정렬은 창이 소유하며 listing cache는 파일 목록과 metadata만 제공한다.

Hierarchy는 HierarchyStore revision, entity/DDOL 수, 접힘 상태로 평탄 목록을 만들고 clipper가 보이는 행만 그린다. 검색 중에는 이름 revision이 없으므로 fail-safe rebuild한다. 이름·아이콘·잠금·활성·선택은 cache에 복제하지 않고 현재 정본에서 읽는다. 형제 순서는 별도 [순서 계약](../analysis/HierarchyOrder20261008.md)을 따른다.

Output Log는 bounded LogStore의 delta를 UI 모델이 받아 필터·Collapse 목록을 만든다. 상태 바 심각도 수는 동일 store의 누계이며 화면의 보유 행 수와 다르다. `editor.panelcost`는 패널 내부/창 전체/셸/프레임을 구별한다. 서로 포함 관계인 시간을 더하면 이중 계수된다.

## 8. 재검증과 남은 요구

정적 선언 검사, 순수 레이아웃 검사, commandlet/HTTP 제품 상태, 실제 Windows 입력 및 screenshot, CPU/GPU 성능 측정은 각각 다른 증거다. W0 과거 숫자를 현재 baseline으로 쓰지 않는다. source·binary hash와 장비·DPI·창 크기·scene·warmup/측정 구간을 함께 남긴다.

미구현 요구는 Cmd 실행/완성/이력의 실제 명령 서비스 연결, Revision Control 통합, Quality preset 저장·패키징·Player 소비, frame capture debugger다. Preferences는 현재 EngineSettings.asset에 저장되는 항목이 있으므로 완전한 사용자 전역/프로젝트 독립 저장소 설계를 완료했다고 보지 않는다. 다중 모니터 복구, narrow/DPI, stale async 완료, 작은 worker pool, 종료 및 장시간 회귀는 최신 binary로 다시 실행해야 한다. 렌더 준비 완료와 GPU fence 완료는 계속 구분한다.

## 구현 근거

- [Editor/EditorWindow/EditorWindowRegistry.h](../../Editor/EditorWindow/EditorWindowRegistry.h)
- [Editor/EditorWindow/RegisterEditorWindowManual.h](../../Editor/EditorWindow/RegisterEditorWindowManual.h)
- [Editor/EditorWindow/EditorWindowHost.cpp](../../Editor/EditorWindow/EditorWindowHost.cpp)
- [Editor/EditorMenu/RegisterEditorMenuManual.h](../../Editor/EditorMenu/RegisterEditorMenuManual.h)
- [Editor/EngineGUIWindow/EditorRenderer.cpp](../../Editor/EngineGUIWindow/EditorRenderer.cpp)
- [Editor/EngineGUIWindow/ViewportHostWindow.cpp](../../Editor/EngineGUIWindow/ViewportHostWindow.cpp)
- [Editor/EngineGUIWindow/EditorViewportCanvas.h](../../Editor/EngineGUIWindow/EditorViewportCanvas.h)
- [Editor/EngineGUIWindow/EditorWorkspaceStore.cpp](../../Editor/EngineGUIWindow/EditorWorkspaceStore.cpp)
- [Editor/EngineGUIWindow/EditorWorkspaceFile.h](../../Editor/EngineGUIWindow/EditorWorkspaceFile.h)
- [Editor/EditorWindow/EditorLayoutPreset.cpp](../../Editor/EditorWindow/EditorLayoutPreset.cpp)
- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp](../../Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp)
- [Editor/EngineEntry/EditorSettingsStore.cpp](../../Editor/EngineEntry/EditorSettingsStore.cpp)
- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
