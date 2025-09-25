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
    
    // ����ũ �ؽ�ó���� ����ũ�� ��������
    float4 temp = gTexture.Sample(linearSampler, animatedUV);
    float maskValue = temp.r;
    float dissolve = temp.g;
    float emissive = temp.b;
    float alpha = temp.a;
    
    // ��ƼŬ ���� ���� ������ �Ӱ谪 ���
    float dissolveThreshold = saturate(input.Age);
    
    // ������ ȿ��: ����ũ���� �Ӱ谪���� ������ �ȼ� ���
    if (dissolve < dissolveThreshold)
        discard;
    
    // �Է� ������ �⺻������ ����ϰ� ����ũ�� ���� ����
    float3 baseColor = input.Color.rgb * maskValue;
    float3 emissiveColor = float3(1.4f, 0.7f, 0.37f);

    // ���̽ú� ���� ���� �⺻ ����� ���̽ú� ������ ����
    float3 finalColor = lerp(baseColor, baseColor + emissiveColor, pow(emissive, 3));
    
    // ���� ���İ� ��� (����ũ�� �� �Է� ����)
    float finalAlpha = input.Color.a * alpha * smoothstep(dissolveThreshold, dissolveThreshold + 0.2, dissolve);
    
    return float4(finalColor, finalAlpha);
}