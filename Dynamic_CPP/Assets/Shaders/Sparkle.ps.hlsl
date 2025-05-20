// Sparkle.hlsl - 반짝이는 이펙트를 위한 픽셀 셰이더

// 텍스처 및 샘플러 정의
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

// 픽셀 셰이더 입력 구조체
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
    // 기본 텍스처 색상 가져오기
    float4 texColor = sparkleTexture.Sample(linearSampler, input.TexCoord);
    float texAlpha = texColor.a;
    
    // 외곽선 두께 및 색상 설정
    float outlineThickness = 0.04f; // 외곽선 두께 조절 (값이 작을수록 얇은 선)
    float outlineIntensity = 2.0f; // 외곽선 밝기 조절
    float3 outlineColor = float3(1.0f, 1.0f, 1.0f); // 흰색 외곽선 (원하는 색상으로 변경 가능)
    
    // Quad의 가장자리 검출 (텍스처 좌표가 0 또는 1에 가까운지 확인)
    float2 border;
    border.x = min(input.TexCoord.x, 1.0f - input.TexCoord.x);
    border.y = min(input.TexCoord.y, 1.0f - input.TexCoord.y);
    float borderDist = min(border.x, border.y);
    
    // 외곽선 효과 계산 (0에 가까울수록 가장자리)
    float outlineEffect = 1.0f - smoothstep(0.0f, outlineThickness, borderDist);
    
    // 기본 컬러 (텍스처 색상 × 입력 색상)
    float4 baseColor = float4(input.Color.rgb, texAlpha * input.Color.a);
    
    // 알파값이 너무 낮으면 픽셀 폐기
    if (baseColor.a < 0.01f)
        discard;
    
    // 최종 색상 계산 (기본 색상과 외곽선 색상 혼합)
    float3 finalColor = lerp(baseColor.rgb, outlineColor, outlineEffect);
    
    return float4(finalColor, baseColor.a);
    //return float4(1, 0, 0, 1);
}