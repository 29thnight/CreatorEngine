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
    float2 dissolveUV = input.texCoord * float2(1.0, 2.0);
    dissolveUV.x *= normalizedAge * 5;
    float4 dissolveData = gDissolveTexture.Sample(gLinearSampler, dissolveUV);

    float2 tempUV = input.texCoord;

    // 원하는 구간 설정 (예: 0.25~1.0)
    float Umin = 0.00;
    float Umax = 0.75;

    float edgeFade = 1.0;
    float fadeWidth = 0.1;
    
    // 왼쪽 가장자리
    if (tempUV.x < Umin + fadeWidth)
        edgeFade *= smoothstep(Umin, Umin + fadeWidth, tempUV.x);

    // 오른쪽 가장자리  
    if (tempUV.x > Umax - fadeWidth)
        edgeFade *= smoothstep(Umax, Umax - fadeWidth, tempUV.x);

    // 완전히 범위 밖이면 버리기
    if (tempUV.x < Umin || tempUV.x > Umax)
        discard;
    
    // 선택한 구간을 다시 0~1로 정규화
    float2 clippedUV;
    clippedUV.x = (tempUV.x - Umin) / (Umax - Umin);
    clippedUV.y = tempUV.y;
    
    
    // 텍스처 샘플링
    float4 smokeColor = gSmokeTexture.Sample(gLinearSampler, clippedUV);
    float4 emissionColor = gEmissionTexture.Sample(gLinearSampler, clippedUV);
    
    float dissolveValue = dissolveData.r;
    
    float dissolveOutStart = 0.4; // 60%부터 dissolve-out 시작
    
    float globalFade = 1.0;
    if (normalizedAge > dissolveOutStart)
    {
        float fadeProgress = (normalizedAge - dissolveOutStart) / (1.0 - dissolveOutStart);
        globalFade = 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }
    
    // Emission 리맵핑
    float emissionStrength = 2.0;
    float remapMin = -0.3;
    float remapMax = 1.0;
    float remappedEmission = saturate((emissionColor.r - remapMin) / (remapMax - remapMin));
    
    // 최종 색상 계산
    float smokeThreshold = 0.01;
    float3 adjustedSmoke = input.color.rgb;
    float smokeIntensity = (smokeColor.r + smokeColor.g + smokeColor.b) / 3.0;

    // smoke texture의 검은 부분의 알파값도 함께 줄이기
    float smokeAlphaMultiplier = saturate(smokeIntensity / smokeThreshold);

    float dissolveAlpha = step(0.5, dissolveValue);

    float3 baseColor = adjustedSmoke;
    float3 finalColor = pow(baseColor + (emissionColor.rgb * emissionStrength * remappedEmission), 2);
    finalColor *= 3.0 * globalFade;
    
    float finalAlpha = input.alpha * smokeColor.a * smokeAlphaMultiplier * edgeFade * dissolveAlpha * globalFade;
    
    clip(finalAlpha - 0.01);
    
    float colorBrightness = (finalColor.r + finalColor.g + finalColor.b) / 3.0;
    if (colorBrightness < 0.1)
        discard;
    
    finalColor = pow(finalColor, 0.7);
    
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}