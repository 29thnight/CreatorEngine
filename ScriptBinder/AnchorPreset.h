#pragma once
#include "Reflection.hpp"

// 앵커 프리셋을 쉽게 설정하기 위한 열거형
enum class AnchorPreset
{
    TopLeft, TopCenter, TopRight,
    MiddleLeft, MiddleCenter, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    StretchLeft, StretchCenter, StretchRight,
    StretchTop, StretchMiddle, StretchBottom,
    StretchAll
};
AUTO_REGISTER_ENUM(AnchorPreset)