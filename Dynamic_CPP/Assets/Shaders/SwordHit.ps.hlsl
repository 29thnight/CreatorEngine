// MeshParticlePS.hlsl - 3D 메시 파티클 픽셀 셰이더

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
    uint frameCount; // 총 프레임 수
    float animationDuration;
    uint2 gridSize; // 스프라이트 시트 격자 크기 (columns, rows)
};

Texture2D gDiffuseTexture : register(t0);
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

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

uint HashVector3(float3 v)
{
    // 소수점 좌표를 정수로 스케일링 (예: 1000배)
    int xi = (int) (v.x * 1000.0f);
    int yi = (int) (v.y * 1000.0f);
    int zi = (int) (v.z * 1000.0f);

    // 단순 해시 조합
    uint h = 2166136261u; // FNV-1a offset
    h = (h ^ xi) * 16777619u;
    h = (h ^ yi) * 16777619u;
    h = (h ^ zi) * 16777619u;
    return h;
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float randomValue = Hash(HashVector3(input.normal));
    uint randomFrame = (uint) (randomValue * frameCount);
    float2 frameSize = float2(1.0f / gridSize.x, 1.0f / gridSize.y);
    int frameX = randomFrame % gridSize.x;
    int frameY = randomFrame / gridSize.x;
    float2 frameOffset = float2(frameX * frameSize.x, frameY * frameSize.y);
    float2 uv = input.texCoord * 0.5; //frameOffset + (input.texCoord * frameSize);
    
    float4 diffuseColor = gDiffuseTexture.Sample(gPointSampler, uv);
    float mask = diffuseColor.r;
    float dissolve = diffuseColor.g;
    float dissolve2 = diffuseColor.b;
    float alpha = diffuseColor.a;
    
    float x = uv.x;
    float y = uv.y;
    float clampY = clamp(y, 0.f, 0.5f);
    float addY = clampY - 1.f;
    float subY = 1.f - clampY;
    float lerpY = lerp(addY, subY, x);
    float oneminus = 1.f - dissolve;
    float addlerp = lerpY + oneminus;
    
    
    float smoothY = smoothstep(clampY, subY, addlerp);
    float satu = saturate(alpha - smoothY);
    
    
    
    float t = input.particleAge / input.particleLifeTime;
    float powt = pow(t, 2);
    
    float3 c = lerp(input.color * mask, input.color * dissolve2, dissolve2);
    
    float a = input.alpha * satu * smoothstep(powt, powt + 0.2, dissolve);
    
    clip(a - 0.01);
    
    output.color = float4(c, a);
    
    //float3 finalColor;
    
    //finalColor = input.color.rgb * diffuseColor.rgb;
    
    //float finalAlpha = input.alpha * diffuseColor.a;
    //output.color = float4(1,1,1, satu);
    ////output.color = float4(finalColor, finalAlpha);
    
    return output;
}