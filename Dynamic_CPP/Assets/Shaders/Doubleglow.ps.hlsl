// Sparkle.hlsl - ��¦�̴� ����Ʈ�� ���� �ȼ� ���̴�

// �ؽ�ó �� ���÷� ����
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

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
    float4 texColor = sparkleTexture.Sample(linearSampler, input.TexCoord);
    float intensity = texColor.r;
    
    float3 finalColor = texColor.rgb;
    float finalAlpha = texColor.a * input.Color.a * intensity;
    
    //if (finalAlpha < 0.1)
    //{
    //    discard;
    //}
    
    return float4(finalColor, finalAlpha);
}