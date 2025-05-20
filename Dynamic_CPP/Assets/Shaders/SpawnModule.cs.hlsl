// SpawnModule.hlsl
// 파티클의 생성에 관한 컴퓨트 셰이더

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


cbuffer SpawnParams : register(b0)
{
    float gSpawnRate;
    float gDeltaTime;
    int gEmitterType;
    float pad;
    float3 gEmitterSize;
    float gEmitterRadius;
    uint gMaxParticles;
    float3 gPad;
}


cbuffer ParticleTemplateParams : register(b1)
{
    float gLifeTime;
    float gRotateSpeed;
    float2 gSize;
    float4 gColor;
    float3 gVelocity;
    float gPad1;
    float3 gAcceleration; // 초기 가속도 
    float gPad2; // 패딩
    float gMinVerticalVelocity; // 최소 수직 속도
    float gMaxVerticalVelocity; // 최대 수직 속도
    float gHorizontalVelocityRange; // 수평 속도 범위
    float gPad3; // 패딩
}

// 파티클 버퍼 (register u0에 바인딩)
RWStructuredBuffer<ParticleData> gParticles : register(u0);
// 랜덤 카운터 버퍼 (register u1에 바인딩)
RWStructuredBuffer<uint> gRandomCounter : register(u1);
RWStructuredBuffer<float> gTimeBuffer : register(u2);
// 새로 추가: 스폰 카운터와 필요한 파티클 수를 위한 버퍼
RWStructuredBuffer<uint> gSpawnCounter : register(u3);

RWStructuredBuffer<uint> gInactiveParticleIndices : register(u4);
RWStructuredBuffer<uint> gInactiveParticleCount : register(u5);
// 난수 생성 함수
uint wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float rand(inout uint state)
{
    state = wang_hash(state);
    return float(state) / 4294967296.0; // 2^32
}

// 파티클 초기화 함수
// 파티클 초기화 함수 개선
void InitializeParticle(inout ParticleData particle, inout uint seed)
{
    // 모든 필드를 먼저 0으로 초기화
    particle.position = float3(0, 0, 0);
    particle.pad1 = 0.0f;
    particle.velocity = float3(0, 0, 0);
    particle.pad2 = 0.0f;
    particle.acceleration = float3(0, 0, 0);
    particle.pad3 = 0.0f;
    particle.size = float2(0, 0);
    particle.age = 0.0f;
    particle.lifeTime = 0.0f;
    particle.rotation = 0.0f;
    particle.rotatespeed = 0.0f;
    particle.pad4 = float2(0, 0);
    particle.color = float4(0, 0, 0, 1);
    particle.isActive = 0;
    particle.pad5 = float3(0, 0, 0);
    
    // 이제 실제 값으로 설정
    particle.position = float3(0, 0, 0); // 기본값, 이후 이미터 타입에 따라 변경됨
    particle.velocity = gVelocity;
    particle.acceleration = gAcceleration;
    particle.size = gSize;
    particle.age = 0.0f;
    particle.lifeTime = 15.0f;
    particle.rotation = 0.0f;
    particle.rotatespeed = gRotateSpeed;
    particle.color = gColor;
    particle.isActive = 1;
    
    // 이미터 타입에 따른 위치 설정
    switch (gEmitterType)
    {
        case 0: // point
            particle.position = float3(0.0f, 0.0f, 0.0f);
            break;
            
        case 1: // sphere
        {
                float theta = rand(seed) * 6.28318f; // 0 ~ 2π (방위각)
                float phi = rand(seed) * 3.14159f; // 0 ~ π (고도각)
                float radius = gEmitterRadius * pow(rand(seed), 1 / 3.0); // 균등 분포를 위한 r 계산
            
                particle.position = float3(
                radius * sin(phi) * cos(theta),
                radius * sin(phi) * sin(theta),
                radius * cos(phi)
            );
                break;
            }
        
        case 2: // box
        {
                particle.position = float3(
                (rand(seed) - 0.5f) * 2.0f * gEmitterSize.x,
                (rand(seed) - 0.5f) * 2.0f * gEmitterSize.y,
                (rand(seed) - 0.5f) * 2.0f * gEmitterSize.z
            );
                break;
            }
        
        case 3: // cone
        {
                float theta = rand(seed) * 6.28318f;
                float height = rand(seed) * gEmitterSize.y;
                float radiusAtHeight = gEmitterRadius * (1.0f - height / gEmitterSize.y);
            
                particle.position = float3(
                radiusAtHeight * cos(theta),
                height,
                radiusAtHeight * sin(theta)
            );
                break;
            }
        
        case 4: // circle
        {
                float theta = rand(seed) * 6.28318f;
                float radius = sqrt(rand(seed)) * gEmitterRadius; // 균등 분포를 위한 제곱근
            
                particle.position = float3(
                radius * cos(theta),
                0.0f,
                radius * sin(theta)
            );
                break;
            }
    }
    
    // 초기 속도에 랜덤 변동 추가
    float3 baseVelocity = gVelocity;
    float3 randomVelocity = float3(0, 0, 0);

    if (gHorizontalVelocityRange > 0.0f || gMaxVerticalVelocity > gMinVerticalVelocity)
    {
        float verticalVelocity = lerp(gMinVerticalVelocity, gMaxVerticalVelocity, rand(seed));
        float angle = rand(seed) * 6.28318f;
        float magnitude = rand(seed) * gHorizontalVelocityRange;
    
        randomVelocity = float3(
            magnitude * cos(angle),
            verticalVelocity,
            magnitude * sin(angle)
        );
    }
    
    particle.velocity = baseVelocity + randomVelocity;
    particle.acceleration = gAcceleration;
}

