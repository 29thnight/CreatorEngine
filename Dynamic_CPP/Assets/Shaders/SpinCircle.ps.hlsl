// BillboardParticlePS.hlsl - 3D 메시 파티클 픽셀 셰이더
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

Texture2D gMainTexture : register(t0);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

#define pi 3.1415926

PixelOutput main(VSOutput input)
{
    PixelOutput output;
    
    float4 texColor = gMainTexture.Sample(gLinearSampler, input.TexCoord);
    
    float luminance = dot(texColor.rgb, float3(0.299, 0.587, 0.114));
    
    if (luminance < 0.1) // 검은색에 가까우면 버림
        discard;
    
    output.color = texColor * input.Color;
    
    return output;
}