# Material 준비, RenderGraph 선언, 병렬 기록

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 단계별 책임

Material host의 기본 흐름은 Prepare → Declare → graph compile/record → submit → completion/게시다. Prepare는 자원 할당, descriptor/upload 준비, exact generation과 device 검증을 수행한다. Declare는 producer/consumer RGHandle과 읽기/쓰기 상태를 등록한다. callback은 준비된 불변 packet으로 bind/draw/dispatch한다.

`RenderBindingCache`의 packet은 recording ID, descriptor version, instance pin, reflected layout을 포함한다. 다른 recording에서 동일 GPU handle처럼 보인다고 재사용하지 않는다. 같은 recording 안의 다른 host layout에는 `ValidatePass`/`BindPass`로 호환성을 확인한다.

## 제품 Scene과 독립 평가 helper

현재 live Scene은 `SceneHost` stage들과 `EnhancedSceneRenderer::BuildPipelineDesc`로 연결된다. 각 stage가 게시한 handle을 다음 stage가 그대로 받아야 한다. `AdvanceSceneColor`/`AdvanceSceneSurface`는 ExplicitVersioned 모드에서 새 write/modify version을 만든다.

`SurfaceBatch`, `RasterSurfaceBatch`, `IblBakeResult`는 독립 평가/회귀에도 사용되는 준비·선언 API다. producer를 같은 graph에 먼저 선언하고 exact graph epoch를 유지한다. 이미 record한 결과를 다시 Declare하거나 선언되지 않은 GPU input을 read하면 안 된다. 실제 Scene이 이 helper들을 모두 직렬로 호출한다고 가정하지 않는다.

## 상태 전이와 오류

`NormalizeSceneReads`는 같은 resource version의 호환 가능한 shader read를 합친다. 중복 writer 또는 호환되지 않는 read state는 예외다. bind 자체가 resource transition을 대신하지 않는다.

texture copy는 native parallel prefix보다 앞에 놓고 fresh upload recording에서 uniforms/geometry를 준비한다. callback 내부의 resource allocation이 남아 있는 지점도 있으므로 모든 엔진 allocation이 완전히 Prepare로 이동했다고 주장하지 않는다. 각 stage의 callback과 backend 허용 범위를 확인해야 한다.

## 완료와 수명

record-ready bit는 dispatch 기록 완료이지 GPU 완료가 아니다. 독립 evaluated-point 경로의 Scene 게시에는 completed readback 유효성 검증이 추가된다. live lookup은 submission ticket과 native 성공을 확인하며 전 화면 readback 경로와 구별된다.

abort된 recording은 게시하지 않는다. native가 참조할 수 있는 submitted owner는 completion까지 유지한다. graph callback이 해제되어도 GPU 자원이 곧바로 해제되는 것은 아니다. shutdown은 scheduler/recording/submission을 정리하고 device idle 이후 수행한다.

검증은 잘못된 epoch, 누락 producer, 중복 Declare, allocation 실패, submission 거부, 완료 전 교체를 각각 포함해야 한다. 이번 문서 갱신에서는 실행하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphRenderBindings.h`](../../Engine/RenderEngine/MaterialGraphRenderBindings.h)
- [`Engine/RenderEngine/MaterialGraphSceneAccess.h`](../../Engine/RenderEngine/MaterialGraphSceneAccess.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.h`](../../Engine/RenderEngine/MaterialGraphSceneHost.h)
- [`Engine/RenderEngine/MaterialGraphSurfaceBatch.h`](../../Engine/RenderEngine/MaterialGraphSurfaceBatch.h)
- [`Engine/RenderEngine/MaterialGraphRasterSurface.h`](../../Engine/RenderEngine/MaterialGraphRasterSurface.h)
- [`Engine/RenderEngine/MaterialGraphIblBake.h`](../../Engine/RenderEngine/MaterialGraphIblBake.h)
