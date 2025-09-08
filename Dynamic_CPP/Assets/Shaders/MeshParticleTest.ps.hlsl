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
    
    // 텍스처 UV 이동 적용
    //float2 offsetUV = uv + float2(time * 0.5, 0.0f);
    float2 offsetUV = uv;
    float2 offsetUV2 = uv;
    float2 offsetUV3 = uv;
    float moveSpeed = 1.0;
    offsetUV.x = uv.x * pow(time, 0.1);
    
    float2 dissolveUV1 = offsetUV;
    float2 dissolveUV2 = offsetUV;
    
    // 텍스처 샘플링
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

    // 기본 디졸브 효과
    float dissolveAlpha = lerp(1.0f, dissolveColor2.a, fadeAmount);

    // 시간이 지나면서 전체적으로 페이드아웃
    float trailFade = 1.0 - smoothstep(0.5, 1.0, time); // 60%부터 100%까지 서서히 사라짐

    // 두 효과를 곱해서 적용
    finalColor.a *= dissolveAlpha * trailFade;
    
    if (time > 0.4)
    {
        float delayedTime = max(0.0, time - 0.1); // 지연 시간 줄이기
        float transitionTime = (delayedTime - 0.3) / 0.6;
        transitionTime = saturate(transitionTime); // smoothstep 대신 saturate
        float dissolve3Fade = 1.0 - smoothstep(0.8, 1.4, delayedTime); // 더 늦게 시작
    
        if (dissolveColor3.a > 0.05) // 임계값 낮추기
        {
            float2 fadingUV = offsetUV3;
            fadingUV *= (1.0 + (1.0 - dissolve3Fade) * 2.0);
    
            float4 fadingColor3 = gEndEffectTexture.Sample(gClampSampler, fadingUV) * float4(0.0, 0.0, 0.0, 1.0);
            float alphaAmount = transitionTime * fadingColor3.a * dissolve3Fade;
        
        // 단순한 블렌딩
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