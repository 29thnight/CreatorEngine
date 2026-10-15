# RenderScene 보유층과 프레임 뷰의 현행 분리

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 보유 데이터

RenderScene은 primitive/light/UI proxy의 별도 map을 소유한다. SceneRuntime의 ProxyCommand가 component 변경을 값으로 옮기고 render 소비 경계에서 적용한다. BeginProxyFrame은 frame packet의 scene epoch로 저장소를 전환한다. 등록·해제·scene 교체의 stale 명령이 다른 scene에 적용되지 않게 신원을 보존한다.

GetPrimitiveProxySnapshot/GetLightProxySnapshot/GetUIProxySnapshot은 lock 안에서 shared owner를 복사해 frame 사용 수명을 고정한다. 진단용 GetUIDrawSnapshot은 live mutable proxy 대신 같은 보호 아래 복사한 값만 반환한다. 모든 getter가 외부 스레드에서 임의 proxy mutation을 허용하는 것은 아니다.

## 뷰별 데이터

Host가 camera snapshot·logical view key·display flags를 봉인하고 EnhancedSceneRenderer가 이를 소비한다. view별 visibility·cascade·temporal/HZB·resolution·display completion은 프레임/뷰 영역이다. 카메라 자체는 Scene의 CameraSystem 또는 EditorCameraRig가 소유하며 RenderScene map에 전역 카메라 registry를 추가하지 않는다.

현재 geometry visibility의 실제 producer/consumer는 METEOR SceneHost stage다. 옛 계획의 하나의 CPU CullingResults가 모든 renderer 경로를 대표한다고 가정하지 않는다. camera opaque·ordered blended·shadow cascade 목록은 서로 다른 의미와 수명을 가진다.

## 계속 필요한 불변식

scene epoch와 proxy instance 신원을 보존하고 제거된 객체의 frame snapshot은 소비 완료까지 유지한다. scene 변경·play 전환·late command·두 view·light/UI 업데이트를 함께 검사한다. 보유층 lock은 복사/게시 경계에 제한하고 장시간 GPU 준비를 그 lock 안에 넣지 않는다.

과거 Light 재수집·CameraContainer·RenderPassData 이행 단계와 엔진 비교 표는 현재 API 보증이 아니다. 실제 흐름은 [RenderPipelineFlow](../../analysis/RenderPipelineFlow.md), camera 정책은 [MultiCameraRenderPlan](MultiCameraRenderPlan.md)이 정리한다.

## 소스 근거

- [Engine/RenderEngine/RenderScene.h](../../../Engine/RenderEngine/RenderScene.h)
- [Engine/RenderEngine/RenderScene.cpp](../../../Engine/RenderEngine/RenderScene.cpp)
- [Engine/SceneRuntime/ProxyCommand.cpp](../../../Engine/SceneRuntime/ProxyCommand.cpp)
- [Engine/SceneRuntime/RenderSceneBridge.cpp](../../../Engine/SceneRuntime/RenderSceneBridge.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/plans/archive/RenderSceneViewPlan.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
