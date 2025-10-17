// MeshParticlePS.hlsl - 3D �޽� ��ƼŬ �ȼ� ���̴�
struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION; // ���� ���� ��ġ
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
    
    // UV �ִϸ��̼� ���
    float2 dissolveUV = input.texCoord * float2(3.0, 6.0);
    dissolveUV.x += gTime;
    
    float4 dissolveData = gDissolveTexture.Sample(gLinearSampler, dissolveUV);

    float2 tempUV = input.texCoord;
    
    // �ؽ�ó ���ø�
    float4 smokeColor = gSmokeTexture.Sample(gLinearSampler, tempUV);
    float4 emissionColor = gEmissionTexture.Sample(gLinearSampler, tempUV);
    
    float dissolveValue = dissolveData.r;
    
    // Emission ������
    float emissionStrength = 2.0;
    float remapMin = -0.3;
    float remapMax = 1.0;
    float remappedEmission = saturate((emissionColor.r - remapMin) / (remapMax - remapMin));
    
    // ���� ���� ���
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
    finalAlpha = finalAlpha * brightnessMask * smoothstep(0.03, 0.12, emissionColor.r); // ���� ���Ŀ� ���ϱ�
    
    clip(finalAlpha - 0.05); // ���� ���ķ� Ŭ��
    
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}