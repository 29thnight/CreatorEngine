#ifndef SHADING_COMMON
#define SHADING_COMMON

#include "Sampler.hlsli"
#include "Lighting.hlsli"
#include "Phong.hlsli"

struct SurfaceInfo
{
    float4 posW;
    float3 N;
    float3 T;
    float3 B;
    float3 V;
    float NdotV;
};

struct LightingInfo
{
    // general terms
    float3 L;

    float3 H;
    float NdotH;
    float NdotL;

    //currently for directional light source
    float shadowFactor;

    // for point sources
    float distance;
    float attenuation;
};

inline float3 CalcNormalFromNormMap(Texture2D normalMap, float2 uv, SurfaceInfo surf)
{
    float3 normalMapVal = normalMap.Sample(LinearSampler, uv).xyz;
    normalMapVal = normalize((normalMapVal * 2) - 1.0);
    float3 N = (normalMapVal.x * surf.T) +
               (normalMapVal.y * surf.B) +
               (normalMapVal.z * surf.N);
    return normalize(N);
}

inline float3 CalcNormalFromBumpMap(Texture2D bumpMap, float2 uv, SurfaceInfo surf)
{
    float3 N = surf.N;
    float2 bumpTextureSize;
    bumpMap.GetDimensions(bumpTextureSize[0], bumpTextureSize[1]);
    float2 pixelSize = 1.0 / bumpTextureSize;

    float mid = bumpMap.Sample(PointSampler, uv) * 2.0 - 1.0;
    float left = bumpMap.Sample(PointSampler, uv + float2(-pixelSize.x, 0)) * 2.0 - 1.0;
    float right = bumpMap.Sample(PointSampler, uv + float2(pixelSize.x, 0)) * 2.0 - 1.0;
    float top = bumpMap.Sample(PointSampler, uv + float2(0, -pixelSize.y)) * 2.0 - 1.0;
    float bottom = bumpMap.Sample(PointSampler, uv + float2(0, pixelSize.y)) * 2.0 - 1.0;
    float3 p1 = ((bottom - mid) - (top - mid)) * normalize(surf.B);
    float3 p2 = ((left - mid) - (right - mid)) * normalize(surf.T);
    return normalize(N - (p1 + p1));
}

inline void CalcCommonLightInfo(SurfaceInfo surf, inout LightingInfo li)
{
    li.H = normalize(li.L + surf.V);
    li.NdotH = dot(surf.N, li.H);
    li.NdotL = dot(surf.N, li.L);
}

inline void EvalDirectionalLight(SurfaceInfo surf, Light light, inout LightingInfo li)
{
    li.L = normalize(-light.direction.xyz);
    li.distance = -1; // infinity
    li.attenuation = 1;

    li.shadowFactor = (light.status == LIGHT_ENABLED_W_SHADOWMAP) ? (1 - ShadowFactor(surf.posW)) * (CloudShadowFactor(surf.posW)) : 1;
    CalcCommonLightInfo(surf, li);

}

inline void EvalPointLight(SurfaceInfo surf, Light light, inout LightingInfo li)
{
    li.L = light.position.xyz - surf.posW.xyz;
    li.distance = length(li.L);
    li.L = normalize(li.L);
    //li.attenuation = 1.0 / (light.constantAtt + light.linearAtt * li.distance + light.quadAtt * (li.distance * li.distance));

    if (li.distance > light.range)
    {
        li.attenuation = 0.0f;
    }
    else
    {
        li.attenuation = 1.0 / (light.constantAtt + (light.linearAtt * li.distance) + light.quadAtt * (li.distance * li.distance));
        li.attenuation *= pow(saturate(1 - pow((pow(li.distance, 2) / pow(light.range, 2)), 2)), 2);
    }
    
    li.shadowFactor = 1;
    CalcCommonLightInfo(surf, li);
}

inline void EvalSpotLight(SurfaceInfo surf, Light light, inout LightingInfo li)
{
    li.L = light.position.xyz - surf.posW.xyz;
    li.distance = length(li.L);
    li.L = normalize(li.L);
    //li.attenuation = 1.0 / (light.constantAtt + light.linearAtt * li.distance + light.quadAtt * (li.distance * li.distance));
    if (li.distance > light.range)
    {
        li.attenuation = 0.0f;
    }
    else
    {
        li.attenuation = 1.0 / (light.constantAtt + (light.linearAtt * li.distance) + light.quadAtt * (li.distance * li.distance));
        li.attenuation *= pow(saturate(1 - pow((pow(li.distance, 2) / pow(light.range, 2)), 2)), 2);
    }
    
    float minCos = cos(light.spotAngle);
    float maxCos = (minCos + 1.0f) / 2.0f; // squash between [0, 1]
    float cosAngle = dot(light.direction.xyz, -li.L);
    float intensity = smoothstep(minCos, maxCos, cosAngle);
    li.attenuation = intensity * li.attenuation;

    li.shadowFactor = 1;
    CalcCommonLightInfo(surf, li);
}


LightingInfo EvalLightingInfo(SurfaceInfo surf, Light light)
{
    LightingInfo li;
    switch (abs(light.lightType))
    {
        case DIRECTIONAL_LIGHT:
            EvalDirectionalLight(surf, light, li);
            break;
        case POINT_LIGHT:
            EvalPointLight(surf, light, li);
            break;
        case SPOT_LIGHT:
            EvalSpotLight(surf, light, li);
            break;
        default:
            break;
    }

    return li;
}

static const float GAMMA = 2.2;
static const float INV_GAMMA = 1.0 / GAMMA;

float LinearToSRGBColorChannel(float c)
{
    const float threshold = 0.0031308;
    const float a = 0.055;
    const float gamma = 1.0 / 2.4;
    const float scale = 12.92;
    const float beta = 1.055;
    
    return (c <= threshold) ? c * scale : (beta * pow(c, gamma) - a);
}

float SRGBToLinearColorChannel(float c)
{
    const float threshold = 0.04045;
    const float a = 0.055;
    const float gamma = 2.4;
    const float scale = 12.92;
    const float beta = 1.055;

    return (c <= threshold) ? c / scale : pow((c + a) / beta, gamma);
}

float3 LINEARtoSRGB(float3 color)
{
    //lagacy formula
    //return pow(color, INV_GAMMA);
    
    //unity formula
    //color = saturate(color);
    //return max(1.055f * pow(color, 0.416666667f) - 0.055f, 0.0f);
    
    //optimized formula
    // 각 채널(R,G,B)에 대해 적용
    return float3(
        LinearToSRGBColorChannel(color.r),
        LinearToSRGBColorChannel(color.g),
        LinearToSRGBColorChannel(color.b)
    );
}

float4 SRGBtoLINEAR(float4 srgbIn)
{
    //lagacy formula
    //return float4(pow(srgbIn.xyz, GAMMA), srgbIn.w);
    
    //unity formula
    //return srgbIn * (srgbIn * (srgbIn * 0.305306011f + 0.682171111f) + 0.012522878f);
    
    //optimized formula
    // 각 채널(R,G,B)에 대해 적용
    return float4(
        SRGBToLinearColorChannel(srgbIn.r),
        SRGBToLinearColorChannel(srgbIn.g),
        SRGBToLinearColorChannel(srgbIn.b),
        srgbIn.a
    );
}

float Square(float x)
{
    return x * x;
}

#endif
