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

Texture2D gMainTexture : register(t0);
Texture2D gNoiseTexture : register(t1);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);
#define pi 3.1415926

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float temp = input.texCoord.y * pi;
    
    // 노이즈 기반 UV 애니메이션
    float2 noiseUV = input.texCoord + (gTime / 2) * float2(0.2, 0.3);
    float4 noiseValue = gNoiseTexture.Sample(gLinearSampler, noiseUV);
    
    // 메인 텍스처 UV에 노이즈의 zw 성분을 더해서 움직임 효과
    float2 animatedUV = noiseUV + noiseValue.zw * 0.1; // 강도 조절
    
    float4 diffuseColor = gMainTexture.Sample(gLinearSampler, animatedUV * float2(2, 1));
    
    float emission = 1.5;
    
    float alphaWeight = diffuseColor.a * diffuseColor.a;
    float4 finalColor = lerp(diffuseColor, diffuseColor * input.color, alphaWeight);
    finalColor.rgb *= emission;
    float finalAlpha = diffuseColor.a * input.color.a * sin(temp);
    
    clip(finalAlpha - 0.01);
    
    output.color = float4(finalColor.rgb, finalAlpha);
    
    return output;
}