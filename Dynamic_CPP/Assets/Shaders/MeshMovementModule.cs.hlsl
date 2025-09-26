// MeshMovementModule.hlsl
// 3D 메쉬 파티클 이동 및 크기 변화를 처리하는 컴퓨트 셰이더

// 3D 메쉬 파티클 구조체
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

// VelocityPoint 구조체
struct VelocityPoint
{
    float time;
    float3 velocity;
    float strength;
    float3 pad;
};

// ImpulseData 구조체
struct ImpulseData
{
    float triggerTime;
    float3 direction;
    float force;
    float duration;
    float spreadRange; // 분산 범위
    int spreadType; // 분산 타입 (0=직선, 1=원뿔, 2=구형)
};

// 상수 버퍼 정의 (C++의 MeshMovementParams와 정확히 일치)
cbuffer MeshMovementParams : register(b0)
{
    float deltaTime;
    float gravityStrength;
    int useGravity;
    int velocityMode;
    
    float currentTime;
    float3 windDirection;
    
    float windStrength;
    float turbulence;
    float frequency;
    float pad1;
    
    float3 orbitalCenter;
    float orbitalRadius;
    
    float orbitalSpeed;
    float3 orbitalAxis;
    
    float explosiveSpeed;
    float explosiveDecay;
    float explosiveRandom;
    float explosiveSphere;
    
    int velocityCurveSize;
    int impulseCount;
    float2 pad2;
};

// 파티클 버퍼 (읽기)
StructuredBuffer<MeshParticleData> ParticlesInput : register(t0);

// Velocity Curve 버퍼 (읽기)
StructuredBuffer<VelocityPoint> VelocityCurve : register(t1);

// Impulse 버퍼 (읽기) 
StructuredBuffer<ImpulseData> Impulses : register(t2);

// 파티클 버퍼 (쓰기)
RWStructuredBuffer<MeshParticleData> ParticlesOutput : register(u0);

// 스레드 그룹 크기 정의
#define THREAD_GROUP_SIZE 1024

