#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <string>

// Vulkan 골격 자가 검증.
//
// ★ 이 헤더는 vulkan.h 를 끌어오지 않는다. CLI(EngineEntry)가 이것만 보면
//   되도록 하기 위해서다 — 골격 하나 때문에 다른 프로젝트가 SDK 헤더 경로를
//   요구하게 만들지 않는다.
//
// 판정 줄은 dx12.* 검사 20종과 같은 모양으로 낸다([n/m] 접두). 두 백엔드의
// 결과를 같은 방식으로 대조할 수 있어야 하기 때문이다.
//
// 반환값이 false 라도 outLog 는 채워진다 — 어디까지 갔는지가 산출물이다.
bool RunVulkanSelfTest(const std::string& outputPngPath, std::string& outLog);

/// 그리드 패스를 Vulkan 으로 (5d). `EnhancedGridPass` 를 한 줄도 고치지 않고
/// 돌려 dx12.grid 기준선(점등 9840 · 원점 R 0.225)과 픽셀 대조한다.
bool RunVulkanGridTest(std::string& outLog);

/// 첫 텍스처 소비 공용 패스. EnhancedSkyBoxPass를 그대로 돌리고 합성 큐브맵의
/// 면 색을 DX12 검사와 같은 좌표·기준으로 대조한다.
bool RunVulkanSkyBoxTest(std::string& outLog);

/// 실제 CameraGizmo.png를 공용 EnhancedGizmoIconPass에 넣어 t0 root storage
/// buffer와 t1 2D SRV의 픽셀·검증 레이어 결과를 DX12 기준과 대조한다.
bool RunVulkanGizmoIconTest(std::string& outLog);

/// 동일한 합성 mesh/camera/light로 EnhancedShadowPass를 두 백엔드에서 실행해
/// depth-only PSO, cascade array slice view와 첫 slice depth 픽셀을 대조한다.
bool RunVulkanShadowTest(std::string& outLog);
bool RunVulkanGBufferTest(std::string& outLog);
bool RunVulkanForwardTest(std::string& outLog);
bool RunVulkanDeferredTest(std::string& outLog);
bool RunVulkanDecalTest(std::string& outLog);
bool RunVulkanSSAOTest(std::string& outLog);
bool RunVulkanSSGITest(std::string& outLog);

#endif
