// MeshSpawnModule.hlsl - 3D 메시 파티클 스폰 컴퓨트 셰이더

struct MeshParticleData
{
    float3 position; // 12 bytes
    float pad1; // 4 bytes -> 16 bytes total
    
    float3 velocity; // 12 bytes  
    float pad2; // 4 bytes -> 32 bytes total
    
    float3 acceleration; // 12 bytes
    float pad3; // 4 bytes -> 48 bytes total
    
    float3 rotation; // 12 bytes
    float pad4; // 4 bytes -> 64 bytes total
    
    float3 rotationSpeed; // 12 bytes
    float pad5; // 4 bytes -> 80 bytes total
    
    float3 scale; // 12 bytes
    float pad6; // 4 bytes -> 96 bytes total
    
    float age; // 4 bytes
    float lifeTime; // 4 bytes
    uint isActive; // 4 bytes
    uint renderMode; // 4 bytes -> 112 bytes total
    
    float4 color; // 16 bytes -> 128 bytes total
    
    uint textureIndex; // 4 bytes
    float3 pad8; // 12 bytes -> 144 bytes total
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
    
    float3 gPreviousEmitterPosition;
    uint gForcePositionUpdate;
    
    float3 gEmitterRotation;
    uint gForceRotationUpdate;
    
    float3 gPreviousEmitterRotation;
    uint gAllowNewSpawn;
    
    float3 gWorldPosition;
    float padding;
}

// 3D 메시 파티클 템플릿
cbuffer MeshParticleTemplateParams : register(b1)
{
    float gLifeTime;
    float3 gScale;

    float3 gRotationSpeed;
    float templatePad2;

    float4 gColor;

    float3 gVelocity;
    float gVelocityRandomRange;

    float3 gAcceleration;
    uint textureIndex;

    int gRenderMode;
    float3 gParticleRandomRotation;
}

// 버퍼 바인딩
StructuredBuffer<MeshParticleData> gParticlesInput : register(t0);
RWStructuredBuffer<MeshParticleData> gParticlesOutput : register(u0);
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

float3 RandomRange3D(uint seed, float3 minVal, float3 maxVal)
{
    uint seed1 = WangHash(seed);
    uint seed2 = WangHash(seed1);
    uint seed3 = WangHash(seed2);
    
    return float3(
        RandomRange(seed1, minVal.x, maxVal.x),
        RandomRange(seed2, minVal.y, maxVal.y),
        RandomRange(seed3, minVal.z, maxVal.z)
    );
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
                0.0,
                r * sin(angle)
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
    
    float4x4 rotMat =
    {
        { rotationMatrix._11, rotationMatrix._12, rotationMatrix._13, 0 },
        { rotationMatrix._21, rotationMatrix._22, rotationMatrix._23, 0 },
        { rotationMatrix._31, rotationMatrix._32, rotationMatrix._33, 0 },
        { 0, 0, 0, 1 }
    };
    float4 emitterPos = mul(rotMat, float4(gEmitterPosition - gWorldPosition, 1.f));
    emitterPos.xyz += gWorldPosition;
    
    return rotatedPos + emitterPos.xyz;
}

// 초기 속도 생성
float3 GenerateInitialVelocity(uint seed)
{
    float3 velocity = gVelocity;
    
    if (gVelocityRandomRange > 0.0)
    {
        float3 randomOffset = RandomRange3D(seed,
            float3(-gVelocityRandomRange, -gVelocityRandomRange, -gVelocityRandomRange),
            float3(gVelocityRandomRange, gVelocityRandomRange, gVelocityRandomRange));
        velocity += randomOffset;
    }
    
    // 속도에도 회전 적용
    float3x3 rotationMatrix = CreateRotationMatrix(gEmitterRotation);
    return mul(rotationMatrix, velocity);
}

