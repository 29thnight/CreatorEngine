// MeshParticlePS.hlsl - 3D �޽� ��ƼŬ �ȼ� ���̴�
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

struct PixelOutput
{
    float4 color : SV_Target;
};

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

cbuffer SpriteAnimationBuffer : register(b4)
{
    uint frameCount; // �� ������ ��
    float animationDuration;
    uint2 gridSize; // ��������Ʈ ��Ʈ ���� ũ�� (columns, rows)
};

Texture2D gNoiseTexture : register(t0);
Texture2D gDissolveTexture : register(t1);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);
#define pi 3.1415926

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    float normalizedAge = input.particleAge / input.particleLifeTime;
    
    float2 uv = input.texCoord;
    float dissolve = gDissolveTexture.Sample(gLinearSampler, input.texCoord);
    float d = smoothstep(0, 0.5, dissolve);
    
    float smoothDissolveDownUp = 1 - smoothstep(0.7, 1.0, normalizedAge);
    float smooth = smoothstep(normalizedAge - 0.2, normalizedAge, smoothDissolveDownUp);
    
    
    float2 animatedUV = input.texCoord.yx * float2(gridSize.x, gridSize.y);
    animatedUV += gTime * animationDuration;
    animatedUV.x -= normalizedAge * 2;
    float4 diffuseColor = gNoiseTexture.Sample(gLinearSampler, animatedUV);
    float s = (1 - smoothstep(0.7, 1, uv.y));
    
    float3 finalColor = input.color.rgb * diffuseColor.rgb * d;
    
    float finalAlpha = input.alpha * diffuseColor.a * s * d; /** smooth * s * d*/;

    clip(finalAlpha - 0.1);
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}