// 간단한 노이즈 함수
float noise(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

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

// 시간 기반 velocity 보간 함수
float3 GetVelocityFromCurve(float normalizedAge)
{
    if (velocityCurveSize <= 0)
        return float3(0, 0, 0);
    
    if (velocityCurveSize == 1)
        return VelocityCurve[0].velocity * VelocityCurve[0].strength;
    
    // 현재 시간에 해당하는 velocity 찾기
    for (int i = 0; i < velocityCurveSize - 1; i++)
    {
        if (normalizedAge >= VelocityCurve[i].time && normalizedAge <= VelocityCurve[i + 1].time)
        {
            // 두 포인트 사이에서 선형 보간
            float t = (normalizedAge - VelocityCurve[i].time) /
                     (VelocityCurve[i + 1].time - VelocityCurve[i].time);
            
            float3 vel1 = VelocityCurve[i].velocity * VelocityCurve[i].strength;
            float3 vel2 = VelocityCurve[i + 1].velocity * VelocityCurve[i + 1].strength;
            
            return lerp(vel1, vel2, t);
        }
    }
    
    // 범위를 벗어나면 가장 가까운 값 사용
    if (normalizedAge < VelocityCurve[0].time)
        return VelocityCurve[0].velocity * VelocityCurve[0].strength;
    else
        return VelocityCurve[velocityCurveSize - 1].velocity * VelocityCurve[velocityCurveSize - 1].strength;
}

// 충격 효과 계산
float3 GetImpulseForce(float normalizedAge, uint particleIndex, inout MeshParticleData particle)
{
    float3 totalImpulse = float3(0, 0, 0);
    float3 dominantDirection = float3(0, 0, 0);
    float maxStrength = 0.0;
    
    for (int i = 0; i < impulseCount; i++)
    {
        float timeDiff = abs(normalizedAge - Impulses[i].triggerTime);
        
        if (timeDiff <= Impulses[i].duration)
        {
            float strength = 1.0 - (timeDiff / Impulses[i].duration);
            strength = strength * strength;
            
            uint seed1 = particleIndex * 73856093u ^ (i * 19349663u);
            uint seed2 = particleIndex * 83492791u ^ (i * 41943041u);
            
            float3 baseDir = normalize(Impulses[i].direction);
            float3 finalDirection;
            
            if (Impulses[i].spreadType == 0)
            {
                // 직선 (분산 없음)
                finalDirection = baseDir;
            }
            else if (Impulses[i].spreadType == 1)
            {
                // 원뿔형 분산 (샷건)
                float angle = Hash(seed1) * 6.28318;
                float radius = Hash(seed2) * Impulses[i].spreadRange;
                
                float3 up = abs(baseDir.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0);
                float3 right = normalize(cross(baseDir, up));
                up = normalize(cross(right, baseDir));
                
                float3 spreadOffset = (right * cos(angle) + up * sin(angle)) * radius;
                finalDirection = normalize(baseDir + spreadOffset);
            }
            else if (Impulses[i].spreadType == 2)
            {
                // 구형 분산 (폭발)
                float angle = Hash(seed1) * 6.28318;
                float elevation = (Hash(seed2) - 0.5) * 1.57079;
                
                float3 spreadDir = float3(
                    cos(angle) * cos(elevation),
                    sin(elevation),
                    sin(angle) * cos(elevation)
                );
                
                finalDirection = normalize(baseDir + spreadDir * Impulses[i].spreadRange);
            }
            
            totalImpulse += finalDirection * Impulses[i].force * strength;
            
            if (strength > maxStrength)
            {
                maxStrength = strength;
                dominantDirection = finalDirection;
            }
        }
    }
    
    if (maxStrength > 0.001)
    {
        particle.pad3 = dominantDirection;
        particle.pad4 = maxStrength;
    }
    
    return totalImpulse;
}

// 바람 효과 계산
float3 GetWindForce(float3 position, float time)
{
    // 기본 바람 방향
    float3 baseWind = normalize(windDirection) * windStrength;
    
    // 난기류 효과 (노이즈 기반)
    float2 noiseInput = position.xz * 0.1 + time * frequency;
    float noiseValue = noise(noiseInput) * 2.0 - 1.0; // -1 ~ 1 범위
    
    // 난기류를 바람에 추가
    float3 turbulentWind = baseWind + float3(
        noiseValue * turbulence,
        noise(noiseInput + 100.0) * turbulence * 0.5,
        noise(noiseInput + 200.0) * turbulence
    );
    
    return turbulentWind;
}

// 궤도 운동 계산
float3 GetOrbitalVelocity(float3 position, float time)
{
    // 중심점으로부터의 벡터
    float3 centerToParticle = position - orbitalCenter;
    
    // 궤도 축과 수직인 평면에서의 움직임 계산
    float3 tangent = cross(normalize(orbitalAxis), normalize(centerToParticle));
    
    // 궤도 속도 적용
    return tangent * orbitalSpeed;
}

float3 GetExplosiveMovement(float3 position, float normalizedAge, uint particleIndex, float particleAge)
{
    // 파티클 생성 시점 계산 (현재시간 - 나이)
    float birthTime = currentTime - particleAge;
    
    // 파티클별 고유한 시드 (생성 시점 + 인덱스로 랜덤하지만 고정)
    float2 seed = float2(particleIndex * 0.1537 + birthTime * 0.7321, particleIndex * 0.2891 + birthTime * 1.2345);
    float angle = noise(seed) * 6.28318; // 0~2π
    float elevation = (noise(seed + 100.0) - 0.5) * 3.14159 * explosiveSphere; // 구형 분포 조절
    
    // 파티클별 고정 방향 (생성 시점에서 결정되고 변하지 않음)
    float3 explosionDir = float3(
        cos(angle) * cos(elevation),
        sin(elevation),
        sin(angle) * cos(elevation)
    );
    
    // 초기 폭발 속도 (시간에 따라 감소)
    float speedDecay = 1.0 - pow(normalizedAge, explosiveDecay);
    
    // 랜덤성 추가 (파티클별 고정값)
    float2 noiseInput = float2(particleIndex * 0.0731 + birthTime * 0.4567, particleIndex * 0.1234 + birthTime * 0.8901);
    float randomFactor = 1.0 + (noise(noiseInput) - 0.5) * explosiveRandom;
    
    return explosionDir * explosiveSpeed * speedDecay * randomFactor;
}

float3x3 CreateRotationMatrix(float3 rotation)
{
    float cx = cos(rotation.x);
    float sx = sin(rotation.x);
    float cy = cos(rotation.y);
    float sy = sin(rotation.y);
    float cz = cos(rotation.z);
    float sz = sin(rotation.z);
    
    float3x3 rotX = float3x3(1, 0, 0, 0, cx, -sx, 0, sx, cx);
    float3x3 rotY = float3x3(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
    float3x3 rotZ = float3x3(cz, -sz, 0, sz, cz, 0, 0, 0, 1);
    
    return mul(mul(rotZ, rotY), rotX);
}

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    MeshParticleData particle = ParticlesInput[particleIndex];
    
    if (particle.isActive)
    {
        float normalizedAge = particle.age / particle.lifeTime;
        
        // 추가 velocity 계산 (모두 월드 공간에서)
        float3 additionalVelocity = float3(0, 0, 0);
        
        if (velocityMode == 1)
            additionalVelocity += GetVelocityFromCurve(normalizedAge);
        else if (velocityMode == 2)
        {
            additionalVelocity += GetImpulseForce(normalizedAge, particleIndex, particle);
        }
        else if (velocityMode == 3)
            additionalVelocity += GetWindForce(particle.position, currentTime);
        else if (velocityMode == 4)
            additionalVelocity += GetOrbitalVelocity(particle.position, currentTime);
        else if (velocityMode == 5)
            additionalVelocity += GetExplosiveMovement(particle.position, normalizedAge, particleIndex, particle.age);
        
        // velocity 업데이트 (월드 공간에서)
        particle.velocity += additionalVelocity * deltaTime;
        
        // 중력 적용 (월드 공간에서)
        if (useGravity != 0)
        {
            float3 gravityForce = float3(0, -9.8, 0) * gravityStrength;
            particle.velocity += gravityForce * deltaTime;
        }
        
        if (length(particle.velocity) > 0.001)
        {
            float3 dir = normalize(particle.velocity);
    
                // velocity 방향을 회전각으로 변환 (화살이 날아가는 방향으로)
            particle.rotation.y = atan2(-dir.x, dir.z); // yaw (좌우 회전)
            particle.rotation.x = -asin(dir.y); // pitch (위아래 회전)
                // roll(Z)은 rotationSpeed에 맡김
        }
        
        // 파티클 회전 업데이트 (로컬 회전)
        if (length(particle.rotationSpeed) > 0.0001)
        {
            particle.rotation += particle.rotationSpeed * deltaTime;
        }
        
        // 위치 업데이트 (월드 공간에서)
        particle.position += particle.velocity * deltaTime;
        
    }
    
    ParticlesOutput[particleIndex] = particle;
}