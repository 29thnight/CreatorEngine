// Sparkle.hlsl - 반짝이는 이펙트를 위한 픽셀 셰이더

// 텍스처 및 샘플러 정의
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

// 픽셀 셰이더 입력 구조체
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
    float Age : TEXCOORD2;
};

float4 main(VSOutput input) : SV_TARGET
{
    // 기본 텍스처 색상 가져오기
    float4 texColor = sparkleTexture.Sample(linearSampler, input.TexCoord);
    
    // 텍스처의 알파값이 너무 낮으면 픽셀 폐기 (원본 형태 유지)
    if (texColor.a < 0.01f)
        discard;
    
    // 외곽선 두께 및 색상 설정
    //float outlineThickness = 0.04f;
    //float outlineIntensity = 2.0f;
    //float3 outlineColor = float3(1.0f, 1.0f, 1.0f);
    
    // Quad의 가장자리 검출
    //float2 border;
    //border.x = min(input.TexCoord.x, 1.0f - input.TexCoord.x);
    //border.y = min(input.TexCoord.y, 1.0f - input.TexCoord.y);
    //float borderDist = min(border.x, border.y);
    
    // 외곽선 효과 계산
    //float outlineEffect = 1.0f - smoothstep(0.0f, outlineThickness, borderDist);
    
    // 원본 텍스처 색상을 기본으로 사용
    float3 baseColor = texColor.rgb;
    
    // 입력 색상을 additive 방식으로 더하기 (밝기 증가)
    // 또는 multiply 방식으로 색조 변경
    //float3 coloredTexture = baseColor * input.Color.rgb; // multiply 방식
    float3 coloredTexture = baseColor + (input.Color.rgb * 0.5f); // additive 방식
    
    // 외곽선과 기본 색상 혼합
    float3 finalColor = coloredTexture; //, outlineColor * outlineIntensity, outlineEffect);
    
    // 최종 알파값 계산 (원본 텍스처 알파 × 입력 알파)
    float finalAlpha = texColor.a * input.Color.a;
    
    return float4(finalColor, finalAlpha);
}