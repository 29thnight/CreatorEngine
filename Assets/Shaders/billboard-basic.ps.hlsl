Texture2D gTexture : register(t0);
SamplerState AnisotropicSampler : register(s0);
SamplerState LinearSampler : register(s1);
SamplerState ClampSampler : register(s2);
SamplerState CubemapSampler : register(s3);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 color = gTexture.Sample(AnisotropicSampler, input.TexCoord);
    return color;
}
