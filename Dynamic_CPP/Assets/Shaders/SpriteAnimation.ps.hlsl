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
    // age�� ������� ���� ������ ���
    float normalizedTime = fmod(input.Age, animationDuration) / animationDuration;
    uint currentFrame = (uint) (normalizedTime * frameCount) % frameCount;
    
    // ��������Ʈ �ִϸ��̼��� ���� UV ��ǥ ���
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (input.TexCoord * frameSize);
    
    // �⺻ �ؽ�ó ���� �������� (�ִϸ��̼ǵ� UV ���)
    float4 texColor = gTexture.Sample(linearSampler, animatedUV);
    
    // �ؽ�ó�� ���İ��� �ʹ� ������ �ȼ� ���
    if (texColor.a < 0.01f)
        discard;
    
    // ���� �ؽ�ó ������ �⺻���� ���
    float3 baseColor = texColor.rgb;
    
    // �Է� ������ additive ������� ���ϱ� (��� ����)
    float3 coloredTexture = baseColor + (input.Color.rgb * 0.5f);
    
    float3 finalColor = coloredTexture;
    
    // ���� ���İ� ��� (���� �ؽ�ó ���� �� �Է� ����)
    float finalAlpha = texColor.a * input.Color.a;
    
    return float4(finalColor, finalAlpha);
}