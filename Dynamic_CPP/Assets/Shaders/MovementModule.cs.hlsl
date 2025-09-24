// MovementModule.hlsl
// ��ƼŬ �̵��� ���� ��ǻƮ ���̴�

// ��ƼŬ ����ü ����
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

// VelocityPoint ����ü
struct VelocityPoint
{
    float time;
    float3 velocity;
    float strength;
    float3 pad;
};

// ImpulseData ����ü
struct ImpulseData
{
    float triggerTime;
    float3 direction;
    float force;
    float duration;
    float spreadRange; // �л� ����
    int spreadType; // �л� Ÿ�� (0=����, 1=����, 2=����)
};

// ��� ���� ���� (C++�� MovementParams�� ��Ȯ�� ��ġ)
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

// ��ƼŬ ���� (�б�)
StructuredBuffer<ParticleData> ParticlesInput : register(t0);

// Velocity Curve ���� (�б�)
StructuredBuffer<VelocityPoint> VelocityCurve : register(t1);

// Impulse ���� (�б�) 
StructuredBuffer<ImpulseData> Impulses : register(t2);

// ��ƼŬ ���� (����)
RWStructuredBuffer<ParticleData> ParticlesOutput : register(u0);

// ������ �׷� ũ�� ����
#define THREAD_GROUP_SIZE 1024

// ������ ������ �Լ�
float noise(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

// Hash �Լ� (������ ������)
float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) * (1.0f / 4294967296.0f);
}

// �ð� ��� velocity ���� �Լ�
float3 GetVelocityFromCurve(float normalizedAge)
{
    if (velocityCurveSize <= 0)
        return float3(0, 0, 0);
    
    if (velocityCurveSize == 1)
        return VelocityCurve[0].velocity * VelocityCurve[0].strength;
    
    // ���� �ð��� �ش��ϴ� velocity ã��
    for (int i = 0; i < velocityCurveSize - 1; i++)
    {
        if (normalizedAge >= VelocityCurve[i].time && normalizedAge <= VelocityCurve[i + 1].time)
        {
            // �� ����Ʈ ���̿��� ���� ����
            float t = (normalizedAge - VelocityCurve[i].time) /
                     (VelocityCurve[i + 1].time - VelocityCurve[i].time);
            
            float3 vel1 = VelocityCurve[i].velocity * VelocityCurve[i].strength;
            float3 vel2 = VelocityCurve[i + 1].velocity * VelocityCurve[i + 1].strength;
            
            return lerp(vel1, vel2, t);
        }
    }
    
    // ������ ����� ���� ����� �� ���
    if (normalizedAge < VelocityCurve[0].time)
        return VelocityCurve[0].velocity * VelocityCurve[0].strength;
    else
        return VelocityCurve[velocityCurveSize - 1].velocity * VelocityCurve[velocityCurveSize - 1].strength;
}

