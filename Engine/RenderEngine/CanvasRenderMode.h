#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

// Unity Canvas의 세 렌더 모드와 같은 화면/월드 배치 계약.
enum class CanvasRenderMode : std::uint8_t
{
    ScreenSpaceOverlay = 0,
    ScreenSpaceCamera = 1,
    WorldSpace = 2,
};
