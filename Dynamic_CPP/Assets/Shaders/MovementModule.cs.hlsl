// MovementModule.hlsl
// 파티클 이동에 관한 컴퓨트 셰이더

// 파티클 구조체 정의
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
    float2 pad;
};

// 상수 버퍼 정의 (C++의 MovementParams와 정확히 일치)
cbuffer MovementParams : register(b0)
{
    float deltaTime;
    float gravityStrength;
    int useGravity;
    int velocityMode; // 0: Constant, 1: Curve, 2: Impulse, 3: Wind, 4: Orbital
    
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
StructuredBuffer<ParticleData> ParticlesInput : register(t0);

// Velocity Curve 버퍼 (읽기)
StructuredBuffer<VelocityPoint> VelocityCurve : register(t1);

// Impulse 버퍼 (읽기) 
StructuredBuffer<ImpulseData> Impulses : register(t2);

// 파티클 버퍼 (쓰기)
RWStructuredBuffer<ParticleData> ParticlesOutput : register(u0);

// 스레드 그룹 크기 정의
#define THREAD_GROUP_SIZE 1024

// 간단한 노이즈 함수
float noise(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
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
float3 GetImpulseForce(float normalizedAge, uint particleIndex, inout ParticleData particle)
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
            
            float2 seed = float2(particleIndex * 0.1234 + i * 0.5678, particleIndex * 0.8765 + i * 0.4321);
            float angle = noise(seed) * 6.28318;
            float elevation = (noise(seed + 50.0) - 0.5) * 1.57079;
            
            float3 baseDir = normalize(Impulses[i].direction);
            float3 spreadDir = float3(
                cos(angle) * cos(elevation),
                sin(elevation),
                sin(angle) * cos(elevation)
            );
            
            float3 finalDirection = normalize(baseDir + spreadDir * 0.5);
            
            totalImpulse += finalDirection * Impulses[i].force * strength;
            
            // 가장 강한 impulse의 방향을 기억 (pad4에 임시 저장)
            if (strength > maxStrength)
            {
                maxStrength = strength;
                dominantDirection = finalDirection;
            }
        }
    }
    
    // pad4에 방향 정보 임시 저장 (x에는 방향각도, y에는 강도)
    if (maxStrength > 0.001)
    {
        particle.pad4.x = atan2(dominantDirection.z, dominantDirection.x); // 목표 각도
        particle.pad4.y = maxStrength; // 강도
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
    
    // explosiveRandom이 0이면 랜덤 팩터 1.0 고정, 아니면 랜덤성 적용
    float randomFactor = 1.0;
    if (explosiveRandom > 0.0)
    {
        float2 speedSeed = float2(particleIndex * 0.0731 + birthTime * 0.4567, particleIndex * 0.1234 + birthTime * 0.8901);
        randomFactor = 1.0 + (noise(speedSeed) - 0.5) * explosiveRandom;
    }
    
    return explosionDir * explosiveSpeed * speedDecay * randomFactor;
}

// 메인 컴퓨트 셰이더 함수
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    // 파티클 데이터를 입력 버퍼에서 읽기
    ParticleData particle = ParticlesInput[particleIndex];
    
    // 파티클이 활성화된 경우에만 계산
    if (particle.isActive)
    {
        // 정규화된 나이 계산
        float normalizedAge = particle.age / particle.lifeTime;
        
        // 추가 velocity 계산
        float3 additionalVelocity = float3(0, 0, 0);
        
        // Velocity 모드에 따른 처리
        if (velocityMode == 1) // Curve
        {
            additionalVelocity += GetVelocityFromCurve(normalizedAge);
        }
        else if (velocityMode == 2) // Impulse  
        {
            additionalVelocity += GetImpulseForce(normalizedAge, particleIndex, particle);
    
            // pad4에 저장된 방향 정보로 rotation 조정
            if (particle.pad4.y > 0.001) // 강도가 충분할 때
            {
                float targetRotation = particle.pad4.x;
                float rotationLerpSpeed = 5.0;
                float lerpFactor = min(particle.pad4.y * rotationLerpSpeed * deltaTime, 1.0);
                particle.rotation = lerp(particle.rotation, targetRotation, lerpFactor);

                // 사용 후 초기화
                particle.pad4 = float2(0, 0);
            }
        }
        else if (velocityMode == 3) // Wind
        {
            additionalVelocity += GetWindForce(particle.position, currentTime);
        }
        else if (velocityMode == 4) // Orbital
        {
            additionalVelocity += GetOrbitalVelocity(particle.position, currentTime);
        }
        else if (velocityMode == 5) // explosive
        {
            additionalVelocity += GetExplosiveMovement(particle.position, normalizedAge, particleIndex, particle.age);
        }
        
        // 기존 velocity에 추가 velocity 더하기
        particle.velocity += additionalVelocity * deltaTime;
        
        // 중력 적용 (설정된 경우)
        if (useGravity != 0)
        {
            particle.velocity += particle.acceleration * gravityStrength * deltaTime;
        }
        
        // 위치 및 회전 업데이트
        particle.position += particle.velocity * deltaTime;
        particle.rotation += particle.rotatespeed * deltaTime;
    }
    
    // 계산된 파티클 데이터를 출력 버퍼에 쓰기
    ParticlesOutput[particleIndex] = particle;
}