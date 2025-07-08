// SpawnModule.hlsl - 파티클 스폰 컴퓨트 셰이더

// 파티클 데이터 구조체
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
    float rotateSpeed;
    float2 pad4;
    float4 color;
    uint isActive;
    float3 pad5;
};

// 스폰 파라미터
cbuffer SpawnParameters : register(b0)
{
    float gSpawnRate; // 초당 생성할 파티클 수
    float gDeltaTime; // 프레임 시간
    float gCurrentTime; // 누적된 총 시간
    int gEmitterType; // 이미터 타입 (0:Point, 1:Sphere, 2:Box, 3:Cone, 4:Circle)
    
    float3 gEmitterSize; // 박스/콘 이미터 크기
    float gEmitterRadius; // 구/원/콘 이미터 반지름
    
    uint gMaxParticles; // 최대 파티클 수
    float3 gEmitterPosition; // 이미터 월드 위치
}

// 파티클 템플릿
cbuffer ParticleTemplate : register(b1)
{
    float gLifeTime; // 파티클 수명
    float gRotateSpeed; // 회전 속도
    float2 gSize; // 파티클 크기
    
    float4 gColor; // 파티클 색상
    
    float3 gVelocity; // 기본 속도
    float gMinVerticalVelocity;
    
    float3 gAcceleration; // 가속도
    float gMaxVerticalVelocity;
    
    float gHorizontalVelocityRange; // 수평 속도 범위
    float3 pad;
}

// 버퍼 바인딩
StructuredBuffer<ParticleData> gParticlesInput : register(t0);
RWStructuredBuffer<ParticleData> gParticlesOutput : register(u0);
RWStructuredBuffer<uint> gRandomSeed : register(u1);

// Wang Hash 기반 고품질 난수 생성
uint WangHash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float RandomFloat01(uint seed)
{
    return float(WangHash(seed)) / 4294967295.0;
}

float RandomRange(uint seed, float minVal, float maxVal)
{
    return lerp(minVal, maxVal, RandomFloat01(seed));
}

// 이미터별 위치 생성
float3 GenerateEmitterPosition(uint seed)
{
    float3 localPos = float3(0, 0, 0);
    
    switch (gEmitterType)
    {
        case 0: // Point Emitter
            localPos = float3(0, 0, 0);
            break;
            
        case 1: // Sphere Emitter
        {
                float theta = RandomFloat01(seed) * 6.28318530718;
                float phi = RandomFloat01(seed + 1) * 3.14159265359;
                float r = gEmitterRadius * pow(RandomFloat01(seed + 2), 0.33333);
        
                localPos = float3(
                r * sin(phi) * cos(theta),
                r * sin(phi) * sin(theta),
                r * cos(phi)
            );
                break;
            }
    
        case 2: // Box Emitter
        {
                localPos = float3(
                RandomRange(seed, -gEmitterSize.x * 0.5, gEmitterSize.x * 0.5),
                RandomRange(seed + 1, -gEmitterSize.y * 0.5, gEmitterSize.y * 0.5),
                RandomRange(seed + 2, -gEmitterSize.z * 0.5, gEmitterSize.z * 0.5)
            );
                break;
            }
    
        case 3: // Cone Emitter
        {
                float height = RandomFloat01(seed) * gEmitterSize.y;
                float angle = RandomFloat01(seed + 1) * 6.28318530718;
                float radiusAtHeight = gEmitterRadius * (1.0 - height / gEmitterSize.y);
                float r = sqrt(RandomFloat01(seed + 2)) * radiusAtHeight;
        
                localPos = float3(
                r * cos(angle),
                height,
                r * sin(angle)
            );
                break;
            }
    
        case 4: // Circle Emitter
        {
                float angle = RandomFloat01(seed) * 6.28318530718;
                float r = sqrt(RandomFloat01(seed + 1)) * gEmitterRadius;
        
                localPos = float3(
                r * cos(angle),
                0.0,
                r * sin(angle)
            );
                break;
            }
    
        default:
            localPos = float3(0, 0, 0);
            break;
    }
    
    // 로컬 위치에 이미터 월드 위치 더하기
    return localPos + gEmitterPosition;
}

