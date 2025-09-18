// MeshSizeModule.hlsl
// 파티클 크기 변화를 처리하는 컴퓨트 셰이더

// 파티클 데이터 구조체 (다른 모듈과 일치해야 함)
struct MeshParticleData
{
    float3 position;
    float pad1;
    
    float3 velocity;
    float pad2;
    
    float3 acceleration;
    float pad3;
    
    float3 rotation;
    float pad4;
    
    float3 rotationSpeed;
    float pad5;
    
    float3 scale;
    float pad6;
    
    float age;
    float lifeTime;
    uint isActive;
    uint renderMode;
    
    float4 color;
    
    uint textureIndex;
    float3 pad8;
};

// Size 파라미터
cbuffer SizeParams : register(b0)
{
    float3 startSize;
    float pad1;
    float3 endSize;
    float deltaTime;
    
    int useRandomScale;
    float randomScaleMin;
    float randomScaleMax;
    uint maxParticles;
    
    float3 emitterScale;
    float pad2;
};

// 입출력 버퍼
StructuredBuffer<MeshParticleData> inputParticles : register(t0);
RWStructuredBuffer<MeshParticleData> outputParticles : register(u0);

#define THREAD_GROUP_SIZE 1024

// 개선된 해시 함수
uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float RandomFloat(uint seed)
{
    return float(WangHash(seed)) * (1.0f / 4294967296.0f);
}

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    
    if (particleIndex >= maxParticles)
        return;
    
    MeshParticleData particle = inputParticles[particleIndex];
    
    // 비활성 파티클은 건너뛰기
    if (particle.isActive == 0)
    {
        outputParticles[particleIndex] = particle;
        return;
    }
    
    // 생명 주기 비율 계산
    float lifeRatio = saturate(particle.age / particle.lifeTime);
    
    // 크기 보간 (생명주기에 따른 크기 변화)
    float3 currentSize = lerp(startSize, endSize, lifeRatio);
    
    // 랜덤 스케일 적용
    if (useRandomScale != 0)
    {
        float randomScale = lerp(randomScaleMin, randomScaleMax,
                               RandomFloat(particleIndex + 12345u));
        currentSize *= randomScale;
    }
    
    // 이미터 전체 스케일 적용 (최종 크기 = 동적크기 * 이미터스케일)
    currentSize *= emitterScale;
    
    particle.scale = currentSize;
    outputParticles[particleIndex] = particle;
}