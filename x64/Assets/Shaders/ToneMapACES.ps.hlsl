#include "Sampler.hlsli"
#include "Shading.hlsli"

Texture2D Colour : register(t0);

cbuffer UseTonemap : register(b0)
{
    bool useTonemap;
    float filmSlope;
    float filmToe;
    float filmShoulder;
    float filmBlackClip;
    float filmWhiteClip;
}

const float a = 278.5085;
const float b = 10.7772;
const float c = 293.6045;
const float d = 88.7122;
const float e = 80.6889;

float3 AcesToneMap_UE4(float3 acesColour)
{
    return saturate((acesColour * (filmSlope * acesColour + filmToe)) / 
    (acesColour * (filmShoulder * acesColour + filmBlackClip) + filmWhiteClip));
}

float3 FilmicCurve(float3 color)
{
    // ACES 기준 input/output 매핑값
    const float InMatch = 0.18;
    const float OutMatch = 0.18;

    // Toe/Straight/Shoulder 연결 포인트 계산
    float toeMatch;
    if (filmToe > 0.8)
    {
        toeMatch = (1.0 - filmToe - OutMatch) / filmSlope + log10(InMatch);
    }
    else
    {
        float bt = (OutMatch + filmBlackClip) / (1.0 + filmBlackClip - filmToe) - 1.0;
        toeMatch = log10(InMatch) - 0.5 * log((1.0 + bt) / (1.0 - bt)) * ((1.0 + filmBlackClip - filmToe) / filmSlope);
    }

    float straightMatch = (1.0 - filmToe) / filmSlope - toeMatch;
    float shoulderMatch = filmShoulder / filmSlope - straightMatch;

    // 로그 변환
    float3 logColor = log10(max(color, 1e-6));

    // 톤 커브 연산
    float3 toeColor = (-filmBlackClip) + (2.0 * (1.0 + filmBlackClip - filmToe)) /
                      (1.0 + exp(-2.0 * filmSlope / (1.0 + filmBlackClip - filmToe) * (logColor - toeMatch)));

    float3 shoulderColor = (1.0 + filmWhiteClip) - (2.0 * (1.0 + filmWhiteClip - filmShoulder)) /
                           (1.0 + exp(2.0 * filmSlope / (1.0 + filmWhiteClip - filmShoulder) * (logColor - shoulderMatch)));

    float3 straightColor = filmSlope * (logColor + straightMatch);

    // 세 영역 블렌딩
    float3 result = straightColor;
    result = (logColor < toeMatch) ? toeColor : result;
    result = (logColor > shoulderMatch) ? shoulderColor : result;

    float3 t = saturate((logColor - toeMatch) / (shoulderMatch - toeMatch));
    t = (3.0 - 2.0 * t) * t * t; // cubic smoothstep

    result = lerp(toeColor, shoulderColor, t);

    return max(result, 0.0);
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
        toneMapped = FilmicCurve(colour);
    }
    else
    {
        toneMapped = colour;
    }
    
    return float4(toneMapped, 1);
}