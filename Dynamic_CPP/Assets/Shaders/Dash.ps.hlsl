// TrailPixel.hlsl
Texture2D g_trailTexture : register(t0);
Texture2D g_dissolveTexture : register(t1);
Texture2D g_dissolveTexture2 : register(t2);
Texture2D g_backgroundTexture : register(t3);
SamplerState g_linearSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
    float depth : TEXCOORD3;
    float dissolveAmount : TEXCOORD4;
};

float4 main(PixelInput input) : SV_TARGET
{
    float lifeRatio = 1.0f - input.dissolveAmount;

    float4 texColor = g_trailTexture.Sample(g_linearSampler, input.texcoord);
    float4 bgColor = g_backgroundTexture.Sample(g_linearSampler, input.texcoord);

    float4 finalColor;

    if (texColor.a < 0.1f)
    {
        finalColor.rgb = bgColor.rgb;
        finalColor.a = bgColor.a * 0.5f;
    }
    else
    {
        float grayValue = (texColor.r + texColor.g + texColor.b) / 3.0f;
     
        if (grayValue < 0.3f)
        {
            finalColor.rgb = float3(0.0f, 0.0f, 0.0f);
        }
        else
        {
            finalColor.rgb = float3(1.0f, 1.0f, 1.0f);
        }
     
        finalColor.a = texColor.a;
     
       // Y=0에 가까운 부분만 빨간색 (input.color 사용)
        if (input.texcoord.y < 0.1f)
        {
            finalColor.rgb = float3(2.0f, 0.3f, 0.3f) * input.color.rgb;
        }
        // 잔상 (input.color 사용)
        else if (lifeRatio >= 0.3f)
        {
            float intensity = (0.3f - lifeRatio) / 0.3f;
            float3 deathColor = float3(1.2f, 0.1f, 0.1f) * input.color.rgb;
            finalColor.rgb = lerp(finalColor.rgb, deathColor, intensity * 0.8f);
        }
       
    }

    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);

    float fresnel = 1.0f - abs(dot(normal, viewDir));
    fresnel = smoothstep(0.0f, 1.0f, fresnel);
    finalColor.rgb *= (1.0f + fresnel * 0.3f);

    float distance = length(input.viewDir);
    float distanceFade = 1.0f - saturate((distance - 50.0f) / 100.0f);
    finalColor.a *= distanceFade;

    finalColor.a *= pow(lifeRatio, 0.5f);

    clip(finalColor.a - 0.01f);

    return finalColor;
}