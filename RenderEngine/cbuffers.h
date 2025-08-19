#pragma once
#include "IRenderPass.h"
constexpr uint32 GAUSSIAN_BLUR_RADIUS = 7;

cbuffer ThresholdParams
{
	float threshold{ 0.3f };
	float knee{ 0.5f };
};

cbuffer BloomCompositeParams
{
	float coefficient{ 0.3f };
    float2 texelSize{};
    float _padding{};
};

cbuffer BlurParams
{
    float coefficients[GAUSSIAN_BLUR_RADIUS + 1];
    float sigma{ 3.f };
    int radius{ GAUSSIAN_BLUR_RADIUS };
    int direction{};
    float _padding{};
};
