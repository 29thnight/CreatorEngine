// SpriteAnimation.hlsl - 스프라이트 애니메이션 구현하는 픽셀 셰이더 

// 픽셀 셰이더 입력 구조체
struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION; // 원본 로컬 위치
    float3 particleScale : PARTICLE_SCALE;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 viewDir : VIEW_DIR;
    float alpha : ALPHA;
    uint renderMode : RENDER_MODE;
    float particleAge : PARTICLE_AGE;
    float particleLifeTime : PARTICLE_LIFETIME;

};

// 텍스처 및 샘플러 정의
Texture2D gTexture : register(t0);
SamplerState linearSampler : register(s0);

// Sprite Animation Constant Buffer
cbuffer SpriteAnimationBuffer : register(b4)
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

float4 main(PixelInput input) : SV_TARGET
{
    // particleAge를 기반으로 현재 프레임 계산 (전역 시간이 아닌 파티클별 나이 사용)
    float normalizedTime = fmod(input.particleAge, animationDuration) / animationDuration;
    uint currentFrame = (uint) (normalizedTime * frameCount) % frameCount;
    
    // 스프라이트 애니메이션을 위한 UV 좌표 계산
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (input.texCoord * frameSize);
    
    // 기본 텍스처 색상 가져오기 (애니메이션된 UV 사용)
    float4 texColor = gTexture.Sample(linearSampler, animatedUV);

    float intensity = dot(texColor.rgb, float3(0.299, 0.587, 0.114)); // 텍스처의 밝기 계산
    float3 finalColor = input.color.rgb * intensity;
    float finalAlpha = texColor.a * input.alpha * input.color.a;
    
    // 텍스처의 알파값이 너무 낮으면 픽셀 폐기
    clip(finalAlpha - 0.1);
    
    return float4(finalColor, finalAlpha);
}