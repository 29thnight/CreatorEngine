#pragma once
#include "Reflection.hpp"

// ��Ŀ �������� ���� �����ϱ� ���� ������
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