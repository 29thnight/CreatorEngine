// Sparkle.hlsl - 반짝이는 이펙트를 위한 픽셀 셰이더

// 상수 버퍼 정의
cbuffer SparkleParametersBuffer : register(b3)
{
    float time; // 현재 시간
    float intensity; // 반짝임 강도
    float speed; // 반짝임 속도
    float padding; // 16바이트 정렬을 위한 패딩
    float4 color; // 기본 색상
    float2 size; // 크기
    float2 range; // 효과 범위
}

// 텍스처 및 샘플러 정의
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

// 픽셀 셰이더 입력 구조체
struct GSOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    //float depth : TEXCOORD1;
    //float distToCamera : TEXCOORD2;
    float4 color : COLOR0;
};

// 메인 픽셀 셰이더 함수
float4 main(GSOutput input): SV_TARGET
{
    
    // 기본 텍스처 색상 가져오기
    float4 texColor = sparkleTexture.Sample(linearSampler, input.texCoord);

    // 시간에 따른 반짝임 효과 계산
    float sparkle = 0.7f + 0.3f * sin((time * speed) * 10.0f);
    
    // 중심에서 가장자리로 갈수록 투명해지는 거리 계산
    float2 center = float2(0.5f, 0.5f);
    float dist = length(input.texCoord - center) * 2.0f; // 0~1 범위로 정규화
    float edgeFade = 1.0f - saturate(dist);
    
    // 두 가지 다른 시간 함수로 반짝임에 변화 추가
    float sparkle2 = 0.8f + 0.2f * sin((time * speed * 0.7f) * 12.0f);
    
    // 최종 색상 계산
    float4 baseColor = texColor * color; // 시스템 색상과 파티클 색상 결합
    
    // 반짝임 효과를 위한 추가 밝기 계산
    float baseValue = 1.0f;
    float sparkleEffect = (sparkle * sparkle2 - 0.7f) * intensity; // 0.7은 대략적인 평균값
    float brightness = baseValue + max(0.0f, sparkleEffect);
    
    float3 finalColor = baseColor.rgb * brightness;
    
    if (texColor.a < 0.01f)
        discard;
    
    // 가장 밝은 부분에 흰색 반짝임 추가
    finalColor = lerp(finalColor, float3(1.0f, 1.0f, 1.0f), pow(sparkle * sparkle2 - 0.5f, 2) * 0.5f);

    return float4(finalColor, texColor.a);
    //return input.color;
}