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
    uint currentFrame = lerp(46, 54, normalizedTime);
    
    float2 uv = input.TexCoord;
    
    //float dissolveX = uv.x; // 0 ~ 1; 0일 때 y dissolve
    
    
    //uv.x -= normalizedTime * 2.f;
    //uv.x = saturate(uv.x);
    
    
    
    // 스프라이트 애니메이션을 위한 UV 좌표 계산
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (uv * frameSize);
    
    // 기본 텍스처 색상 가져오기 (애니메이션된 UV 사용)
    float4 texColor = gTexture.Sample(linearSampler, animatedUV);
    
    // 텍스처의 알파값이 너무 낮으면 픽셀 폐기
    if (texColor.a < 0.01f)
        discard;
    
    // 원본 텍스처 색상을 기본으로 사용
    float3 baseColor = texColor.rgb;
    
    // 입력 색상을 additive 방식으로 더하기 (밝기 증가)
    float3 coloredTexture = baseColor * input.Color.rgb;
    
    float3 finalColor = coloredTexture;
    
    // 최종 알파값 계산 (원본 텍스처 알파 × 입력 알파)
    float finalAlpha = texColor.a * input.Color.a;
    
    return float4(finalColor, finalAlpha);
}