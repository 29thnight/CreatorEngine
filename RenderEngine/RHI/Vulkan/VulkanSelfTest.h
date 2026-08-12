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

#endif
