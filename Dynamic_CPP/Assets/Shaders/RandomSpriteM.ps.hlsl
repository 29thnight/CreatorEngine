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

// Hash 함수 (랜덤값 생성용)
float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) * (1.0f / 4294967296.0f);
}

float4 main(VSOutput input) : SV_TARGET
{
    
    float randomValue = Hash(input.TexIndex);
    
    uint randomFrame = (uint) (randomValue * frameCount);
    
    // age를 기반으로 현재 프레임 계산
    float normalizedTime = fmod(input.Age, animationDuration) / animationDuration;
    
    // 스프라이트 애니메이션을 위한 UV 좌표 계산
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = randomFrame % gridSize.x;
    uint frameY = randomFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (input.TexCoord * frameSize);
    
    // 마스크 텍스처에서 마스크값 가져오기
    float4 temp = gTexture.Sample(linearSampler, animatedUV);
    float maskValue = temp.r;
    float alpha = temp.a;
    
    // 마스크값이 너무 낮으면 픽셀 폐기
    if (maskValue < 0.01f)
        discard;
    
    float3 baseColor = input.Color.rgb * maskValue;

    float3 finalColor = baseColor;

    float finalAlpha = input.Color.a * alpha;
    
    return float4(finalColor, finalAlpha);
}