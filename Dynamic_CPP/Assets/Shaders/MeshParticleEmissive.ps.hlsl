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

Texture2D gDiffuseTexture : register(t0);
Texture2D gEmissiveTexture : register(t1);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    if (input.worldPos.y <= 0.0)
        discard;
    
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
    if (diffuseColor.a < 0.01)
        discard;
    
    if (input.alpha <= 0.01)
        discard;
    
    float3 finalColor;
    
    float3 emissiveColor = gEmissiveTexture.Sample(gLinearSampler, input.texCoord).rgb;
    float luminance = dot(emissiveColor, float3(0.299, 0.587, 0.114)); // 흑백 밝기값(luminance)로 변환하는 가중치
    
    // 까맣다면 흰색으로
    if(luminance < 0.1)
        emissiveColor = float3(1, 1, 1);
    
    finalColor = input.color.rgb * diffuseColor.rgb * emissiveColor;
    
    float finalAlpha = input.alpha * diffuseColor.a;
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}