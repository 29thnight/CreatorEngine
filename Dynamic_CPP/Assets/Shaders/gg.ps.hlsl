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
    
    // ������ ����ŷ - Ư�� ������ ��ƼŬ ���̿� ���� ����
    float2 centeredUV = input.texCoord - float2(0.5, 0.5);
    float angle = atan2(centeredUV.y, centeredUV.x);
    float normalizedAngle = (angle + 3.14159) / (2.0 * 3.14159); // 0~1 ����
    
    // ��ƼŬ ���̰� 0.75 �̻��� �� 270�������� ���̰� (������ 90�� ����)
    if (input.particleAge >= 0.75)
    {
        // 0.75(270��) �̻� ���� ���� ����, 0~0.75 ������ ����
        if (normalizedAngle > 0.75)
            discard;
    }
    
    // UV �ִϸ��̼� ���
    float2 dissolveUV = input.texCoord * float2(1.0, 2.0);
    float4 dissolveData = gDissolveTexture.Sample(gLinearSampler, dissolveUV);
    float2 tempUV = input.texCoord;
    tempUV.x += (dissolveData.z) * input.particleAge * 0.8;
    
    // �ؽ�ó ���ø�
    float4 smokeColor = gSmokeTexture.Sample(gLinearSampler, tempUV);
    float4 emissionColor = gEmissionTexture.Sample(gLinearSampler, tempUV);
    float dissolveValue = dissolveData.a;
    
    if (dissolveValue < 0.8)
        discard;
        
    // Emission ������
    float emissionStrength = 2.0;
    float remapMin = -0.3;
    float remapMax = 1.0;
    float remappedEmission = saturate((emissionColor.r - remapMin) / (remapMax - remapMin));
    
    // ���� ���� ���
    float3 baseColor = input.color;
    float3 emissionContrib = emissionColor.rgb * emissionStrength * remappedEmission;
    float3 finalColor = baseColor + emissionContrib;
    float finalAlpha = input.alpha * smokeColor.a * dissolveValue;
    
    if (finalAlpha < 0.1)
        discard;
        
    output.color = float4(finalColor, finalAlpha);
    return output;
}