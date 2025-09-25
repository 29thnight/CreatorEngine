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
    
    // ����ũ �ؽ�ó���� ����ũ�� ��������
    float4 temp = gTexture.Sample(linearSampler, animatedUV);
    float maskValue = temp.r;
    float dissolve = temp.g;
    float emissive = temp.b;
    float alpha = temp.a;
    
     // ��ƼŬ ���� ���� ������ �Ӱ谪 ���
    float dissolveThreshold = saturate(input.particleAge);
    
    // ������ ȿ��: ����ũ���� �Ӱ谪���� ������ �ȼ� ���
    if (dissolve < dissolveThreshold)
        discard;
    
    // �Է� ������ �⺻������ ����ϰ� ����ũ�� ���� ����
    float3 baseColor = input.color.rgb * maskValue;
    float3 emissiveColor = float3(1.4f, 0.7f, 0.37f);

    // ���̽ú� ���� ���� �⺻ ����� ���̽ú� ������ ����
    float3 finalColor = lerp(baseColor, baseColor + emissiveColor, pow(emissive, 3));
    
    // ���� ���İ� ��� (����ũ�� �� �Է� ����)
    float finalAlpha = input.color.a * alpha * smoothstep(dissolveThreshold, dissolveThreshold + 0.2, dissolve);
    
    
    return float4(finalColor, finalAlpha);
}
