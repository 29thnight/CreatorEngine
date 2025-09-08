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
    offsetUV.x *= time; // + float2(time * 0.5, 0.0f);
    offsetUV2.x *= time;
    
    offsetUV3.x *= 0.7;
    offsetUV3.y *= 0.4;
    offsetUV3.x += time * 0.3 - 0.15;
    
    float2 dissolveUV1 = offsetUV;
    float2 dissolveUV2 = offsetUV;
    
    // 텍스처 샘플링
    float4 baseTexture = gDiffuseTexture.Sample(gClampSampler, time < 0.5 ? offsetUV : offsetUV2) * float4(1.0f, 0.0f, 0.0f, 1.0f);
    float4 dissolveColor1 = gDissolveTexture1.Sample(gClampSampler, time < 0.5 ? offsetUV : offsetUV2) * float4(3.0f, 3.0f, 3.0f, 1.0f);
    float4 dissolveColor2 = gDissolveTexture2.Sample(gClampSampler, time < 0.5 ? offsetUV : offsetUV2);
    float4 dissolveColor3 = gEndEffectTexture.Sample(gClampSampler, offsetUV3) * float4(0.0, 0.0, 0.0, 1.0);
    float4 finalColor = float4(0.0f, 0.0f, 0.0f, 1.0f);

    int debug = 4;

    switch (debug)
    {
        case 0:
            finalColor.rgb = lerp(baseTexture.rgb, dissolveColor1.rgb, dissolveColor1.a);
            finalColor.a = baseTexture.a;
            break;
        case 1:
            finalColor.a *= dissolveColor2.a;
            break;
        case 2:
            if (baseTexture.a < 1)
            {
                finalColor = dissolveColor3;
            }
            break;
        default:
            finalColor.rgb = lerp(baseTexture.rgb, dissolveColor1.rgb, dissolveColor1.a);
            finalColor.a = baseTexture.a;
        
            finalColor.a *= dissolveColor2.a;
    
            if (baseTexture.a < 1)
            {
                finalColor = dissolveColor3;
            }
            break;
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