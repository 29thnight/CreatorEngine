#include "Sampler.hlsli"
#include "Shading.hlsli"
#include "ACES.hlsli"

static const int ToneMap_Reinhard   = 0;
static const int ToneMap_ACES       = 1;
static const int ToneMap_Uncharted2 = 2;
static const int ToneMap_HDR10      = 3;
static const int ToneMap_Unreal     = 4;

Texture2D Colour : register(t0);

cbuffer UseTonemap : register(b0)
{
    int m_operatorType;
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

float ColorToLuminanceReinhard(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 LinearToPQ(float3 color)
{
    // ST2084 표준 상수 (일부 파라미터는 상황에 따라 조정 가능)
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    
    const float MaxHDR10Nits = 1000.0f;
    float3 normalizedColor = color * (MaxHDR10Nits / 10000.0f);

    float3 Lp = pow(normalizedColor, m1);
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

float3 ACESFilmic(float3 color)
{
    const float3x3 ACESInputMat =
    {
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    };
    const float3x3 ACESOutputMat =
    {
        1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    };
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

float3 ReinhardToneMapping(float3 color)
{
    float luma = ColorToLuminanceReinhard(color);
    float toneMappedLuma = luma / (1. + luma);
    if (luma > 1e-6)
        color *= toneMappedLuma / luma;
    
    color = pow(color, 1. / GAMMA);
    return color;
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float ComputeEV100(float aperture, float shutterTime)
{
    aperture = max(aperture, 1e-4f);
    shutterTime = max(shutterTime, 1e-6f);
    return log2((aperture * aperture) / shutterTime);
}

float ApplyExposureCompensation(float ev100, float exposureCompensation)
{
    return ev100 - exposureCompensation;
}

float ComputeLuminanceFromEV100(float ev100)
{
    return 1.2f * exp2(ev100);
}

float ComputeExposureMultiplier(float luminance)
{
    return 1.0f / (luminance + 1e-4f);
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 colour = Colour.Sample(PointSampler, IN.texCoord);
    float3 toneMapped = 0;

    [branch]
    switch(m_operatorType)
    {
    case ToneMap_Reinhard:
            toneMapped = ReinhardToneMapping(colour.rgb * toneMapExposure);
        break;
    case ToneMap_ACES:
            toneMapped = ApplyACES_Full(colour.rgb * toneMapExposure);
        break;
    case ToneMap_Uncharted2:
            toneMapped = uncharted2_filmic(colour.rgb * toneMapExposure);
            toneMapped = LINEARtoSRGB(toneMapped);
        break;
    case ToneMap_Unreal:
            toneMapped = ACESFilmic(colour.rgb * toneMapExposure);
            toneMapped = LINEARtoSRGB(toneMapped);
        break;
    case ToneMap_HDR10:
            toneMapped = LinearToPQ(colour.rgb * toneMapExposure);
        break;
    default:
            toneMapped = colour.rgb; // No tonemapping
        break;
    }

    return float4(toneMapped, 1.f);
}