// TrailPixel.hlsl - 트레일 픽셀 셰이더

// 텍스처와 샘플러
Texture2D g_trailTexture : register(t0);
SamplerState g_linearSampler : register(s0);

// 입력 구조체 (정점 셰이더 출력과 일치)
struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
};

// 출력 구조체
struct PixelOutput
{
    float4 color : SV_TARGET;
};

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // 텍스처 샘플링
    float4 texColor = g_trailTexture.Sample(g_linearSampler, input.texcoord);
    
    // 기본 색상과 텍스처 결합
    float4 baseColor = input.color * texColor;
    
    // 간단한 라이팅 계산 (옵션)
    float3 normalizedNormal = normalize(input.normal);
    float3 normalizedViewDir = normalize(input.viewDir);
    
    // 프레넬 효과 (가장자리에서 더 밝게)
    float fresnel = 1.0f - saturate(dot(normalizedNormal, normalizedViewDir));
    fresnel = pow(fresnel, 2.0f);
    
    // 가장자리 강조
    float edgeIntensity = 1.0f + fresnel * 0.5f;
    baseColor.rgb *= edgeIntensity;
    
    // 거리에 따른 페이드 아웃 (옵션)
    // 여기서는 생략하지만 필요시 추가 가능
    
    // 최종 색상 출력
    output.color = baseColor;
    
    // 알파가 너무 작으면 픽셀 버리기
    if (output.color.a < 0.01f)
        discard;
    
    return output;
}