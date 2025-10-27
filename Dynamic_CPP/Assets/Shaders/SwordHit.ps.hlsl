// MeshParticlePS.hlsl - 3D �޽� ��ƼŬ �ȼ� ���̴�
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
    uint frameCount; // �� ������ ��
    float animationDuration;
    uint2 gridSize; // ��������Ʈ ��Ʈ ���� ũ�� (columns, rows)
};

Texture2D gMaskTexture : register(t0);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(VSOutput input)
{
    PixelOutput output;

    // age�� ������� ���� ������ ���
    float normalizedTime = fmod(input.Age, animationDuration) / animationDuration;
    uint currentFrame = 2;

    // ��������Ʈ �ִϸ��̼��� ���� UV ��ǥ ���
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;

    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);

    float2 animatedUV = frameOffset + (input.TexCoord * frameSize);

    // �⺻ �ؽ�ó ���� �������� (�ִϸ��̼ǵ� UV ���)
    float4 texColor = gMaskTexture.Sample(gLinearSampler, animatedUV);

    float mask = texColor.r;
    float intensity = texColor.g;
    float flow = texColor.b;
    float opacity = texColor.a;

    // ���� ���� ���
    float3 finalColor = input.Color.rgb * intensity;
    float finalAlpha = input.Color.a * mask * opacity;

    clip(finalAlpha - 0.05);

    output.color = float4(finalColor, finalAlpha);

    return output;
}