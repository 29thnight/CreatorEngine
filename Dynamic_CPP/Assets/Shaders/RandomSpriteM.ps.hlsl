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
    uint randomFrame = input.TexIndex % frameCount;
    
    // age�� ������� ���� ������ ���
    float normalizedTime = fmod(input.Age, animationDuration) / animationDuration;
    
    // ��������Ʈ �ִϸ��̼��� ���� UV ��ǥ ���
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = randomFrame % gridSize.x;
    uint frameY = randomFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (input.TexCoord * frameSize);
    
    // ����ũ �ؽ�ó���� ����ũ�� ��������
    float4 temp = gTexture.Sample(linearSampler, animatedUV);
    float maskValue = temp.r;
    float alpha = temp.a;
    
    // ����ũ���� �ʹ� ������ �ȼ� ���
    if (maskValue < 0.01f)
        discard;
    
    float3 baseColor = input.Color.rgb * maskValue;

    float3 finalColor = baseColor;

    float finalAlpha = input.Color.a * alpha;
    
    return float4(finalColor, finalAlpha);
}