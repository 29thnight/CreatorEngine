#include "Includes/Radiance.slang"
Texture2D    gEquirect : register(t0);
SamplerState gSampler  : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const float2 invAtan = float2(0.15915494309189535, 0.3183098861837907);
float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), -asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // Integrate a cube texel's angular footprint. A single center sample aliases
    // a sub-texel sun even when float32 storage preserves its peak. This work is
    // performed only on a cook miss, before diffuse/specular integration.
    const float3 dx = ddx_fine(input.texCoord), dy = ddy_fine(input.texCoord);
    float3 sum = float3(0);
    float weight = 0.0;
    for (uint y=0; y<4; ++y)
        for (uint x=0; x<4; ++x)
        {
            const float3 direction = input.texCoord + dx*((float(x)+.5)/4.0-.5)
                + dy*((float(y)+.5)/4.0-.5);
            const float solidAngle = pow(dot(direction,direction), -1.5);
            const float2 uv = SampleSphericalMap(normalize(direction));
            sum += SanitizeEnvironmentRadiance(gEquirect.SampleLevel(gSampler, uv, 0).rgb)*solidAngle;
            weight += solidAngle;
        }
    return float4(sum/weight, 1.0);
}
