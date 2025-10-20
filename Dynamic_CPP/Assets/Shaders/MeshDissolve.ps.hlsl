// MeshParticlePS.hlsl - 3D 메시 파티클 픽셀 셰이더
struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION; // 원본 로컬 위치
    float3 particleScale : PARTICLE_SCALE;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 viewDir : VIEW_DIR;
    float alpha : ALPHA;
    uint renderMode : RENDER_MODE;
    float particleAge : PARTICLE_AGE;
    float particleLifeTime : PARTICLE_LIFETIME;

};

struct PixelOutput
{
    float4 color : SV_Target;
};

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

cbuffer SpriteAnimationBuffer : register(b4)
{
    uint frameCount; // 총 프레임 수
    float animationDuration;
    uint2 gridSize; // 스프라이트 시트 격자 크기 (columns, rows)
};

Texture2D gNoiseTexture : register(t0);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);
#define pi 3.1415926

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float2 animatedUV = (input.texCoord * 1.0) * float2(gridSize.x, gridSize.y);
    animatedUV += gTime * animationDuration;
    float4 diffuseColor = gNoiseTexture.Sample(gLinearSampler, animatedUV);
    
    float3 finalColor;
    finalColor = input.color.rgb * diffuseColor.rgb;
    
    float finalAlpha = input.alpha;
    
    float colorBrightness = (finalColor.r + finalColor.g + finalColor.b) / 3.0;
    float brightnessMask = smoothstep(0.1, 0.6, colorBrightness);
    finalAlpha = finalAlpha * brightnessMask; // 기존 알파와 곱하기

    clip(finalAlpha - 0.05);
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}