// ��� ȿ�� ���
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
            
            uint seed1 = particleIndex * 73856093u ^ (i * 19349663u);
            uint seed2 = particleIndex * 83492791u ^ (i * 41943041u);
            
            float3 baseDir = normalize(Impulses[i].direction);
            float3 finalDirection;
            
            if (Impulses[i].spreadType == 0)
            {
                // ���� (�л� ����)
                finalDirection = baseDir;
            }
            else if (Impulses[i].spreadType == 1)
            {
                // ������ �л� (����)
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
                // ���� �л� (����)
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

// �ٶ� ȿ�� ���
float3 GetWindForce(float3 position, float time)
{
    // �⺻ �ٶ� ����
    float3 baseWind = normalize(windDirection) * windStrength;
    
    // ����� ȿ�� (������ ���)
    float2 noiseInput = position.xz * 0.1 + time * frequency;
    float noiseValue = noise(noiseInput) * 2.0 - 1.0; // -1 ~ 1 ����
    
    // ������� �ٶ��� �߰�
    float3 turbulentWind = baseWind + float3(
        noiseValue * turbulence,
        noise(noiseInput + 100.0) * turbulence * 0.5,
        noise(noiseInput + 200.0) * turbulence
    );
    
    return turbulentWind;
}

// �˵� � ���
float3 GetOrbitalVelocity(float3 position, float time)
{
    // �߽������κ����� ����
    float3 centerToParticle = position - orbitalCenter;
    
    // �˵� ��� ������ ��鿡���� ������ ���
    float3 tangent = cross(normalize(orbitalAxis), normalize(centerToParticle));
    
    // �˵� �ӵ� ����
    return tangent * orbitalSpeed;
}

float3 GetExplosiveMovement(float3 position, float normalizedAge, uint particleIndex, float particleAge)
{
    // ��ƼŬ ���� ���� ��� (����ð� - ����)
    float birthTime = currentTime - particleAge;
    
    // ��ƼŬ�� ������ �õ� (���� ���� + �ε����� ���������� ����)
    float2 seed = float2(particleIndex * 0.1537 + birthTime * 0.7321, particleIndex * 0.2891 + birthTime * 1.2345);
    float angle = noise(seed) * 6.28318; // 0~2��
    float elevation = (noise(seed + 100.0) - 0.5) * 3.14159 * explosiveSphere; // ���� ���� ����
    
    // ��ƼŬ�� ���� ���� (���� �������� �����ǰ� ������ ����)
    float3 explosionDir = float3(
        cos(angle) * cos(elevation),
        sin(elevation),
        sin(angle) * cos(elevation)
    );
    
    // �ʱ� ���� �ӵ� (�ð��� ���� ����)
    float speedDecay = 1.0 - pow(normalizedAge, explosiveDecay);
    
    // explosiveRandom�� 0�̸� ���� ���� 1.0 ����, �ƴϸ� ������ ����
    float randomFactor = 1.0;
    if (explosiveRandom > 0.0)
    {
        float2 speedSeed = float2(particleIndex * 0.0731 + birthTime * 0.4567, particleIndex * 0.1234 + birthTime * 0.8901);
        randomFactor = 1.0 + (noise(speedSeed) - 0.5) * explosiveRandom;
    }
    
    return explosionDir * explosiveSpeed * speedDecay * randomFactor;
}

// ���� ��ǻƮ ���̴� �Լ�
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    // ��ƼŬ �����͸� �Է� ���ۿ��� �б�
    ParticleData particle = ParticlesInput[particleIndex];
    
    // ��ƼŬ�� Ȱ��ȭ�� ��쿡�� ���
    if (particle.isActive)
    {
        // ����ȭ�� ���� ���
        float normalizedAge = particle.age / particle.lifeTime;
        
        // �߰� velocity ���
        float3 additionalVelocity = float3(0, 0, 0);
        
        // Velocity ��忡 ���� ó��
        if (velocityMode == 1) // Curve
        {
            additionalVelocity += GetVelocityFromCurve(normalizedAge);
        }
        else if (velocityMode == 2) // Impulse  
        {
            additionalVelocity += GetImpulseForce(normalizedAge, particleIndex, particle);
    
            // pad4�� ����� ���� ������ rotation ����
            if (particle.pad4.y > 0.001) // ������ ����� ��
            {
                float targetRotation = particle.pad4.x;
                float rotationLerpSpeed = 5.0;
                float lerpFactor = min(particle.pad4.y * rotationLerpSpeed * deltaTime, 1.0);
                particle.rotation = lerp(particle.rotation, targetRotation, lerpFactor);

                // ��� �� �ʱ�ȭ
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
        
        // ���� velocity�� �߰� velocity ���ϱ�
        particle.velocity += additionalVelocity * deltaTime;
        
        // �߷� ���� (������ ���)
        if (useGravity != 0)
        {
            particle.velocity += particle.acceleration * gravityStrength * deltaTime;
        }
        
        // ��ġ �� ȸ�� ������Ʈ
        particle.position += particle.velocity * deltaTime;
        particle.rotation += particle.rotatespeed * deltaTime;
    }
    
    // ���� ��ƼŬ �����͸� ��� ���ۿ� ����
    ParticlesOutput[particleIndex] = particle;
}