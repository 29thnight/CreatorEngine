#define LX_MATERIAL_FEATURE_MASK 0x2000u
#define LX_MATERIAL_ROUTE 2
#include "Includes/MaterialGraphSceneVolumeLighting.slang"

Texture2D<float4> lx_volumeBackground : register(t0);
Texture2D<float> lx_volumeDepth : register(t1);
RWTexture2D<float4> lx_volumeColor : register(u0);

[shader("compute")]
[numthreads(32, 1, 1)]
void LXSceneVolumeCompositeCS(uint thread : SV_DispatchThreadID)
{
    if (thread >= lx_volumeDispatchCount)
        return;
    const uint index = lx_volumeFirst + thread;
    const uint2 pixel = uint2(index % lx_volumeWidth, index / lx_volumeWidth);
    const float2 ndc = float2((float(pixel.x) + .5) / lx_volumeWidth * 2 - 1,
                              1 - (float(pixel.y) + .5) / lx_volumeHeight * 2);
    const float4 nearHomogeneous = mul(float4(ndc, 0, 1), lx_volumeInverseViewProjection);
    const float3 origin = nearHomogeneous.xyz / nearHomogeneous.w;
    const float depth = lx_volumeDepth.Load(int3(pixel, 0));
    const float4 endHomogeneous = mul(float4(ndc, depth, 1), lx_volumeInverseViewProjection);
    // The far homogeneous point can be at infinity or behind the camera.
    // A visible opaque endpoint defines the forward ray without that reversal.
    const bool finiteEndpoint = endHomogeneous.w > 1e-6;
    const float4 rayHomogeneous = finiteEndpoint ? endHomogeneous
        : mul(float4(ndc, .5, 1), lx_volumeInverseViewProjection);
    const float3 offset = rayHomogeneous.xyz / rayHomogeneous.w - origin;
    const float distance = length(offset);
    const float3 direction = offset / max(distance, 1e-6);
    const float limit = finiteEndpoint ? distance : 1e9;
    const VolumeSegment medium = LXSceneVolumeTransport(origin, direction, limit);
    const float4 background = lx_volumeBackground.Load(int3(pixel, 0));
    lx_volumeColor[pixel] = float4(medium.radiance + medium.transmittance * background.rgb, background.a);
}
