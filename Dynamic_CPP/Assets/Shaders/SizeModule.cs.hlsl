// SizeModule.hlsl
// 파티클 크기 변화를 처리하는 컴퓨트 셰이더

// 파티클 데이터 구조체 (다른 모듈과 일치해야 함)
struct ParticleData
{
    float3 position;
    float pad1;
    float3 velocity;
    float pad2;
    float3 acceleration;
    float pad3;
    float2 size;
    float age;
    float lifeTime;
    float rotation;
    float rotatespeed;
    float2 pad4;
    float4 color;
    uint isActive;
    float3 pad5;
};

// Size 파라미터
cbuffer SizeParams : register(b0)
{
    float2 startSize; // 시작 크기
    float2 endSize; // 끝 크기
    float deltaTime; // 델타 시간 (이징 적용됨)
    int useRandomScale; // 랜덤 스케일 사용 여부
    float randomScaleMin; // 랜덤 스케일 최소값
    float randomScaleMax; // 랜덤 스케일 최대값
    uint maxParticles; // 최대 파티클 수
    float pad1; // 패딩
    float3 emitterScale;
    float pad2;
};

// 입출력 버퍼
StructuredBuffer<ParticleData> inputParticles : register(t0);
RWStructuredBuffer<ParticleData> outputParticles : register(u0);

// 상수
#define THREAD_GROUP_SIZE 1024

// 간단한 해시 함수 (랜덤값 생성용)
float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) * (1.0f / 4294967296.0f);
}

// 메인 컴퓨트 셰이더
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    uint numParticles, stride;
    inputParticles.GetDimensions(numParticles, stride);
    
    // 범위 검사
    if (particleIndex >= numParticles)
        return;
    
    // 입력 파티클 데이터 가져오기
    ParticleData particle = inputParticles[particleIndex];
    
    // 죽은 파티클은 처리하지 않음
    if (particle.age >= particle.lifeTime || particle.lifeTime <= 0.0f)
    {
        outputParticles[particleIndex] = particle;
        return;
    }
    
    // 생명 주기 비율 계산 (0.0 ~ 1.0)
    float lifeRatio = saturate(particle.age / particle.lifeTime);
    
    // 크기 보간 (이미 CPU에서 이징이 적용된 상태)
    float2 currentSize = lerp(startSize, endSize, lifeRatio);
    
    // 랜덤 스케일 적용 (파티클별로 고유한 시드 사용)
    if (useRandomScale)
    {
        // 파티클 인덱스를 시드로 사용하여 일관된 랜덤값 생성
        float randomScale = lerp(randomScaleMin, randomScaleMax, Hash(particleIndex + 1));
        currentSize *= randomScale;
    }
    
    currentSize.x *= emitterScale.x;
    currentSize.y *= emitterScale.y;
    
    // 결과 적용
    particle.size = currentSize;
    
    // 출력 버퍼에 저장
    outputParticles[particleIndex] = particle;
}