// 스레드 그룹 크기 정의
#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 첫 번째 스레드만 이번 프레임에 생성할 파티클 수 계산
    if (DTid.x == 0)
    {
        // 비활성 카운터 값 확인
        uint inactiveCount = gInactiveParticleCount[0];
        
        // 생성할 파티클 수 계산 
        float particlesPerSecond = gSpawnRate;
        float particlesThisFrame = particlesPerSecond * gDeltaTime;
        
        // 누적 소수점 처리
        float fractionalPart = frac(gTimeBuffer[0]);
        float totalParticles = particlesThisFrame + fractionalPart;
        
        // 생성할 파티클 수 (정수부)
        uint particlesToSpawn = min(uint(totalParticles), inactiveCount);
        
        // 남은 소수점 부분은 다음 프레임으로
        float remainingFraction = frac(totalParticles);
        
        // 생성할 파티클 수 저장
        gSpawnCounter[0] = particlesToSpawn;
        
        // 남은 소수점 저장
        gTimeBuffer[0] = remainingFraction;
    }
    
    // 그룹 동기화
    GroupMemoryBarrierWithGroupSync();
    
    // 각 스레드는 전역 인덱스를 사용하여 파티클 처리
    uint globalIndex = DTid.x;
    uint spawnCount = gSpawnCounter[0];
    uint inactiveCount = gInactiveParticleCount[0];
    
    // 생성할 파티클 수보다 작고, 비활성 파티클이 충분히 있는 경우
    if (globalIndex < spawnCount && globalIndex < inactiveCount)
    {
        // 비활성 인덱스에서 파티클 슬롯 가져오기
        uint particleIndex = gInactiveParticleIndices[globalIndex];
        
        // 유효한 인덱스인지 확인
        if (particleIndex < gMaxParticles)
        {
            // 난수 시드 생성
            uint seed = wang_hash(particleIndex + DTid.x + gRandomCounter[0]);
            InterlockedAdd(gRandomCounter[0], 1);
            
            // 파티클 초기화
            ParticleData particle = (ParticleData) 0;
            InitializeParticle(particle, seed);
            
            // 중요: 파티클 데이터를 GPU 버퍼에 저장
            gParticles[particleIndex] = particle;
        }
    }
}