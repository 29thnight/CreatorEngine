# DPI·resize 표시 전환: 현재 원인 분리와 남은 요구

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 상태로 읽는 방법

이 문서는 2026-09-12의 긴 Scene 공백 및 옛 Vulkan Editor device-loss 분석을 현재 수명 경계로 다시 정리한다. 당시의 무잠금 width/height 읽기와 매 resize 전체 DX12 pipeline 재구축을 현재 코드의 결함처럼 재서술하지 않는다. 현재는 크기 snapshot과 size-dependent 자원 교체가 구현되어 있다. 그렇다고 그 사실만으로 모든 창·GPU·드라이버 조합의 resize가 재검증되었다는 뜻은 아니다.

현재 Editor 표시 host는 DX12-only다. 이전 Vulkan Editor 경로의 원인 가설은 현재 제품 지원 기능이나 이미 해결된 결과로 취급하지 않는다. Vulkan extent/acquire/device-loss 요구를 재개한다면 native probe 또는 Player의 실제 구현과 수명 경계부터 다시 대조해야 한다.

## 1. 크기는 서로 다른 관측값이다

Windows client, ImGui content canvas, scaled render target, 실제 swapchain image, 완료된 표시 texture 크기는 같은 필드가 아니다. WM_DPICHANGED는 창 스레드에서 suggested RECT를 즉시 적용한다. Host는 GetClientRect의 실제 크기로 DX12 shell resize를 요청하고, ViewportHost는 자신의 content를 물리 픽셀로 게시한다. render scale은 off=1, auto=1/DPI, fixed 정책을 0.25~1 범위로 제한한다. 숨겨진 host는 0 extent로 정상 타깃을 붕괴시키지 않고 이전 유효 크기를 유지한다.

`ScreenResizeBus`는 별도 mutex로 `{width,height,generation}`을 함께 저장·복사한다. 같은 크기면 generation을 올리지 않는다. subscriber 목록은 복사한 뒤 callback을 잠금 밖에서 호출한다. `BuildLiveFramePacket`은 snapshot을 한 번 읽고, frame 크기 변화에 따른 렌더러 자체 resizeGeneration을 게시한다. 버스 generation과 렌더러 generation은 이름이 비슷해도 동일 카운터라고 가정하지 않는다.

## 2. DX12 생산과 표시 수명

기존 pipeline의 크기가 다르면 `ResizePipeline`이 live GPU 작업을 drain한 후 displayLifetimeMutex 아래 결과를 무효화한다. graph, display slot, transient pool/alias cache, SSGI history 같은 크기 자원을 회수하고 새 display slot을 만든다. PSO, ShaderMeta variant, IBL과 고정 fog volume은 유지한다. 같은 pipeline 전체 shader 준비를 resize마다 다시 수행하는 구조가 아니다.

실패하면 lastError에 resize lifecycle 오류를 기록하고 teardown 후 enabled=false로 전환한다. 성공해도 즉시 새 GPU 결과가 있는 것은 아니다. 완료된 slot이 게시되고 표시 sink가 해당 token을 열어야 UI에서 사용할 수 있다. 이전 세대를 계속 정상 이미지로 보이게 하려고 ready/epoch 검사를 제거해서는 안 된다.

표시 획득은 texture ID와 그 texture의 frame metadata를 함께 반환한다. sink inactive, renderer disabled, inactive view, result pending, key missing, scene changed, resize changed 등 unavailable 이유를 구별한다. completed scene epoch/resize generation이 현재와 다르면 새 프레임처럼 소비하지 않는다. 조회 횟수, unavailable 구간 및 복구 시간, 실제 소비 frame/generation은 pipeline ready보다 직접적인 표시 증거다.

## 3. shell swapchain은 별도 수명이다

ImGuiDx12Shell은 비활성·0 크기·동일 크기를 건너뛰고 ResizeSwapChain 성공 때만 cached width/height를 바꾼다. DX12DeviceResources는 lifecycle drain, backbuffer 참조 해제, ResizeBuffers, 새 buffer/RTV 획득을 순서대로 수행한다. live Scene의 offscreen target 교체와 OS swapchain 교체를 하나의 작업으로 뭉개지 않는다. shell Resize가 void이고 지역 오류를 상위로 직접 반환하지 않는 점도 진단 시 고려해야 한다.

## 4. 원인 판정과 보존할 요구

검은 Scene 화면만으로 font failure, GPU hang, stale descriptor, resize 반복 중 어느 것이 원인인지 확정할 수 없다. 최소한 요청/실제 client, canvas/target extent, render/complete/consume frame, scene/resize generation, token 열기 결과와 최초 오류를 같은 시간축에 남긴다. GPU 완료 전 새 자원을 표시하거나 사용 중인 texture를 해제하는 해결은 허용하지 않는다.

과거 Vulkan 가설의 유효한 요구는 실제 생성 extent 기반 render area, acquire/present 재생성 분류, 미개방 프레임 취소, device-loss 최초 오류 보존 및 추가 제출 중단이다. 당시 특정 VUID나 fence 대기 오류가 이번 HEAD의 직접 원인이라는 결론은 없다. 옛 실패 보고가 현재 경로에서 다시 재현되어야 수정 여부를 판단할 수 있다.

남은 수용은 반복 resize·최소화/복원·다중 DPI 이동·종료 경합, 각 generation의 첫 새 GPU 완료 이후 신속한 UI 소비, 오래된 자원 안전 퇴역과 오류 복구 정책이다. 이전의 “GPU 완료 후 두 정상 표시 프레임 안 소비”는 수용 목표이며 현재 코드가 보장하는 시간 상한으로 선언하지 않는다. [DX12 검사](EditorW1Dx12ResizeValidation.md)와 [실제 조작 절차](EditorW1InteractiveValidation.md)가 증거를 구분한다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorWindowChrome.cpp](../../Editor/EngineGUIWindow/EditorWindowChrome.cpp)
- [Editor/EngineGUIWindow/ViewportHostWindow.cpp](../../Editor/EngineGUIWindow/ViewportHostWindow.cpp)
- [Editor/EngineGUIWindow/ViewportHostWindow.h](../../Editor/EngineGUIWindow/ViewportHostWindow.h)
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp](../../Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp)
- [Editor/HostImGuiPresentation/RHI/DX12/ImGuiDx12Shell.cpp](../../Editor/HostImGuiPresentation/RHI/DX12/ImGuiDx12Shell.cpp)
- [Engine/RenderEngine/RHI/ScreenSizedResource.h](../../Engine/RenderEngine/RHI/ScreenSizedResource.h)
- [Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp](../../Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