void InitializeMeshParticle(inout MeshParticleData particle, uint seed)
{
    particle.position = GenerateEmitterPosition(seed);
    particle.velocity = GenerateInitialVelocity(seed + 100);
    particle.acceleration = gAcceleration;
    
    particle.scale = gScale;
    particle.rotationSpeed = gRotationSpeed;
    
    // 랜덤 회전 적용
    if (length(gParticleRandomRotation) > 0.0)
    {
        float3 randomRotation = RandomRange3D(seed + 200,
        float3(-gParticleRandomRotation.x, -gParticleRandomRotation.y, -gParticleRandomRotation.z),
        float3(gParticleRandomRotation.x, gParticleRandomRotation.y, gParticleRandomRotation.z));
        particle.rotation = gEmitterRotation + randomRotation;
    }
    else
    {
        particle.rotation = gEmitterRotation;
    }

    particle.pad3 = gEmitterRotation.x;
    particle.pad4 = gEmitterRotation.y;
    particle.pad5 = gEmitterRotation.z;

    particle.age = 0.0;
    particle.lifeTime = gLifeTime;
    particle.color = gColor;
    particle.textureIndex = textureIndex;
    particle.renderMode = gRenderMode;
    particle.isActive = 1;
    particle.pad8 = particle.position;
}

void UpdateExistingMeshParticlePosition(inout MeshParticleData particle)
{
    // 에미터 위치 변화량 계산
    float3 positionDelta = gEmitterPosition - gPreviousEmitterPosition;
    
    // 기존 파티클 위치에 변화량 적용
    particle.position += positionDelta;
}

void UpdateExistingMeshParticleRotation(inout MeshParticleData particle)
{
    // 에미터 중심을 기준으로 파티클의 상대 위치 계산
    float3 relativePos = particle.position - gEmitterPosition;
    
    // 전체 에미터 회전 행렬
    float3x3 rotationMatrix = CreateRotationMatrix(gEmitterRotation);
    
    // 파티클 위치 회전
    float3 rotatedRelativePos = mul(rotationMatrix, relativePos);
    particle.position = rotatedRelativePos + gEmitterPosition;
    
    // 파티클 속도 회전  
    particle.velocity = mul(rotationMatrix, particle.velocity);
    
    // 파티클 자체 회전도 에미터 회전과 동일하게 설정
    particle.rotation = gEmitterRotation;
}

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    if (particleIndex >= gMaxParticles)
        return;
    
    MeshParticleData particle = gParticlesInput[particleIndex];
    
    // 기존 활성 파티클 업데이트
    if (particle.isActive == 1)
    {
        // 에미터 위치가 변경되었다면 즉시 파티클 위치 업데이트
        if (gForcePositionUpdate == 1)
        {
            UpdateExistingMeshParticlePosition(particle);
        }
        
        // 에미터 회전이 변경되었다면 즉시 파티클 회전 업데이트  
        if (gForceRotationUpdate == 1)
        {
            UpdateExistingMeshParticleRotation(particle);
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
    // 비활성 파티클 - 스폰 체크
    else
    {
        if (gAllowNewSpawn)
        {
        // SpawnRate가 MaxParticles보다 크거나 같으면 모든 파티클을 동시에 생성
            if (gSpawnRate >= gMaxParticles)
            {
                uint seed = WangHash(particleIndex * 12345 + uint(gCurrentTime * 1000.0) + gRandomSeed[0]);
                InitializeMeshParticle(particle, seed);
            }
            else
            {
            // 기존 순차 생성 로직
                float particleSpawnTime = float(particleIndex) / gSpawnRate;
                float spawnCycle = float(gMaxParticles) / gSpawnRate;
                float cycleTime = fmod(gCurrentTime, spawnCycle);
            
                float prevTime = cycleTime - gDeltaTime;
                if (prevTime < 0.0)
                    prevTime += spawnCycle;
            
                bool shouldSpawn = false;
            
                if (prevTime < cycleTime)
                {
                    shouldSpawn = (particleSpawnTime > prevTime && particleSpawnTime <= cycleTime);
                }
                else
                {
                    shouldSpawn = (particleSpawnTime > prevTime || particleSpawnTime <= cycleTime);
                }
            
                if (shouldSpawn)
                {
                    uint seed = WangHash(particleIndex * 12345 + uint(gCurrentTime * 1000.0) + gRandomSeed[0]);
                    InitializeMeshParticle(particle, seed);
                }
            }
        }
    }
    
    gParticlesOutput[particleIndex] = particle;
    
    if (DTid.x == 0)
    {
        gRandomSeed[0] = WangHash(gRandomSeed[0] + 1);
    }
}