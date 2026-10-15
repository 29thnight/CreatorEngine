# W1 테마·DPI 계약과 검증 구분

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 배율과 토큰

`ApplyEditorTheme(userScale,dpiScale)`는 기존 style을 계속 곱하지 않는다. FontSizeBase를 보존한 뒤 새 ImGuiStyle에서 기본 토큰과 색을 세우고 유효한 `FontScaleMain`/`FontScaleDpi`를 설정한 후 그 곱으로 크기를 한 번 변환한다. 유한 양수가 아닌 scale은 1로 정규화한다. 사용자 배율 왕복 뒤 간격이 누적 팽창하지 않는 것이 핵심 불변식이다.

기본 논리 토큰은 row/control 20, tab 24, body/icon font 16, small 12, xsmall 10, panel padding 3×2, tree indent 20, scrollbar 8이다. `ThemePixels`는 논리 값을 두 font scale의 곱으로 변환한다. Inspector·Hierarchy는 별도의 scoped 색·간격을 사용하지만 전역 테마의 배율 출처는 공유한다. 색상은 theme token enum과 RGB 표를 통해 이름/값을 조회한다.

제목표시줄의 45는 **물리 픽셀 최소 높이**다. 일반 논리 토큰처럼 DPI/user scale을 다시 곱하지 않는다. font와 safe padding이 더 크면 행이 높아질 수 있다. 탭은 24 기준의 세로 padding을 임시 적용하고 복구하며 NewFrame을 넘는 ImGui style-stack 항목을 보유하지 않는다.

## Host와 DPI

CreatorEditor 매니페스트는 PerMonitorV2다. Host는 GetDpiForWindow/96로 DPI를 관측하고 font DPI scaling을 초기화한다. PMv2 client·mouse·render 좌표는 이미 물리 픽셀이므로 DisplayFramebufferScale에 DPI를 한 번 더 곱하지 않는다. DPI는 font와 UI geometry의 scale에 반영한다. OS가 보내는 WM_DPICHANGED의 suggested RECT는 창 소유 스레드에서 즉시 적용한다.

Docking/keyboard navigation 설정은 backend 초기화 전에 세운다. 플랫폼/렌더 capability flags는 backend가 소유한다. 현재 분리된 native multi-viewport는 활성화하지 않는다. docking 지원만 보고 패널을 메인 창 밖의 별도 OS 창으로 꺼낼 수 있다고 안내하면 안 된다. WantCaptureMouse/Keyboard/WantTextInput은 ImGui 출력이며 매 프레임 강제로 true로 설정하지 않는다.

## 자동 검사와 실제 DPI 조작은 다르다

`verify-editor-theme.ps1`은 기본 DX12 Editor를 user scale 1.0→1.5→1.0으로 별도 실행하고 `editor.selftest` 및 `editor.theme`의 token/font/DPI 값을 대조한다. selftest의 합성 DPI 왕복은 실제 모니터 이동이 아니다. runner는 기존 Editor가 있으면 거부하고 설정 파일 및 ini 바이트를 보존/복원한다. 실제 실행 때 해당 side effect와 복원 결과를 확인해야 한다.

Backends 인자는 vulkan을 받는 문법이 남아 있으나 초기 거부한다. 현재 Editor host는 DX12-only다. Vulkan 렌더 검증은 Player/native probe에서 수행한다. 과거 Vulkan UI 결과를 이번 W1 상태로 합치지 않는다.

## 미완료 수용과 관련 문서

실제 서로 다른 DPI 모니터 이동, Windows 배율 변경, 최대화/복원, 한글·아이콘 atlas 동적 크기, popup·tooltip 및 좁은 창의 hit area는 [대화형 수용](EditorW1InteractiveValidation.md)에서 확인한다. [제목표시줄](EditorTitleBarValidation.md), [DX12 크기 전환](EditorW1Dx12ResizeValidation.md)을 함께 본다. 이번 개정은 새 실행을 하지 않았으므로 과거 합격 횟수나 화면 캡처를 최신 결과로 재선언하지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorTheme.h](../../Editor/EngineGUIWindow/EditorTheme.h)
- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Editor/EngineGUIWindow/EditorWindowChrome.cpp](../../Editor/EngineGUIWindow/EditorWindowChrome.cpp)
- [Editor/EngineEntry/CreatorEditor.manifest](../../Editor/EngineEntry/CreatorEditor.manifest)
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp](../../Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp)
- [Tools/regression/verify-editor-theme.ps1](../../Tools/regression/verify-editor-theme.ps1)
