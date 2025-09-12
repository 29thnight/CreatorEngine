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
};

struct PixelOutput
{
    float4 color : SV_Target;
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
    
    // 공간적 마스킹 - 특정 구역을 파티클 나이에 따라 제거
    float2 centeredUV = input.texCoord - float2(0.5, 0.5);
    float angle = atan2(centeredUV.y, centeredUV.x);
    float normalizedAngle = (angle + 3.14159) / (2.0 * 3.14159); // 0~1 범위
    
    // 파티클 나이가 0.75 이상일 때 270도까지만 보이게 (나머지 90도 제거)
    if (input.particleAge >= 0.75)
    {
        // 0.75(270도) 이상 각도 범위 제거, 0~0.75 범위만 유지
        if (normalizedAngle > 0.75)
            discard;
    }
    
    // UV 애니메이션 계산
    float2 dissolveUV = input.texCoord * float2(1.0, 2.0);
    float4 dissolveData = gDissolveTexture.Sample(gLinearSampler, dissolveUV);
    float2 tempUV = input.texCoord;
    tempUV.x += (dissolveData.z) * input.particleAge * 0.8;
    
    // 텍스처 샘플링
    float4 smokeColor = gSmokeTexture.Sample(gLinearSampler, tempUV);
    float4 emissionColor = gEmissionTexture.Sample(gLinearSampler, tempUV);
    float dissolveValue = dissolveData.a;
    
    if (dissolveValue < 0.8)
        discard;
        
    // Emission 리맵핑
    float emissionStrength = 2.0;
    float remapMin = -0.3;
    float remapMax = 1.0;
    float remappedEmission = saturate((emissionColor.r - remapMin) / (remapMax - remapMin));
    
    // 최종 색상 계산
    float3 baseColor = input.color;
    float3 emissionContrib = emissionColor.rgb * emissionStrength * remappedEmission;
    float3 finalColor = baseColor + emissionContrib;
    float finalAlpha = input.alpha * smokeColor.a * dissolveValue;
    
    if (finalAlpha < 0.1)
        discard;
        
    output.color = float4(finalColor, finalAlpha);
    return output;
}