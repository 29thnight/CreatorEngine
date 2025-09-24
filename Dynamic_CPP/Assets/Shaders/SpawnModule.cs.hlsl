// SpawnModule.hlsl - 파티클 스폰 컴퓨트 셰이더 (즉시 위치 업데이트 버전)

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
    float gSpawnRate;
    float gDeltaTime;
    float gCurrentTime;
    int gEmitterType;
    
    float3 gEmitterSize;
    float gEmitterRadius;
    
    uint gMaxParticles;
    float3 gEmitterPosition;
    
    // 즉시 위치 업데이트를 위한 새 변수들
    float3 gPreviousEmitterPosition;
    uint gForcePositionUpdate;
    
    float3 gEmitterRotation;
    uint gForceRotationUpdate;
    
    float3 gPreviousEmitterRotation;
    uint gAllowNewSpawn;
}

// 파티클 템플릿
cbuffer ParticleTemplate : register(b1)
{
    float gLifeTime;
    float gRotateSpeed;
    float2 gSize;
    
    float4 gColor;
    
    float3 gVelocity;
    float gVelocityRandomRange;
    
    float3 gAcceleration;
    float gInitialRotation;
    
    float gInitialRotationRange;
    float pad1;
    float pad2;
    float pad3;
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

float3x3 CreateRotationMatrixX(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3x3(
        1, 0, 0,
        0, c, -s,
        0, s, c
    );
}

float3x3 CreateRotationMatrixY(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3x3(
        c, 0, s,
        0, 1, 0,
        -s, 0, c
    );
}

float3x3 CreateRotationMatrixZ(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3x3(
        c, -s, 0,
        s, c, 0,
        0, 0, 1
    );
}

float3x3 CreateRotationMatrix(float3 rotation)
{
    float3x3 rotX = CreateRotationMatrixX(rotation.x);
    float3x3 rotY = CreateRotationMatrixY(rotation.y);
    float3x3 rotZ = CreateRotationMatrixZ(rotation.z);
    
    return mul(mul(rotZ, rotY), rotX);
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
                r * sin(angle),
                0.0
            );
                break;
            }
        
        default:
            localPos = float3(0, 0, 0);
            break;
    }
    
    // 로컬 위치에 회전 적용
    float3x3 rotationMatrix = CreateRotationMatrix(gEmitterRotation);
    float3 rotatedPos = mul(rotationMatrix, localPos);
    
    return rotatedPos + gEmitterPosition;
}

// 초기 속도 생성
float3 GenerateInitialVelocity(uint seed)
{
    float3 velocity = gVelocity;
    
    // 랜덤 범위 적용 (각 축별로)
    if (gVelocityRandomRange > 0.0)
    {
        uint seed1 = WangHash(seed);
        uint seed2 = WangHash(seed1);
        
        float randomX = (RandomFloat01(seed1) - 0.5) * gVelocityRandomRange;
        float randomY = (RandomFloat01(seed2) - 0.5) * gVelocityRandomRange;
        
        velocity += float3(randomX, randomY, 0);
    }
    
    // 속도에도 회전 적용
    float3x3 rotationMatrix = CreateRotationMatrix(gEmitterRotation);
    return mul(rotationMatrix, velocity);
}

// 2D 빌보드 초기 회전 생성
float GenerateInitialRotation(uint seed)
{
    float rotation = gInitialRotation;
    
    // 랜덤 범위 적용
    if (gInitialRotationRange > 0.0)
    {
        float randomOffset = (RandomFloat01(seed) - 0.5) * gInitialRotationRange;
        rotation += randomOffset;
    }
    
    return rotation;
}

// 파티클 초기화
void InitializeParticle(inout ParticleData particle, uint seed)
{
    particle.position = GenerateEmitterPosition(seed);
    particle.velocity = GenerateInitialVelocity(seed + 100);
    particle.acceleration = gAcceleration;
    particle.size = gSize;
    particle.age = 0.0;
    particle.lifeTime = gLifeTime;
    particle.rotation = GenerateInitialRotation(seed + 200); // Z축 회전만
    particle.rotateSpeed = gRotateSpeed;
    particle.color = gColor;
    particle.isActive = 1;
    particle.pad5 = particle.position;
}

// 기존 파티클의 위치를 즉시 업데이트 (에미터 이동 시)
void UpdateExistingParticlePosition(inout ParticleData particle)
{
    // 에미터 위치 변화량 계산
    float3 positionDelta = gEmitterPosition - gPreviousEmitterPosition;
    
    // 기존 파티클 위치에 변화량 적용
    particle.position += positionDelta;
}

void UpdateExistingParticleRotation(inout ParticleData particle)
{
    // 변화량만 계산
    float3 rotationDelta = gEmitterRotation - gPreviousEmitterRotation;
    
    // 에미터 중심 기준 상대 위치
    float3 relativePos = particle.position - gEmitterPosition;
    
    // 델타 회전만 적용
    float3x3 deltaRotationMatrix = CreateRotationMatrix(rotationDelta);
    float3 rotatedRelativePos = mul(deltaRotationMatrix, relativePos);
    
    particle.position = rotatedRelativePos + gEmitterPosition;
    particle.velocity = mul(deltaRotationMatrix, particle.velocity);
}

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    if (particleIndex >= gMaxParticles)
        return;
    
    ParticleData particle = gParticlesInput[particleIndex];
    
    // 기존 활성 파티클 업데이트
    if (particle.isActive == 1)
    {
        // 에미터 위치가 변경되었다면 즉시 파티클 위치 업데이트
        if (gForcePositionUpdate == 1)
        {
            UpdateExistingParticlePosition(particle);
        }
    
        // 에미터 회전이 변경되었다면 즉시 파티클 회전 업데이트
        if (gForceRotationUpdate == 1)
        {
            UpdateExistingParticleRotation(particle);
        }
        
        // 나이 증가
        particle.age += gDeltaTime;
    
        // 수명 체크
        if (particle.age >= particle.lifeTime)
        {
            particle.isActive = 0;
            particle.age = 0.0;
        }
    }
    // 비활성 파티클 스폰 체크
    else
    {
        if (gAllowNewSpawn == 1)
        {
            float particleSpawnTime = float(particleIndex) / gSpawnRate;
            float spawnCycle = float(gMaxParticles) / gSpawnRate;
            float cycleTime = fmod(gCurrentTime, spawnCycle * 2.0);
        
            bool shouldSpawn = false;
        
            if (cycleTime >= particleSpawnTime && cycleTime < particleSpawnTime + (1.0 / gSpawnRate))
            {
                shouldSpawn = true;
            }
            else if (cycleTime >= (spawnCycle + particleSpawnTime) &&
                 cycleTime < (spawnCycle + particleSpawnTime + (1.0 / gSpawnRate)))
            {
                shouldSpawn = true;
            }
        
            if (shouldSpawn)
            {
                uint seed = WangHash(particleIndex + uint(gCurrentTime * 1000.0) + gRandomSeed[0]);
                InitializeParticle(particle, seed);
            }
        }
    }
    
    gParticlesOutput[particleIndex] = particle;
    
    if (DTid.x == 0)
    {
        gRandomSeed[0] = WangHash(gRandomSeed[0] + 1);
    }
}