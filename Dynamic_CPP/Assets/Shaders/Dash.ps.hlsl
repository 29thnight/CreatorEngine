// TrailPixel.hlsl
Texture2D g_trailTexture : register(t0);
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
};

float4 main(PixelInput input) : SV_TARGET
{
    // 텍스처 샘플링
    float4 texColor = g_trailTexture.Sample(g_linearSampler, input.texcoord);
    
    // 기본 색상
    float4 finalColor = input.color * texColor;
    
    // 정규화 (VS에서 이미 계산된 viewDir 사용)
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    // 프레넬 효과 (부드럽게)
    float fresnel = 1.0f - abs(dot(normal, viewDir));
    fresnel = smoothstep(0.0f, 1.0f, fresnel);
    
    // 가장자리 강조
    finalColor.rgb *= (1.0f + fresnel * 0.3f);
    
    // V 좌표 기반 페이드 (트레일 가장자리)
    float vFade = smoothstep(0.0f, 0.1f, input.texcoord.y) *
                  smoothstep(1.0f, 0.9f, input.texcoord.y);
    finalColor.a *= vFade;
    
    // 거리 기반 페이드 (viewDir 길이 사용)
    float distance = length(input.viewDir);
    float distanceFade = 1.0f - saturate((distance - 50.0f) / 100.0f);
    finalColor.a *= distanceFade;
    
    // 알파 테스트
    clip(finalColor.a - 0.01f);
    
    return finalColor;
}