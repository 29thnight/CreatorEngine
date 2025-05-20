// SpawnModule.hlsl
// ��ƼŬ�� ������ ���� ��ǻƮ ���̴�

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
    float3 gAcceleration; // �ʱ� ���ӵ� 
    float gPad2; // �е�
    float gMinVerticalVelocity; // �ּ� ���� �ӵ�
    float gMaxVerticalVelocity; // �ִ� ���� �ӵ�
    float gHorizontalVelocityRange; // ���� �ӵ� ����
    float gPad3; // �е�
}

// ��ƼŬ ���� (register u0�� ���ε�)
RWStructuredBuffer<ParticleData> gParticles : register(u0);
// ���� ī���� ���� (register u1�� ���ε�)
RWStructuredBuffer<uint> gRandomCounter : register(u1);
RWStructuredBuffer<float> gTimeBuffer : register(u2);
// ���� �߰�: ���� ī���Ϳ� �ʿ��� ��ƼŬ ���� ���� ����
RWStructuredBuffer<uint> gSpawnCounter : register(u3);

RWStructuredBuffer<uint> gInactiveParticleIndices : register(u4);
RWStructuredBuffer<uint> gInactiveParticleCount : register(u5);
// ���� ���� �Լ�
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

// ��ƼŬ �ʱ�ȭ �Լ�
// ��ƼŬ �ʱ�ȭ �Լ� ����
void InitializeParticle(inout ParticleData particle, inout uint seed)
{
    // ��� �ʵ带 ���� 0���� �ʱ�ȭ
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
    
    // ���� ���� ������ ����
    particle.position = float3(0, 0, 0); // �⺻��, ���� �̹��� Ÿ�Կ� ���� �����
    particle.velocity = gVelocity;
    particle.acceleration = gAcceleration;
    particle.size = gSize;
    particle.age = 0.0f;
    particle.lifeTime = 15.0f;
    particle.rotation = 0.0f;
    particle.rotatespeed = gRotateSpeed;
    particle.color = gColor;
    particle.isActive = 1;
    
    // �̹��� Ÿ�Կ� ���� ��ġ ����
    switch (gEmitterType)
    {
        case 0: // point
            particle.position = float3(0.0f, 0.0f, 0.0f);
            break;
            
        case 1: // sphere
        {
                float theta = rand(seed) * 6.28318f; // 0 ~ 2�� (������)
                float phi = rand(seed) * 3.14159f; // 0 ~ �� (����)
                float radius = gEmitterRadius * pow(rand(seed), 1 / 3.0); // �յ� ������ ���� r ���
            
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
                float radius = sqrt(rand(seed)) * gEmitterRadius; // �յ� ������ ���� ������
            
                particle.position = float3(
                radius * cos(theta),
                0.0f,
                radius * sin(theta)
            );
                break;
            }
    }
    
    // �ʱ� �ӵ��� ���� ���� �߰�
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

// ������ �׷� ũ�� ����
#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // ù ��° �����常 �̹� �����ӿ� ������ ��ƼŬ �� ���
    if (DTid.x == 0)
    {
        // ��Ȱ�� ī���� �� Ȯ��
        uint inactiveCount = gInactiveParticleCount[0];
        
        // ������ ��ƼŬ �� ��� 
        float particlesPerSecond = gSpawnRate;
        float particlesThisFrame = particlesPerSecond * gDeltaTime;
        
        // ���� �Ҽ��� ó��
        float fractionalPart = frac(gTimeBuffer[0]);
        float totalParticles = particlesThisFrame + fractionalPart;
        
        // ������ ��ƼŬ �� (������)
        uint particlesToSpawn = min(uint(totalParticles), inactiveCount);
        
        // ���� �Ҽ��� �κ��� ���� ����������
        float remainingFraction = frac(totalParticles);
        
        // ������ ��ƼŬ �� ����
        gSpawnCounter[0] = particlesToSpawn;
        
        // ���� �Ҽ��� ����
        gTimeBuffer[0] = remainingFraction;
    }
    
    // �׷� ����ȭ
    GroupMemoryBarrierWithGroupSync();
    
    // �� ������� ���� �ε����� ����Ͽ� ��ƼŬ ó��
    uint globalIndex = DTid.x;
    uint spawnCount = gSpawnCounter[0];
    uint inactiveCount = gInactiveParticleCount[0];
    
    // ������ ��ƼŬ ������ �۰�, ��Ȱ�� ��ƼŬ�� ����� �ִ� ���
    if (globalIndex < spawnCount && globalIndex < inactiveCount)
    {
        // ��Ȱ�� �ε������� ��ƼŬ ���� ��������
        uint particleIndex = gInactiveParticleIndices[globalIndex];
        
        // ��ȿ�� �ε������� Ȯ��
        if (particleIndex < gMaxParticles)
        {
            // ���� �õ� ����
            uint seed = wang_hash(particleIndex + DTid.x + gRandomCounter[0]);
            InterlockedAdd(gRandomCounter[0], 1);
            
            // ��ƼŬ �ʱ�ȭ
            ParticleData particle = (ParticleData) 0;
            InitializeParticle(particle, seed);
            
            // �߿�: ��ƼŬ �����͸� GPU ���ۿ� ����
            gParticles[particleIndex] = particle;
        }
    }
}