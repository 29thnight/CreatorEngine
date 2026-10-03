#pragma once

// Host registers are outside Graph's authoring range t16..127.
#define LX_VOLUME_TRIANGLES_REGISTER t131
#define LX_VOLUME_COEFFICIENTS_REGISTER t132
#define LX_VOLUME_CONSTANTS_REGISTER b5
#define LX_VOLUME_SHADOW_REGISTER t133
#define LX_VOLUME_ENVIRONMENT_REGISTER t134
#define LX_VOLUME_SHARED_SAMPLERS 1
#include "MaterialGraphSceneVolumeLighting.slang"

float3 LXForwardVolume(float2 pixel, float3 position, float3 radiance)
{
    if (lx_volumeTriangleCount == 0) return radiance;
    const float2 ndc = float2(pixel.x / lx_volumeWidth * 2 - 1, 1 - pixel.y / lx_volumeHeight * 2);
    const float4 nearPoint = mul(float4(ndc, 0, 1), lx_volumeInverseViewProjection);
    const float3 origin = nearPoint.xyz / nearPoint.w;
    const float3 offset = position - origin;
    const float distance = length(offset);
    const VolumeSegment medium = LXSceneVolumeTransport(origin, offset / max(distance, 1e-6), distance);
    return medium.radiance + medium.transmittance * radiance;
}
