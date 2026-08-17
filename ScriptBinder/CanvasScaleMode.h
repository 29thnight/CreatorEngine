#pragma once
#include "Reflection.hpp"

// 캔버스가 화면 크기에 어떻게 반응할지 정한다(uGUI CanvasScaler와 같은 개념).
enum class CanvasScaleMode
{
    // 저작한 픽셀 크기를 그대로 쓴다. 화면이 커지면 UI가 상대적으로 작아진다.
    ConstantPixelSize,

    // 기준 해상도 대비 화면 비율로 전체 UI를 확대·축소한다.
    ScaleWithScreenSize,
};
