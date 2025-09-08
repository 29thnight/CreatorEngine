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
Texture2D gDissolveTexture1 : register(t1);
Texture2D gDissolveTexture2 : register(t2);
Texture2D gEndEffectTexture : register(t3);
SamplerState gLinearSampler : register(s0);
SamplerState gClampSampler : register(s1);

PixelOutput main(PixelInput input)
{
    PixelOutput output;

    float2 uv = input.texCoord;
    float time = input.particleAge;
    
    // �ؽ�ó UV �̵� ����
    //float2 offsetUV = uv + float2(time * 0.5, 0.0f);
    float2 offsetUV = uv;
    float2 offsetUV2 = uv;
    float2 offsetUV3 = uv;
    float moveSpeed = 1.0;
    offsetUV.x = uv.x * pow(time, 0.1);
    
    float2 dissolveUV1 = offsetUV;
    float2 dissolveUV2 = offsetUV;
    
    // �ؽ�ó ���ø�
    float4 baseTexture = gDiffuseTexture.Sample(gClampSampler, offsetUV) * float4(1.0f, 0.0f, 0.0f, 1.0f);
    float4 dissolveColor1 = gDissolveTexture1.Sample(gClampSampler, offsetUV) * float4(3.0f, 3.0f, 3.0f, 1.0f);
    float4 dissolveColor2 = gDissolveTexture2.Sample(gClampSampler, offsetUV);
    float4 dissolveColor3 = gEndEffectTexture.Sample(gClampSampler, offsetUV) * float4(0.0, 0.0, 0.0, 1.0);
    float4 finalColor = float4(0.0f, 0.0f, 0.0f, 1.0f);

    finalColor.rgb = lerp(baseTexture.rgb, dissolveColor1.rgb, dissolveColor1.a);
    finalColor.a = baseTexture.a;
    float delayStart = 0.2;
    float delayEnd = 0.8;
    float fadeAmount = smoothstep(delayStart, delayEnd, time);

    // �⺻ ������ ȿ��
    float dissolveAlpha = lerp(1.0f, dissolveColor2.a, fadeAmount);

    // �ð��� �����鼭 ��ü������ ���̵�ƿ�
    float trailFade = 1.0 - smoothstep(0.5, 1.0, time); // 60%���� 100%���� ������ �����

    // �� ȿ���� ���ؼ� ����
    finalColor.a *= dissolveAlpha * trailFade;
    
    if (time > 0.4)
    {
        float delayedTime = max(0.0, time - 0.1); // ���� �ð� ���̱�
        float transitionTime = (delayedTime - 0.3) / 0.6;
        transitionTime = saturate(transitionTime); // smoothstep ��� saturate
        float dissolve3Fade = 1.0 - smoothstep(0.8, 1.4, delayedTime); // �� �ʰ� ����
    
        if (dissolveColor3.a > 0.05) // �Ӱ谪 ���߱�
        {
            float2 fadingUV = offsetUV3;
            fadingUV *= (1.0 + (1.0 - dissolve3Fade) * 2.0);
    
            float4 fadingColor3 = gEndEffectTexture.Sample(gClampSampler, fadingUV) * float4(0.0, 0.0, 0.0, 1.0);
            float alphaAmount = transitionTime * fadingColor3.a * dissolve3Fade;
        
        // �ܼ��� ����
            float blendStrength = saturate(alphaAmount);
            finalColor.rgb = lerp(finalColor.rgb, fadingColor3.rgb, blendStrength);
            finalColor.a = max(finalColor.a, alphaAmount);
        }
    }
    
    finalColor *= input.color;
    finalColor.a *= input.alpha;

    if (finalColor.a < 0.1)
    {
        discard;
    }
    
    output.color = finalColor;
    //output.color = float4(time, 0, 0, 1);

    return output;
}