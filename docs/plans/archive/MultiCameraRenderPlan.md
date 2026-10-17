# 다중 카메라·표시 뷰의 현행 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 카메라와 표시 신원

CameraComponent가 Camera 값을 소유하고 Scene의 CameraSystem이 등록·해제를 관리한다. GetPrimaryCamera는 유효·활성·현재 scene 소유 camera 중 primary를 우선하고 같은 범주에서는 가장 작은 instance ID를 고른다. primary가 없으면 가장 작은 유효 instance가 fallback이다. 전역 registry나 마지막 camera pointer를 RenderCore의 신원으로 쓰지 않는다.

EditorCameraRig는 Editor 뷰 camera와 조작 상태를 소유한다. Host는 EnhancedLiveViewRequest에 viewId/historyRevision, FrameCameraSnapshot, displayTarget, 기능 flags를 값으로 봉인한다. viewId는 논리 출력이고 displayTarget은 표시 목적이며 backend slot index와 다르다.

## 프레임과 history

Editor/Player의 BuildLiveFramePacket→PublishLiveFrame이 실제 진입점이다. renderer는 Camera*를 역참조해 view 상태를 다시 만들지 않는다. view별 해상도·projection·scene epoch·history revision·temporal configuration을 추적하고 바뀐 계약에서 history를 reset한다. 제출된 이전 camera만 motion/history 기준으로 Commit한다.

SceneOverlay·ScreenSpaceUI·CanvasPreview는 host view 정책이다. 같은 scene을 두 view로 볼 때 geometry/material owner를 공유할 수 있지만 camera visibility·HZB·temporal·display completion은 각 view의 신원을 유지한다. preview의 재사용 이미지도 완료 snapshot과 일치해야 한다.

## 확장·검증 주의

카메라 cut·primary 전환·scene 교체·resize·Editor/Game 동시 표시·inactive camera·view 제거를 구분해 검사한다. pointer 재사용이 같은 history로 인정되지 않아야 한다. FG는 Player presentation 대상 계약이며 EditorViewportForbidden 제한을 준수한다.

과거 고정 CameraContainer·RenderPassData 슬롯 제안은 현행 설계가 아니다. 당시 빌드/77개 golden·30프레임 기록을 이번 커밋의 결과로 올리지 않는다. [현재 라이브 흐름](../../analysis/RenderPipelineFlow.md)과 [temporal 계약](../TemporalReconstructionPlan.md)을 따른다.

## 소스 근거

- [Engine/SceneRuntime/CameraSystem.cpp](../../../Engine/SceneRuntime/CameraSystem.cpp)
- [Editor/EngineGUIWindow/EditorCameraRig.cpp](../../../Editor/EngineGUIWindow/EditorCameraRig.cpp)
- [Engine/RenderEngine/FrameCameraSnapshot.h](../../../Engine/RenderEngine/FrameCameraSnapshot.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h](../../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Editor/EngineEntry/App.cpp](../../../Editor/EngineEntry/App.cpp)
- [Player/PlayerApp.cpp](../../../Player/PlayerApp.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/plans/archive/MultiCameraRenderPlan.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
