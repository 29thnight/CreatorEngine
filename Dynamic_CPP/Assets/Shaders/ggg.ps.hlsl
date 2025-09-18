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

Texture2D gMainTexture : register(t0);
Texture2D gNoiseTexture : register(t1);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float normalizedAge = input.particleAge / input.particleLifeTime;
    
     // 노이즈 기반 UV 애니메이션
    float2 noiseUV = input.texCoord + normalizedAge * float2(0.2, 0.3);
    float4 noiseValue = gNoiseTexture.Sample(gLinearSampler, noiseUV);
    
    // 메인 텍스처 UV에 노이즈의 zw 성분을 더해서 움직임 효과
    float2 animatedUV = input.texCoord + noiseValue.zw * 0.1; // 강도 조절
    
    float4 diffuseColor = gMainTexture.Sample(gLinearSampler, animatedUV * float2(2, 1));
    
    
    
    output.color = diffuseColor * input.color;
    
    return output;
}