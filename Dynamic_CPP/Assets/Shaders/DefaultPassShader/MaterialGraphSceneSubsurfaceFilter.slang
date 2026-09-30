#define LX_MATERIAL_FEATURE_MASK 0x107Fu
#include "Includes/PrincipledSubsurface.slang"

cbuffer LXSceneSubsurfaceConstants : register(b0)
{
    uint lx_width, lx_height, lx_first, lx_dispatchCount;
    uint lx_environment, lx_radius, lx_reserved0, lx_reserved1;
};
Texture2D<float4> lx_inputs_0 : register(t0);
Texture2D<float4> lx_inputs_1 : register(t1);
Texture2D<float4> lx_inputs_2 : register(t2);
Texture2D<float4> lx_inputs_3 : register(t3);
Texture2D<float4> lx_inputs_4 : register(t4);
Texture2D<float4> lx_inputs_5 : register(t5);
Texture2D<float4> lx_inputs_6 : register(t6);

float4 LXLoadSceneInput(uint field, int3 pixel)
{
    switch (field)
    {
        case 0u:
            return lx_inputs_0.Load(pixel);
        case 1u:
            return lx_inputs_1.Load(pixel);
        case 2u:
            return lx_inputs_2.Load(pixel);
        case 3u:
            return lx_inputs_3.Load(pixel);
        case 4u:
            return lx_inputs_4.Load(pixel);
        case 5u:
            return lx_inputs_5.Load(pixel);
        case 6u:
            return lx_inputs_6.Load(pixel);
        default:
            return float4(0);
    }
}
Texture2D<uint> lx_owners : register(t7);
RWStructuredBuffer<float4> lx_irradiance : register(u1);

// Finite, normalized visible-surface dipole gather. The draw owner is the
// transport mask: hidden pixels, legacy materials and other draws never mix.
// World-space distances and projected surface areas drive the RGB profile.
[shader("compute")]
[numthreads(32, 1, 1)]
void LXSceneSubsurfaceFilter(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_dispatchCount)
    {
        return;
    }
    const uint index = lx_first + id.x;
    const int2 pixel = int2(index % lx_width, index / lx_width);
    const float4 position = lx_inputs_1.Load(int3(pixel, 0));
    if (position.w != 1.0)
    {
        lx_irradiance[index] = float4(0);
        return;
    }
    const float4 normal = lx_inputs_2.Load(int3(pixel, 0));
    const uint localMask = uint(normal.w);
    const uint owner = lx_owners.Load(int3(pixel, 0));
    const float3 source = lx_inputs_0.Load(int3(pixel, 0)).rgb;
    PrincipledSubsurfaceProfile profile;
    profile.realDepth = lx_inputs_3.Load(int3(pixel, 0)).rgb;
    profile.virtualDepth = lx_inputs_4.Load(int3(pixel, 0)).rgb;
    profile.attenuation = lx_inputs_5.Load(int3(pixel, 0)).rgb;
    profile.normalization = lx_inputs_6.Load(int3(pixel, 0)).rgb;
    profile.local = bool3((localMask & 1u) != 0u, (localMask & 2u) != 0u, (localMask & 4u) != 0u);
    float3 sum = float3(0);
    float3 denominator = float3(0);
    const int radius = int(lx_radius);
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const int2 neighbor = pixel + int2(x, y);
            if (any(neighbor < 0) || neighbor.x >= int(lx_width) || neighbor.y >= int(lx_height)
                || lx_owners.Load(int3(neighbor, 0)) != owner)
            {
                continue;
            }
            const float4 otherPosition = lx_inputs_1.Load(int3(neighbor, 0));
            const float3 otherNormal = lx_inputs_2.Load(int3(neighbor, 0)).xyz;
            if (otherPosition.w != 1.0 || dot(normal.xyz, otherNormal) < 0.5)
            {
                continue;
            }
            const float4 incident = lx_inputs_0.Load(int3(neighbor, 0));
            const float3 weight = EvaluateSubsurfaceProfile(profile, length(position.xyz - otherPosition.xyz))
                * max(incident.w, 1e-12);
            sum += weight * incident.rgb;
            denominator += weight;
        }
    }
    const float3 result = select(profile.local || denominator <= 0.0, source, sum / max(denominator, 1e-20));
    lx_irradiance[index] = float4(result, 1);
}
