# 렌더링 편집 표면의 수용 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 표면별 책임

[렌더링 표면 정본](../plans/EditorRenderingSurfacesPlan.md)은 저작 설정, 현재 실행 관측, 그래프 표시와 자산 편집을 분리한다. Preferences는 Editor UI scale·브라우저 폭·frame pacing, Project Settings는 General/Build·Graphics·Player 기본값을 다룬다. Graphics의 startup default와 SceneRenderProfile에 저장된 씬 소유 값을 같은 설정으로 보지 않는다. Quality 탭은 완성된 품질 preset 편집기가 아니다.

RenderPass의 그래프는 native C++에서 컴파일되어 제출된 snapshot의 읽기 전용 표시다. 선택한 view와 snapshot이 맞지 않으면 이전 그래프를 정답처럼 계속 보여 주지 않고 대기한다. generation, graph epoch/frame/view, extent, scene/history revision, dependency hash, pass/resource/execute order가 provenance다. queue·alias·subresource range 시각화는 UI에서 미구현이라고 명시한다. 노드를 끌 수 있는 외형이 있다고 런타임 pass 작성·재연결 API로 취급하지 않는다.

Render Debug의 완전한 capture/event/resource inspection은 제공되지 않는다. Rendering Live는 명시적으로 여는 외부 Profiler viewer의 관측 표면이며 recorder의 Record 상태와 GPU timing 수집은 다른 조건이다. workspace 복원이나 preset 선택만으로 외부 process를 실행하지 않는다.

Material Graph는 현재 자산 Create/Open 중심이다. Scene이나 MeshRenderer 선택을 필수 생성 조건으로 두지 않는다. 재질 자산 저작과 이전의 Scene-bound draft session API를 같은 경로라고 설명하면 현재 실행과 옛 runner가 어긋난다.

## 현재 회귀 runner가 실제로 하는 일

`verify-editor-rendering-surfaces.ps1`은 FixtureProject와 새 OutputDirectory가 필요하다. Assets/ProjectSetting/Saved를 복사한 프로젝트를 만들고 Editor를 command service 및 smoke-offscreen 옵션으로 시작한다. endpoint PID를 확인한 뒤 인증된 HTTP 비동기 명령을 보내고 operation 완료를 polling한다. fixture 생성·자산 복사·프로세스 시작을 수행하는 실행 검사이며 읽기 전용 정적 검사로 부르면 안 된다.

등록 창의 orphan/bodyless 상태, Preferences/ProjectSettings/RenderPass/RenderPassDebug focus와 UI frame 진행, recorder 정지 중 GPU timing의 계속 수집, GamePreview, Scene skybox와 전역 environment background 변경을 검사하도록 작성되어 있다. 창 focus는 viewport를 가릴 수 있으므로 render fence 대신 `editor.state.frames`의 UI fence를 쓰는 부분이 중요하다. IBL generation이 background 토글만으로 바뀌지 않는지도 기대한다.

다만 후반부는 여전히 `material.editor open Ground`, document/revision 기반 add/apply, Scene 변경 뒤 retained draft를 전제로 한다. 이 부분은 현재 asset-only UI와 동일한 수용 계약으로 간주할 수 없다. runner 존재만으로 최신 Material Graph가 검증되었다고 쓰지 말고, 실제 등록 API·fixture와의 호환을 확인한 뒤 테스트를 개편해야 한다. 이번 개정은 이를 실행하거나 수정하지 않았다.

## 백엔드와 증거

Editor ImGui host는 `ImGuiDx12Shell`로 고정되어 있다. runner의 Backend 인자가 vulkan을 문법상 허용하더라도 초기에 오류를 던지고 artifact/설정 변경 전에 중단한다. Vulkan 검사는 native RHI probe 또는 Player 경로로 분리한다. Editor DX12 실행을 Vulkan이라는 라벨로 기록해서는 안 된다.

새 수용 결과에는 commit/build, fixture, 실제 명령 결과, UI frame/표시 frame, 선택 view, 캡처 provenance와 정상 종료를 함께 남긴다. 스크린샷만으로 다른 view의 GPU sample을 인증하거나 `ready=true` 하나로 표시 텍스처의 최신성을 인증하지 않는다. 이전 날짜의 성공 요약은 역사 기록이며 이 checkout의 통과 판정은 아니다.

## 구현 근거

- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
- [Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp)
- [Editor/EngineGUIWindow/MaterialGraphWindow.cpp](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [Editor/EngineGUIWindow/ProfilerLiveDiagnosticsBridge.cpp](../../Editor/EngineGUIWindow/ProfilerLiveDiagnosticsBridge.cpp)
- [Editor/EngineEntry/EditorPreferences.h](../../Editor/EngineEntry/EditorPreferences.h)
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp](../../Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp)
- [Tools/regression/verify-editor-rendering-surfaces.ps1](../../Tools/regression/verify-editor-rendering-surfaces.ps1)
