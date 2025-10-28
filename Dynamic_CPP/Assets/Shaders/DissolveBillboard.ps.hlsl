// SpriteAnimation.hlsl - ��������Ʈ �ִϸ��̼� �����ϴ� �ȼ� ���̴� 
// �ؽ�ó �� ���÷� ����
Texture2D gTexture : register(t0);
SamplerState linearSampler : register(s0);

// Sprite Animation Constant Buffer
cbuffer SpriteAnimationBuffer : register(b0)
{
    uint frameCount; // �� ������ ��
    float animationDuration;
    uint2 gridSize; // ��������Ʈ ��Ʈ ���� ũ�� (columns, rows)
};

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

struct ParticleData
{
    float3 position;
    float pad1;

    float3 velocity;
    float pad2;

    float3 acceleration;
    float pad3;

    float2 size;
    float age;
    float lifeTime;

    float rotation;
    float rotatespeed;
    float2 pad4;

    float4 color;

    uint isActive;
    float3 pad5;
};

StructuredBuffer<ParticleData> g_Particles : register(t0);

// �ȼ� ���̴� �Է� ����ü
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
    float Age : TEXCOORD2;
};

float4 main(VSOutput input) : SV_TARGET
{
    // age�� ������� ���� ������ ���
    float normalizedTime = input.Age;
    uint currentFrame = (uint) (normalizedTime * frameCount) % frameCount;
    
    float2 uv = input.TexCoord;
    
    // ��������Ʈ �ִϸ��̼��� ���� UV ��ǥ ���
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (uv * frameSize);
    
    // �⺻ �ؽ�ó ���� �������� (�ִϸ��̼ǵ� UV ���)
    float4 texColor = gTexture.Sample(linearSampler, animatedUV);
    
    // �ؽ�ó�� ���İ��� �ʹ� ������ �ȼ� ���
    if (texColor.a < 0.01f)
        discard;
    
    float alpha = smoothstep(normalizedTime, normalizedTime + 0.2, texColor.r);
    
    // ���� �ؽ�ó ������ �⺻���� ���
    float3 baseColor = texColor.rgb;
    
    // �Է� ������ additive ������� ���ϱ� (��� ����)
    float3 coloredTexture = baseColor * input.Color.rgb;
    
    float3 finalColor = coloredTexture;
    
    // ���� ���İ� ��� (���� �ؽ�ó ���� �� �Է� ����)
    float finalAlpha = texColor.a * input.Color.a * alpha;
    
    return float4(finalColor, finalAlpha);
}