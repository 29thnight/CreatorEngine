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

float3 ACESFilmicToneMapping(float3 color)
{
    color *= 0.6f;
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0f, 1.0f);
}

float3 FilmToneMap(float3 LinearColor)
{
    const float3x3 sRGB_2_AP0 = mul(XYZ_2_AP0_MAT, mul(D65_2_D60_CAT, sRGB_2_XYZ_MAT));
    const float3x3 sRGB_2_AP1 = mul(XYZ_2_AP1_MAT, mul(D65_2_D60_CAT, sRGB_2_XYZ_MAT));
    const float3x3 AP1_2_sRGB = mul(XYZ_2_sRGB_MAT, mul(D60_2_D65_CAT, AP1_2_XYZ_MAT));
	
#if 1
    float3 ACESColor = mul(sRGB_2_AP0, LinearColor);

	// --- Red modifier --- //
    const float RRT_RED_SCALE = 0.82;
    const float RRT_RED_PIVOT = 0.03;
    const float RRT_RED_HUE = 0;
    const float RRT_RED_WIDTH = 135;

    float saturation = rgb_2_saturation(ACESColor);
    float hue = rgb_2_hue(ACESColor);
    float centeredHue = center_hue(hue, RRT_RED_HUE);
    float hueWeight = Square(smoothstep(0, 1, 1 - abs(2 * centeredHue / RRT_RED_WIDTH)));
		
    ACESColor.r += hueWeight * saturation * (RRT_RED_PIVOT - ACESColor.r) * (1. - RRT_RED_SCALE);

	// Use ACEScg primaries as working space
    float3 WorkingColor = mul(AP0_2_AP1_MAT, ACESColor);
#else
	// Use ACEScg primaries as working space
	float3 WorkingColor = mul( sRGB_2_AP1, LinearColor );
#endif

    WorkingColor = max(0, WorkingColor);

	// Pre desaturate
    WorkingColor = lerp(dot(WorkingColor, AP1_RGB2Y), WorkingColor, 0.96);
	
    const half ToeScale = 1 + filmBlackClip - filmToe;
    const half ShoulderScale = 1 + filmWhiteClip - filmShoulder;
	
    const float InMatch = 0.18;
    const float OutMatch = 0.18;

    float ToeMatch;
    if (filmToe > 0.8)
    {
		// 0.18 will be on straight segment
        ToeMatch = (1 - filmToe - OutMatch) / filmSlope + log10(InMatch);
    }
    else
    {
		// 0.18 will be on toe segment

		// Solve for ToeMatch such that input of InMatch gives output of OutMatch.
        const float bt = (OutMatch + filmBlackClip) / ToeScale - 1;
        ToeMatch = log10(InMatch) - 0.5 * log((1 + bt) / (1 - bt)) * (ToeScale / filmSlope);
    }

    float StraightMatch = (1 - filmToe) / filmSlope - ToeMatch;
    float ShoulderMatch = filmShoulder / filmSlope - StraightMatch;
	
    half3 LogColor = log10(WorkingColor);
    half3 StraightColor = filmSlope * (LogColor + StraightMatch);
	
    half3 ToeColor = (-filmBlackClip) + (2 * ToeScale) / (1 + exp((-2 * filmSlope / ToeScale) * (LogColor - ToeMatch)));
    half3 ShoulderColor = (1 + filmWhiteClip) - (2 * ShoulderScale) / (1 + exp((2 * filmSlope / ShoulderScale) * (LogColor - ShoulderMatch)));

    ToeColor = LogColor < ToeMatch ? ToeColor : StraightColor;
    ShoulderColor = LogColor > ShoulderMatch ? ShoulderColor : StraightColor;

    half3 t = saturate((LogColor - ToeMatch) / (ShoulderMatch - ToeMatch));
    t = ShoulderMatch < ToeMatch ? 1 - t : t;
    t = (3 - 2 * t) * t * t;
    half3 ToneColor = lerp(ToeColor, ShoulderColor, t);

	// Post desaturate
    ToneColor = lerp(dot(ToneColor, AP1_RGB2Y), ToneColor, 0.93);

    ToneColor = mul(AP1_2_sRGB, ToneColor);

	//return saturate( ToneColor );
    return max(0, ToneColor);
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float3 colour = Colour.Sample(PointSampler, IN.texCoord).rgb;
    float3 toneMapped = 0;
    [branch]
    if (useTonemap)
    {
        if (useFilmic)
        {
            toneMapped = FilmToneMap(colour);
        }
        else
        {
            toneMapped = ACESFilmicToneMapping(colour);
        }
    }
    else
    {
        toneMapped = colour;
    }
    
    return float4(toneMapped * toneMapExposure, 1);
}