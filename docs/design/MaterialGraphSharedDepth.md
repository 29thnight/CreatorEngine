# 공유 깊이와 현재 pose의 두 실행 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 독립 표면 수집 계약

`RasterSurfaceCollector::PrepareSharedDepth`는 complete opaque draw set의 D32 depth와 승자 정보 위에서 material별 표면 입력을 수집한다. depth source는 같은 view/recording의 un-sampled geometry에서 만들어야 한다. material별로 depth를 다시 clear하거나 독립 깊이를 합성하는 방식은 겹친 표면의 가시성을 보장하지 못한다.

source depth와 mesh producer를 먼저 Declare하고 shared consumer는 depth read만 등록한다. exact winner를 함께 확인하므로 동등한 depth의 다른 draw를 자기 표본으로 받아들이지 않는다. completed readback으로 source/coverage를 검증한 뒤 evaluated-point 결과를 수용한다.

## 현재 pose와 topology 재사용

`BuildForScene`은 정적 mesh partition은 공유하지만 bone palette/world/view revision을 새로 밀봉한다. posed object-space producer는 동일 pose의 instance 사이 공유할 수 있고 각 소비자는 자기 world transform을 적용한다. 이전 pose가 들어 있는 topology cache를 현재 frame 출력처럼 사용할 수 없다.

`SceneGeometryIdentity`는 geometry/LOD namespace와 object/incarnation/instance/material slot을 함께 가진다. source geometry key 하나만으로 camera와 shadow의 가시성을 대체하지 않는다.

## 실제 제품 Scene

현재 METEOR는 Geometry → Cull.First → Occluder → Cull.Final → Surface의 hybrid visibility를 사용한다. occluder depth는 별도 첫 목록의 HW/SW producer가 만들며 최종 surface visibility에서 GBuffer를 재구성한다. 과거의 `LXSceneDepthPS` 기반 설명을 현재 pass 목록으로 유지하지 않는다.

shadow와 Temporal도 자기 소비 계약과 current/previous geometry를 사용한다. 독립 raster helper의 shared-depth 회귀는 이 모든 제품 stage의 동작 증거가 아니다. 실제 배선은 [Scene host](MaterialGraphSceneHost.md)에 모았다.

## 실패와 수명

다른 frame/view/epoch, 선언되지 않은 source, 잘못된 coverage, 제한을 넘는 geometry는 정상 깊이처럼 수용하지 않는다. GPU owner는 마지막 제출 완료까지 유지한다. 이번 갱신에서 새 캡처나 readback을 수행하지 않았으며 과거 통과 숫자는 현재 pose/hybrid 경로의 최신 증거로 취급하지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphRasterSurface.h`](../../Engine/RenderEngine/MaterialGraphRasterSurface.h)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.h`](../../Engine/RenderEngine/MaterialGraphMeshSurface.h)
- [`Engine/RenderEngine/MaterialGraphGeometryContract.h`](../../Engine/RenderEngine/MaterialGraphGeometryContract.h)
- [`Engine/RenderEngine/MaterialGraphSceneOccluderStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneOccluderStage.inl)
- [`Engine/RenderEngine/MaterialGraphSceneSurfaceStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneSurfaceStage.inl)
- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
