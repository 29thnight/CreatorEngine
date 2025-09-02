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
    float dissolve1 = g_dissolveTexture.Sample(g_linearSampler, input.texcoord).r;
    float dissolve2 = g_dissolveTexture2.Sample(g_linearSampler, input.texcoord).r;
    
    float lifeRatio = 1.0f - input.dissolveAmount;
    
    // 부드러운 점진적 사라짐 (threshold를 더 부드럽게)
    float dissolveThreshold = 1.0f - lifeRatio * 0.8f; // 0.8f로 범위 조정
    clip(dissolve2 - dissolveThreshold);
    
    float4 texColor = g_trailTexture.Sample(g_linearSampler, input.texcoord);
    float4 bgColor = g_backgroundTexture.Sample(g_linearSampler, input.texcoord);
    
    // 하이라이트도 더 부드럽게
    float deathEdge = smoothstep(lifeRatio - 0.15f, lifeRatio + 0.05f, dissolve1);
    float3 highlightColor = float3(1.0f, 0.3f, 0.1f);
    
    float4 finalColor = input.color * texColor;
    
    // 백그라운드 블렌딩도 더 점진적으로
    float dissolveEdge = smoothstep(dissolveThreshold - 0.1f, dissolveThreshold + 0.2f, dissolve2);
    finalColor = lerp(finalColor, bgColor, (1.0f - dissolveEdge) * 0.2f);
    
    finalColor.rgb = lerp(finalColor.rgb, highlightColor, deathEdge * lifeRatio * 0.5f);
    
    // 나머지 효과들...
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    float fresnel = 1.0f - abs(dot(normal, viewDir));
    fresnel = smoothstep(0.0f, 1.0f, fresnel);
    finalColor.rgb *= (1.0f + fresnel * 0.3f);
    
    float vFade = smoothstep(0.0f, 0.1f, input.texcoord.y) *
                  smoothstep(1.0f, 0.9f, input.texcoord.y);
    finalColor.a *= vFade;
    
    float distance = length(input.viewDir);
    float distanceFade = 1.0f - saturate((distance - 50.0f) / 100.0f);
    finalColor.a *= distanceFade;
    
    finalColor.a *= pow(lifeRatio, 0.5f); // 제곱근으로 더 부드러운 페이드
    
    clip(finalColor.a - 0.01f);
    
    return finalColor;
}