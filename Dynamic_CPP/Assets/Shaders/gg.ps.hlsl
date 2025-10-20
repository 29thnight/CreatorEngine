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
Texture2D gEmissionTexture : register(t1);
Texture2D gDissolveTexture : register(t2);
Texture2D gSmokeTexture : register(t3);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float normalizedAge = input.particleAge / input.particleLifeTime;
    
    // UV 애니메이션 계산
    float2 dissolveUV = input.texCoord * float2(3.0, 6.0);
    dissolveUV.x += gTime;
    
    float4 dissolveData = gDissolveTexture.Sample(gLinearSampler, dissolveUV);

    float2 tempUV = input.texCoord;
    
    // 텍스처 샘플링
    float4 smokeColor = gSmokeTexture.Sample(gLinearSampler, tempUV);
    float4 emissionColor = gEmissionTexture.Sample(gLinearSampler, tempUV);
    
    float dissolveValue = dissolveData.r;
    
    // Emission 리맵핑
    float emissionStrength = 2.0;
    float remapMin = -0.3;
    float remapMax = 1.0;
    float remappedEmission = saturate((emissionColor.r - remapMin) / (remapMax - remapMin));
    
    // 최종 색상 계산
    float smokeThreshold = 0.01;
    float3 adjustedSmoke = input.color.rgb;
    float smokeIntensity = (smokeColor.r + smokeColor.g + smokeColor.b) / 3.0;

    float s = 1 - sin(normalizedAge * 3.1416926);
    float dissolveAlpha = smoothstep(s, s + 0.2, dissolveValue);

    float3 baseColor = adjustedSmoke;
    float3 finalColor = pow(baseColor + (emissionColor.rgb * emissionStrength * remappedEmission), 2);
    
    float finalAlpha = input.alpha * smokeColor.a * dissolveAlpha;
    
    float colorBrightness = (finalColor.r + finalColor.g + finalColor.b) / 3.0;
    float brightnessMask = smoothstep(0.03, 0.12, colorBrightness);
    finalAlpha = finalAlpha * brightnessMask * smoothstep(0.03, 0.12, emissionColor.r); // 기존 알파와 곱하기
    
    clip(finalAlpha - 0.05); // 최종 알파로 클립
    
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}