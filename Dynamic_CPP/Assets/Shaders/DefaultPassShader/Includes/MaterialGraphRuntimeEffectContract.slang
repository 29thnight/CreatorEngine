#pragma once

#include "MaterialGraphSceneSubsurface.slang"
#include "MaterialGraphSceneRefraction.slang"

// float32 transport와 원래 reflection/refraction 식을 유지한다. 이 레코드는
// 전체 화면 대신 256² 내부 + 반경 4 halo의 제한된 scratch에만 존재한다.
struct LXSceneRuntimeEffectSample
{
    float4 sourceArea;
    float4 positionValid;
    float3 normal;
    uint owner;
    LXSceneSubsurfaceReflection reflection;
    LXSceneRefractionSample refraction;
};

cbuffer LXSceneRuntimeEffectConstants : register(b7)
{
    float4x4 lx_effectInverseViewProjection;
    uint lx_effectSourceX, lx_effectSourceY, lx_effectSourceWidth, lx_effectSourceHeight;
    uint lx_effectWidth, lx_effectHeight, lx_effectSteps, lx_effectRays;
    uint lx_effectBackgroundHasMedium, lx_effectTransmission, lx_effectReserved0, lx_effectReserved1;
};
