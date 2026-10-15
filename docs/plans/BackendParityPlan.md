# DX12·Vulkan 교차 비교 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 공통 기반과 차이

양 backend는 RHI service/encoder와 EnhancedRenderGraph, LivePipelineDesc를 공유하고 각각 DXIL/SPIR-V shader output와 native resource·command·display 경로를 가진다. 공용 선언이나 Vulkan 선택 enum만으로 기능·품질·성능 동등성을 판정할 수 없다.

현재 METEOR SceneHost는 mesh-indirect·cooked topology와 hybrid visibility의 atomic/raster capability를 요구한다. 장치가 실제로 지원하는지 먼저 확인한다. DLSS host는 DX12 전용이고 FSR/XeSS는 각각 optional API build/runtime 조건이 있다. DX12 owned queue 결과를 Vulkan multi-queue 결과로 이월하지 않는다.

## 비교 순서

1. 같은 source·cooked asset·shader generation과 해상도·camera/light/material을 고정한다
2. 각 backend의 device feature·PSO/shader 생성·경고·unsupported 사유를 기록한다
3. 실제 Editor/Player frame을 RenderDoc 등으로 capture하고 graph producer→native resource→consumer binding을 확인한다
4. 최종 HDR/display와 geometry visibility·GBuffer·shadow 3장·temporal 입력 등 관련 attachment를 비교한다
5. format·coordinate·depth·sampling 의미 차이를 설명한 허용치로 픽셀을 대조한다. 차이를 단순히 backend 차이라고 무시하지 않는다
6. Debug correctness/validation과 Release normal-frame 성능을 분리하고 resize·view 교체·abort·shutdown 수명까지 검사한다

## 주요 이관 사례

CSM의 off-camera/far caster와 alpha·음수 scale, 메시렛 primitive/LOD identity와 ordered blended, hybrid HW/SW 동일성, decal/temporal material packet, environment cooked/source-free, SDF UI와 Canvas를 포함한다. TU/FG는 지원 행렬을 먼저 정하고 unsupported를 false success로 만들지 않는다.

각 분야의 기존 DX12 주 수용을 이 계획의 Vulkan 미실행만으로 소급 무효화하지 않는다. 반대로 그 DX12 성공이 Vulkan parity 통과는 아니다. 현재 문서 개정은 비교 실행을 하지 않았고 과거 로컬 capture를 재확인하지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp](../../Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp)
- [Engine/RenderEngine/RHI/Vulkan/VulkanRenderServices.cpp](../../Engine/RenderEngine/RHI/Vulkan/VulkanRenderServices.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/MaterialGraphHybridVisibility.h](../../Engine/RenderEngine/MaterialGraphHybridVisibility.h)
