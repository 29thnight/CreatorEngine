# 독립 raster 표면 수집 helper

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 적용 범위

`RasterSurfaceCollector`는 opaque visible pixel을 `SurfaceGeometrySource`로 제공하는 bounded helper다. 현재 제품 Scene의 주 visibility는 METEOR hybrid HW/SW raster와 material reconstruction이며 이 helper를 그 전체 구현으로 설명하지 않는다.

`RasterSurfaceRequest`는 width/height, viewProjection, texture footprint를 가진다. `Prepare`는 한 material의 mesh chunk에 private depth를 만들고 `PrepareSharedDepth`는 complete opaque set에서 만든 depth/승자 입력을 읽는다. unsampled world-frame mesh buffer와 exact view가 필요하다.

## 준비와 선언

`Initialize`는 vertex/pixel/resolve/shared-pixel artifact를 받고 candidate-first로 준비한다. `Prepare`에서 targets 6장, depth, output buffer, uploads/descriptors를 마련한다. 최대 pixel은 `IblBaker::MaxPoints`인 4096, draw는 64다. 이것을 live viewport의 4096×4096 host 예산과 혼동하지 않는다.

batch는 현재 device/recording/descriptor generation에서 한 번만 Declare한다. mesh producer를 먼저 선언하고 shared-depth 사용 시 depth source가 먼저 있어야 한다. callback은 output과 depth의 의존성을 graph에 드러낸다.

## 깊이·미분·배경

pixel count는 width×height지만 모든 pixel이 유효 표면은 아니다. coverage는 별도 읽기 검증 후 `IsCovered`로 확인한다. background를 원점의 정상 material 표본으로 처리하면 안 된다. `Derivatives()`는 진단 footprint 출력이며 graph 실행 이후 ShaderResource 상태다.

shared-depth 경로는 같은 depth와 exact draw winner를 읽으며 자기 material만 다시 depth clear/write하지 않는다. 가까운 다른 material에 가려진 표본을 따로 적분하는 오류를 막는 경계다.

## 완료와 제품 연결

`IsReadyForEvaluation`은 기록 stage와 depth source 준비를 검사한다. GPU 완료 및 `ValidateReadback` 수용은 별도다. 반환 owner를 submission 완료까지 유지하고 device 종료 전에 놓는다. [Shared depth](MaterialGraphSharedDepth.md), [Surface batch](MaterialGraphSurfaceBatch.md)를 함께 사용하되 독립 probe 결과를 제품 Scene 전체 결과로 확대하지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphRasterSurface.h`](../../Engine/RenderEngine/MaterialGraphRasterSurface.h)
- [`Engine/RenderEngine/MaterialGraphRasterSurface.cpp`](../../Engine/RenderEngine/MaterialGraphRasterSurface.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphRasterSurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphRasterSurface.slang)
