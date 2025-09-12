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
    
    // UV �ִϸ��̼� ���
    float2 dissolveUV = input.texCoord * float2(1.0, 2.0);
    float4 dissolveData = gDissolveTexture.Sample(gLinearSampler, dissolveUV);
    float2 tempUV = input.texCoord;
    tempUV.x += 0.3;
    tempUV.x += (dissolveData.z) * input.particleAge * 0.8;

    // �̷��� ó��
    if (tempUV.x > 1.0)
    {
        tempUV.x = 2.0 - tempUV.x; // 1.0�� ������ �ſ�ó�� ����
    }
    else if (tempUV.x < 0.0)
    {
        tempUV.x = -tempUV.x; // 0.0 �̸��̸� ����
    }
    
    // �ؽ�ó ���ø�
    float4 smokeColor = gSmokeTexture.Sample(gLinearSampler, tempUV * float2(1.2, 1));
    float4 emissionColor = gEmissionTexture.Sample(gLinearSampler, tempUV);
    float dissolveValue = dissolveData.a;
    
    // Emission ������
    float emissionStrength = 2.0;
    float remapMin = -0.3;
    float remapMax = 1.0;
    float remappedEmission = saturate((emissionColor.r - remapMin) / (remapMax - remapMin));
    
    // ���� ���� ���
    float3 baseColor = input.color.rgb * smokeColor.rgb;
    float3 finalColor = baseColor + (emissionColor.rgb * emissionStrength * remappedEmission);
    
    // ���� ���̵� ���
    float uvFade = 1.0 - saturate((tempUV.x - 0.7) / 0.1);
    float ageFade = 1.0 - (input.particleAge * input.particleAge);
    float finalAlpha = input.alpha * smokeColor.a * ageFade * dissolveValue * uvFade;
    
    if (finalAlpha < 0.1)
        discard;
    
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}