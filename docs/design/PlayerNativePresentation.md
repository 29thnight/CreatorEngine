# Player의 native presentation 경로

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 진입점과 책임

PlayerMain이 RuntimeSettings의 backend를 읽어 CreateDX12Presentation 또는 CreateVulkanPresentation을 선택한다. Presentation은 window 초기화, Resize, BeginFrame, Present, Shutdown을 제공하고 acquire·composition·pacing·native Present를 소유한다. renderer는 display image를 제공하며 Player swapchain을 직접 운영하지 않는다. Editor launch의 DX12 정책과 Player의 backend 선택을 혼동하지 않는다.

GT는 input sampling 전에 BeginSimulationFrame(realFrameId)을 호출한다. RT display-available callback은 기존 PT를 깨우며 그 callback 자체가 GPU command를 기록하지 않는다. PT mailbox의 최신 요청을 소비하고 종료 시 명시적으로 join한다.

## DX12

DX12 presentation은 producer의 shared texture/fence를 자기 device에서 연다. engine RHI handle을 다른 device에서 그대로 사용할 수 없다. pending/submitted 사용 기록은 COM resource와 consumer lease를 유지하며 producer 완료와 presentation GPU 완료를 각각 확인한다. swapchain resize나 종료에도 이 수명이 적용된다.

Present 반환은 consumer-complete 신호가 아니다. 종료 복구의 bounded fence 대기에서 완료를 증명하지 못하면 quarantine과 shutdown failure를 보고하는 경로가 있다. 숨겨진 smoke window의 성공을 실제 사용자의 scanout으로 표현하지 않는다.

## Vulkan

현재 구현의 이름은 `Vulkan (CPU readback/upload bridge)`다. scene readback → CPU 최신 snapshot mailbox → Vulkan upload → composition/present 경로를 사용한다. CPU frame은 크기·할당 실패 검사를 거치며 단일 업로드 이미지와 descriptor는 이전 GPU 사용을 확인한 뒤 갱신한다.

이 경로는 native multi-resource temporal SDK transport를 제공하지 않는다. 해당 요청은 NativePresentationInteropUnavailable 진단과 real-frame fallback으로 구분된다. Vulkan renderer의 존재만으로 DX12와 같은 zero-copy 또는 temporal capability를 약속하지 않는다.

## 관측 계약

GetSubmittedGameFrames는 game composition을 제출하고 native Present가 성공한 횟수다. DXGI_STATUS_OCCLUDED도 그 draw를 세지만 compositor 수락이나 scanout은 증명하지 않는다. acquisition이 연기되거나 producer만 일한 frame은 증가하지 않는다. backend 이름, fallback 이유, shutdown failure와 이 count를 함께 해석해야 한다.

구현 확인과 장치별 검증은 다르다. resize·minimize·occlusion·device loss·SDK 비지원 조합의 실행 결과는 별도로 기록해야 한다.

## 구현 근거

- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Player/PlayerPresentation.h](../../Player/PlayerPresentation.h)
- [Player/PlayerDX12Presentation.cpp](../../Player/PlayerDX12Presentation.cpp)
- [Player/PlayerVulkanPresentation.cpp](../../Player/PlayerVulkanPresentation.cpp)
- [Engine/RenderEngine/RHI/IDisplayPresentationSink.h](../../Engine/RenderEngine/RHI/IDisplayPresentationSink.h)
