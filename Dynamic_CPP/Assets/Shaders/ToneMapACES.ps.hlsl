#include "Sampler.hlsli"
#include "Shading.hlsli"
#include "ACES.hlsli"

Texture2D Colour : register(t0);

cbuffer UseTonemap : register(b0)
{
    bool useTonemap;
    bool useFilmic;
    float filmSlope;
    float filmToe;
    float filmShoulder;
    float filmBlackClip;
    float filmWhiteClip;
    float toneMapExposure;
}

float CalcLuminance(float3 color)
{
    return max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
}

float3 LinearToPQ(float3 color)
{
    // ST2084 표준 상수 (일부 파라미터는 상황에 따라 조정 가능)
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    
    // color 는 선형 공간의 값이며, 일반적으로 피크 밝기(예: 1000nit 혹은 4000nit 등) 기준으로 정규화되어 있어야 합니다.
    // 필요에 따라 정규화 단계를 추가하세요.
    float3 Lp = pow(color, m1);
    float3 numerator = c1 + c2 * Lp;
    float3 denominator = 1.0f + c3 * Lp;
    float3 pqValue = pow(numerator / denominator, m2);
    
    return pqValue;
}

float3 aces_approx(float3 color)
{
    color *= 0.6f;
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0f, 1.0f);
}

// Determines the color based on exposure settings
float3 CalcExposedColor(float3 color, float avgLuminance, float threshold, out float exposure)
{
	// Use geometric mean
    avgLuminance = max(avgLuminance, 0.001f);
    float keyValue = filmSlope;
    float linearExposure = (filmSlope / avgLuminance);
    exposure = log2(max(linearExposure, 0.0001f));
    exposure -= threshold;
    return exp2(exposure) * color;
}

float3 uncharted2_tonemap_partial(float3 x)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 uncharted2_filmic(float3 v)
{
    float exposure_bias = 2.0f;
    float3 curr = uncharted2_tonemap_partial(v * exposure_bias);

    float3 W = float3(11.2f, 11.2f, 11.2f);
    float3 white_scale = float3(1.0f, 1.0f, 1.0f) / uncharted2_tonemap_partial(W);
    return curr * white_scale;
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 colour = Colour.Sample(PointSampler, IN.texCoord);
    float2 texelSize = float2(1.0f / 1920.0f, 1.0f / 1080.0f);
    float3 toneMapped = 0;
    
    float avgLuminance = toneMapExposure;
    
    [branch]
    if (useTonemap)
    {
        [branch]
        if (useFilmic)
        {
            toneMapped = uncharted2_filmic(colour.rgb * avgLuminance);
            toneMapped = LINEARtoSRGB(toneMapped);
        }
        else
        {
            toneMapped = ApplyACES_Full(colour.rgb * avgLuminance);
            toneMapped = LINEARtoSRGB(toneMapped);
        }
    }
    else
    {
        toneMapped = colour.rgb;
        toneMapped = LinearToPQ(toneMapped);
    }

    return float4(toneMapped, 1.f);
}