// 초기 속도 생성
float3 GenerateInitialVelocity(uint seed)
{
    float3 velocity = gVelocity;
    
    // 랜덤 속도 추가
    if (gHorizontalVelocityRange > 0.0 || gMaxVerticalVelocity != gMinVerticalVelocity)
    {
        float verticalVel = RandomRange(seed, gMinVerticalVelocity, gMaxVerticalVelocity);
        float horizontalAngle = RandomFloat01(seed + 1) * 6.28318530718;
        float horizontalMag = RandomFloat01(seed + 2) * gHorizontalVelocityRange;
        
        velocity += float3(
            horizontalMag * cos(horizontalAngle),
            verticalVel,
            horizontalMag * sin(horizontalAngle)
        );
    }
    
    return velocity;
}

// 파티클 초기화 (완전한 버전)
void InitializeParticle(inout ParticleData particle, uint seed)
{
    particle.position = GenerateEmitterPosition(seed);
    particle.velocity = GenerateInitialVelocity(seed + 100);
    particle.acceleration = gAcceleration;
    particle.size = gSize;
    particle.age = 0.0;
    particle.lifeTime = gLifeTime;
    particle.rotation = 0.0;
    particle.rotateSpeed = gRotateSpeed;
    particle.color = gColor;
    particle.isActive = 1;
}

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    // 범위 체크
    if (particleIndex >= gMaxParticles)
        return;
    
    // 파티클 데이터 복사
    ParticleData particle = gParticlesInput[particleIndex];
    
    // 기존 활성 파티클 업데이트
    if (particle.isActive == 1)
    {
        particle.age += gDeltaTime;
        
        // 수명 체크
        if (particle.age >= particle.lifeTime)
        {
            particle.isActive = 0;
            particle.age = 0.0;
        }
    }
    // 비활성 파티클 - 스폰 체크
    else
    {
        // 각 파티클의 스폰 시간 계산 (순환적)
        float particleSpawnTime = float(particleIndex) / gSpawnRate;
        
        // 순환 스폰: 파티클이 죽은 후 다시 스폰되도록
        float spawnCycle = float(gMaxParticles) / gSpawnRate; // 전체 사이클 시간
        float cycleTime = fmod(gCurrentTime, spawnCycle * 2.0); // 2배 여유
        
        // 현재 사이클에서 이 파티클의 스폰 시간이 되었는지 체크
        bool shouldSpawn = false;
        
        // 첫 번째 사이클에서 스폰 체크
        if (cycleTime >= particleSpawnTime && cycleTime < particleSpawnTime + (1.0 / gSpawnRate))
        {
            shouldSpawn = true;
        }
        // 두 번째 사이클에서 스폰 체크 (재스폰)
        else if (cycleTime >= (spawnCycle + particleSpawnTime) &&
                 cycleTime < (spawnCycle + particleSpawnTime + (1.0 / gSpawnRate)))
        {
            shouldSpawn = true;
        }
        
        if (shouldSpawn)
        {
            // 고품질 랜덤 시드 생성
            uint seed = WangHash(particleIndex + uint(gCurrentTime * 1000.0) + gRandomSeed[0]);
            
            // 파티클 초기화 (이제 모든 이미터 타입 지원)
            InitializeParticle(particle, seed);
        }
    }
    
    // 결과 저장
    gParticlesOutput[particleIndex] = particle;
    
    // 첫 번째 스레드에서만 글로벌 랜덤 시드 업데이트
    if (DTid.x == 0)
    {
        gRandomSeed[0] = WangHash(gRandomSeed[0] + 1);
    }
}