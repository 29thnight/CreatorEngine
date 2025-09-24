// SpriteAnimation.hlsl - ��������Ʈ �ִϸ��̼� �����ϴ� �ȼ� ���̴� 

// �ȼ� ���̴� �Է� ����ü
struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION; // ���� ���� ��ġ
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

// �ؽ�ó �� ���÷� ����
Texture2D gTexture : register(t0);
SamplerState linearSampler : register(s0);

// Sprite Animation Constant Buffer
cbuffer SpriteAnimationBuffer : register(b4)
{
    uint frameCount; // �� ������ ��
    float animationDuration;
    uint2 gridSize; // ��������Ʈ ��Ʈ ���� ũ�� (columns, rows)
};

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

float4 main(PixelInput input) : SV_TARGET
{
    // particleAge�� ������� ���� ������ ��� (���� �ð��� �ƴ� ��ƼŬ�� ���� ���)
    float normalizedTime = fmod(input.particleAge, animationDuration) / animationDuration;
    uint currentFrame = (uint) (normalizedTime * frameCount) % frameCount;
    
    // ��������Ʈ �ִϸ��̼��� ���� UV ��ǥ ���
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    uint frameX = currentFrame % gridSize.x;
    uint frameY = currentFrame / gridSize.x;
    
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 animatedUV = frameOffset + (input.texCoord * frameSize);
    
    // �⺻ �ؽ�ó ���� �������� (�ִϸ��̼ǵ� UV ���)
    float4 texColor = gTexture.Sample(linearSampler, animatedUV);

    float intensity = dot(texColor.rgb, float3(0.299, 0.587, 0.114)); // �ؽ�ó�� ��� ���
    float3 finalColor = input.color.rgb * intensity;
    float finalAlpha = texColor.a * input.alpha * input.color.a;
    
    // �ؽ�ó�� ���İ��� �ʹ� ������ �ȼ� ���
    clip(finalAlpha - 0.1);
    
    return float4(finalColor, finalAlpha);
}