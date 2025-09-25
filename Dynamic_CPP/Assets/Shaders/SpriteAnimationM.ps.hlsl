// SpriteAnimation.hlsl - 스프라이트 애니메이션 구현하는 픽셀 셰이더 
// 텍스처 및 샘플러 정의
Texture2D gTexture : register(t0);
SamplerState linearSampler : register(s0);

// Sprite Animation Constant Buffer
cbuffer SpriteAnimationBuffer : register(b0)
{
    uint frameCount; // 총 프레임 수
    float animationDuration;
    uint2 gridSize; // 스프라이트 시트 격자 크기 (columns, rows)
};

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
    // age를 기반으로 현재 프레임 계산
    float normalizedTime = fmod(input.Age, animationDuration) / animationDuration;
    uint currentFrame = (uint) (normalizedTime * frameCount) % frameCount;
    
    // 스프라이트 애니메이션을 위한 UV 좌표 계산
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (input.TexCoord * frameSize);
    
    // 마스크 텍스처에서 마스크값 가져오기
    float4 temp = gTexture.Sample(linearSampler, animatedUV);
    float maskValue = temp.r;
    float dissolve = temp.g;
    float emissive = temp.b;
    float alpha = temp.a;
    
    // 파티클 수명에 따른 디졸브 임계값 계산
    float dissolveThreshold = saturate(input.Age);
    
    // 디졸브 효과: 마스크값이 임계값보다 낮으면 픽셀 폐기
    if (dissolve < dissolveThreshold)
        discard;
    
    // 입력 색상을 기본색으로 사용하고 마스크로 강도 조절
    float3 baseColor = input.Color.rgb * maskValue;
    float3 emissiveColor = float3(1.4f, 0.7f, 0.37f);

    // 에미시브 값에 따라 기본 색상과 에미시브 색상을 블렌딩
    float3 finalColor = lerp(baseColor, baseColor + emissiveColor, pow(emissive, 3));
    
    // 최종 알파값 계산 (마스크값 × 입력 알파)
    float finalAlpha = input.Color.a * alpha * smoothstep(dissolveThreshold, dissolveThreshold + 0.2, dissolve);
    
    return float4(finalColor, finalAlpha);
}