# DX12 resize 수용: 자원 전환과 표시 증거

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 코드에서 확인한 수정 경계

`EnhancedSceneRenderer::ResizePipeline`은 GPU drain 뒤 크기에 의존하는 display slot, graph/transient 자원, SSGI history를 교체한다. PSO·ShaderMeta variant·IBL·고정 fog volume은 보존한다. render target은 이제 창 전체 대신 ViewportHost canvas와 render scale을 통해 정해지므로 2026-09-12의 고정 창 크기 시험과 입력 조건이 다르다. 구조와 남은 진단 요구는 [원인 분리 문서](EditorW1DpiResizeRootCauseAndFixPlan.md)에 둔다.

완료 결과와 UI texture 조회는 scene epoch와 resize generation을 확인한다. pipeline이 ready이고 framesRendered가 늘어도 UI에 올바른 새 texture가 보인다는 보장은 없다. `lastTextureAvailable`, completed frame/generation, 실제 texture generation 및 unavailable/recovered 로그를 함께 본다. 새 pipeline 준비, GPU 완료, UI 소비는 다른 사건이다.

ImGui host swapchain도 별도로 drain 후 ResizeBuffers한다. inactive/0/동일 크기는 shell에서 건너뛰고 성공 때만 shell의 cached extent를 바꾼다. 실패 시 살아 있는 resources와 frame 상태를 확인해야 하며 단순히 다음 resize 요청이 성공할 것이라고 가정하지 않는다.

## 현재 실행 시나리오

`verify-editor-viewport-extent.ps1`은 isolated workspace/legacy ini 환경변수를 사용하고 기존 Editor 실행 파일에 명령 script를 준다. scale off, auto, fixed 0.5를 적용한 뒤 각 상태를 여러 번 polling해 `dx12.live` extent가 canvas×scale에 수렴하는지 본다. 1600×900, 2200×1200, 1280×800, 2560×1440 요청을 이어 보내고 generation 진행·최신 texture·렌더 frame 증가·정상 종료를 확인한다. 별도 입력으로 scale 2.0/0.1 거부를 검사한다.

창 크기는 요청값이 아니라 `window.resize` 결과의 실제 client 값을 사용한다. OS가 화면 경계/DPI 조건에 맞춰 제한할 수 있기 때문이다. 유한 polling 안에서 어느 표본에 수렴했는지도 기록한다. 이는 자동 검사 timeout이며 제품의 무조건적인 최대 지연 보장은 아니다. game-thread `wait`만으로 Presentation frame 완료를 단정하지 않는다.

## 역사 측정의 의미

이전 2026-09-12 기록은 수정 전 Scene 공백 8,578.926ms, IBL만 유지한 후보 14,649ms와 size-resource 수정 후의 실제 OS DPI 왕복을 보고했다. 당시 150→100% 공백은 Debug 118.492ms, Release 62.719ms로 기록되어 있다. 이 숫자는 당시 실행 파일·GPU·작업공간에서 관측한 공백이고 현재 성능 목표나 재측정 결과가 아니다.

그 기록이 언급한 `verified-*-resize.json`, `verified-*-dpi*.json/png`, `restore-proof.json` 등은 외부 실행 산출물 이름이다. 현재 checkout에 첨부된 최신 결과인 것처럼 링크하거나 현재 통과 횟수로 인용하지 않는다. 원래 역사 자료를 확보했다면 configuration, 당시 commit, OS scale, actual extent와 원복 증거까지 함께 해석해야 한다.

## 아직 별도로 확인할 것

- 같은 DPI에서 연속 resize와 실제 OS DPI 전환을 분리한다. user scale은 세 번째 독립 변수다
- Scene/GamePreview 양쪽의 active/ready와 새 generation을 확인한다. 숨겨진 Game target 수요 억제는 resize 성공과 다른 계약이다
- 최소화의 0 extent, 복원, 창 가림, GPU 지연 및 종료 중 전환을 시험한다
- 준비 배경 허용 구간과 장시간 공백의 기준을 별도로 정하고 최초 실패 로그를 보존한다
- UI 배치·font 건강과 GPU 표시 건강을 둘 다 판단한다

현재 Editor의 공식 표시 host는 DX12다. 이 문서의 DX12 수용을 Vulkan Editor 지원이나 Player Vulkan swapchain 검증으로 확장하지 않는다. 이번 개정에서는 실행 파일을 만들거나 runner를 실행하지 않았다.

## 구현 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/RHI/ScreenSizedResource.h](../../Engine/RenderEngine/RHI/ScreenSizedResource.h)
- [Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp](../../Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp)
- [Editor/HostImGuiPresentation/RHI/DX12/ImGuiDx12Shell.cpp](../../Editor/HostImGuiPresentation/RHI/DX12/ImGuiDx12Shell.cpp)
- [Editor/EngineGUIWindow/ViewportHostWindow.cpp](../../Editor/EngineGUIWindow/ViewportHostWindow.cpp)
- [Tools/regression/verify-editor-viewport-extent.ps1](../../Tools/regression/verify-editor-viewport-extent.ps1)
