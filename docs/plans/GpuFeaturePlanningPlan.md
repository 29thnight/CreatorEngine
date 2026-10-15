# GPU 후속 기능의 설계 의사결정

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 설계 축과 현재 위치

GPU-1 visibility는 이미 substantial native 구현을 갖는다. 현행 METEOR의 geometry·hybrid occluder·meshlet list·spatial LOD·shadow를 출발점으로 삼고 초기 indexed-only 가정을 버린다. 미확인 품질/성능을 구현 부재와 혼동하지 않는다.

GPU-2 확률 tile lighting은 현재 EnhancedLightTilePass의 light 분류와 다른 기능이다. 후보 sampling·reservoir·temporal/spatial reuse·denoise·bias/light leaking·fallback 계약이 필요하다. 현재 light-tile 배선이 이 확률 알고리즘을 구현한다는 증거는 없다.

GPU-3 RT는 공통 AS·shader table·hit/material 및 지원 행렬 설계다. 현재 RHI/SceneHost/BuildPipelineDesc에는 제품 BLAS/TLAS·ray dispatch·PT/hybrid mode의 실행 배선이 확인되지 않는다. standalone PT와 raster+RT hybrid를 모두 만들려는 목표는 PathTracingHybridPipelinePlan으로 유지한다.

GPU-9는 이 결과의 공유 resource·generation·queue·history·실행 순서·산정을 하나로 대조하는 게이트다. 기존 4행 9인일은 설계 예산이며 세 기능의 구현·제품 수용 예산이 아니다.

## 결정해야 할 계약

geometry identity와 posed stream을 raster/AS가 어떻게 공유하는지, deform BLAS 갱신과 선택 LOD의 primitive remap을 어떻게 맞추는지부터 명시한다. 각 효과는 requested/active/supported와 실패 사유, 정확한 대체 경로를 가져야 한다. 새로운 RT 효과의 raster fallback 목표가 현행 SceneHost의 미지원 장치에 CPU fallback을 추가했다는 뜻은 아니다.

표면 BSDF·texture generation·alpha/coverage·normal·emission·volume의 실제 소비를 공통으로 정의한다. GPU culling 성능 향상이 material shading·IBL 재사용 비용도 해결했다고 가정하지 않는다. MAT 성능 게이트는 geometry와 material 비용을 분리한 실제 제품 프레임으로 회수한다.

## 선행과 산출물

C# 저작·라이트맵·품질 개선 전체를 GPU-9의 선행으로 묶지 않는다. 각 기능은 필요한 versioned graph·RHI queue·temporal 입력 계약만 소비한다. RT 탐색 효과별 실험은 DxrShaderTraversalExperimentPlan, 실제 두 모드 구현은 PathTracingHybridPipelinePlan이 소유한다. 같은 AS 기반이나 실험 비용을 중복 계상하지 않는다.

산출물은 지원 행렬, 최소 end-to-end slice, 정확성 oracle, cold/warm AS·CPU/GPU/VRAM budget, fallback·실패 정책, 위험과 재산정이다. DX12 Debug/Release 제품 게이트와 별도 Vulkan parity를 유지한다. 출처·버전·라이선스·상업 사용 허가가 확인되지 않은 외부 코드를 구현 전제로 삼지 않는다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedLightTilePass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedLightTilePass.cpp)
- [Engine/RenderEngine/RHI/IRenderDeviceServices.h](../../Engine/RenderEngine/RHI/IRenderDeviceServices.h)
- [Engine/RenderEngine/RHI/RHIEncoder.h](../../Engine/RenderEngine/RHI/RHIEncoder.h)
