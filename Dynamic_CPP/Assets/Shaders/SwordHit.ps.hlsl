// MeshParticlePS.hlsl - 3D 메시 파티클 픽셀 셰이더
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
    float Age : TEXCOORD2;
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

cbuffer SpriteAnimationBuffer : register(b0)
{
    uint frameCount; // 총 프레임 수
    float animationDuration;
    uint2 gridSize; // 스프라이트 시트 격자 크기 (columns, rows)
};

Texture2D gMaskTexture : register(t0);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(VSOutput input)
{
    PixelOutput output;

    // age를 기반으로 현재 프레임 계산
    float normalizedTime = fmod(input.Age, animationDuration) / animationDuration;
    uint currentFrame = 2;

    // 스프라이트 애니메이션을 위한 UV 좌표 계산
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;

    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);

    float2 animatedUV = frameOffset + (input.TexCoord * frameSize);

    // 기본 텍스처 색상 가져오기 (애니메이션된 UV 사용)
    float4 texColor = gMaskTexture.Sample(gLinearSampler, animatedUV);

    float mask = texColor.r;
    float intensity = texColor.g;
    float flow = texColor.b;
    float opacity = texColor.a;

    // 최종 색상 계산
    float3 finalColor = input.Color.rgb * intensity;
    float finalAlpha = input.Color.a * mask * opacity;

    clip(finalAlpha - 0.05);

    output.color = float4(finalColor, finalAlpha);

    return output;
}