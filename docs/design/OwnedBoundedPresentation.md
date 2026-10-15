# 표시 입력의 bounded 소유권

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 소유자가 분명한 전달

GT는 live frame packet을 만들고 RT는 이를 소비한다. renderer는 처리 중 하나와 교체 가능한 pending 하나를 유지하며, 새 packet은 이전 pending의 필요한 lifecycle delta를 보존해 합친다. GPU credit을 기다리는 동안 오래된 packet을 무조건 렌더링하지 않고 credit 확보 뒤 최신 packet을 선택한다.

이 CPU publication bound를 GPU queue depth, 화면 지연 상한 또는 SDK 내부 frame 수와 같다고 해석하지 않는다. 각각 별도 credit·fence·owner가 있다. profiler의 frame age는 관측 값이며 스케줄러나 운영체제의 실시간 보장이 아니다.

## display lease

RHIDisplayFrameMetadata에는 frameId, sceneEpoch, resizeGeneration, viewId, historyRevision, camera 정보와 source capture timestamp가 담긴다. timestamp는 진단용이고 completion token이 아니다. textureId 역시 host가 연 shared resource의 opaque 식별자다.

IDisplayPresentationSink::OpenSharedTexture는 consumer lease를 함께 받는다. sink는 최종 GPU 사용이 끝날 때까지 lease를 유지한다. producer의 재사용 판단과 acquisition은 display lifetime mutex 아래 직렬화된다. refcount만 확인하고 fence 없는 이미지를 덮어쓸 수 없다. completionLost가 설정된 slot은 완료를 증명할 수 없으므로 재사용하지 않는다.

## temporal 입력

RHITemporalDisplayPacket의 HUDless, UI, depth, motion은 전용 non-alias 공유 이미지다. producer copy fence 이후 publish하고 공통 lease/lifetime token이 SDK의 마지막 소비까지 모두 붙잡는다. native Present가 반환되어도 해당 소비가 끝났다고 판단하지 않는다. 사용 불가능한 capability는 명시적인 real-frame fallback 진단으로 표현해야 한다.

## 멈춤과 실패

RT publication 중단·join 전에 sink callback을 제거하면 깨우기와 수명 경합이 생길 수 있다. Player는 PT를 stop/join하고 연결을 끊는다. device loss나 fence 실패로 완료를 증명하지 못하면 안전한 quarantine/실패 보고가 필요하다. 정상 close의 메모리 회수와 불확실한 GPU 수명 보존은 다른 정책이다.

실제 backend 연결은 [PlayerNativePresentation](PlayerNativePresentation.md), simulation/render 순서는 [동기화 구조](../analysis/EngineSynchronizationArchitecture.md)에 있다.

## 구현 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h)
- [Engine/RenderEngine/RHI/RHIDisplayFrame.h](../../Engine/RenderEngine/RHI/RHIDisplayFrame.h)
- [Engine/RenderEngine/RHI/IDisplayPresentationSink.h](../../Engine/RenderEngine/RHI/IDisplayPresentationSink.h)
- [Player/PlayerPresentation.h](../../Player/PlayerPresentation.h)
- [Player/PlayerDX12Presentation.cpp](../../Player/PlayerDX12Presentation.cpp)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
