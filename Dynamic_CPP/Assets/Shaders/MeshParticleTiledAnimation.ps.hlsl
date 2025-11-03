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

cbuffer SpriteAnimationBuffer : register(b4)
{
    uint frameCount; // 총 프레임 수
    float animationDuration;
    uint2 gridSize; // 스프라이트 시트 격자 크기 (columns, rows)
};

Texture2D gDiffuseTexture : register(t0);
Texture2D gNoiseTexture : register(t1);
Texture2D gHUETexture : register(t2);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float normalizedAge = input.particleAge / input.particleLifeTime;
    
    float2 uv = input.texCoord; 
    float2 uv2 = input.texCoord;
    
    float s = smoothstep(0.0, 0.1, uv.x) * smoothstep(1.0, 0.9, uv.x)
            * smoothstep(0.0, 0.1, uv.y) * smoothstep(1.0, 0.9, uv.y);
    
    uv *= gridSize;
    uv.x += normalizedAge * 3.f;
    
    uv2.x -= normalizedAge;
    
    float noise = gNoiseTexture.Sample(gLinearSampler, uv).r + normalizedAge;
    float4 hueColor = gHUETexture.Sample(gLinearSampler, float2(noise, 0.f));
    
    float4 diffuseColor = 1 - gDiffuseTexture.Sample(gLinearSampler, uv);
    float no = gNoiseTexture.Sample(gLinearSampler, input.texCoord).r;
    float4 originDif = gDiffuseTexture.Sample(gLinearSampler, uv);
    
    
    float smoothDissolve = smoothstep(normalizedAge - 0.03, normalizedAge, diffuseColor.g);
    //float emissive = smoothstep(normalizedAge + 0.03, normalizedAge, diffuseColor.g);
    //float3 eColor = emissive * hueColor;
    
    float3 finalColor = input.color.rgb * diffuseColor.rgb; //+ float3(emissive, emissive, emissive);
    
    float finalAlpha = input.alpha * diffuseColor.r * s * no; // * smoothDissolve;
    
    clip(finalAlpha - 0.05);
    
    output.color = float4(finalColor.rgb, finalAlpha);
    
    return output;
}