// SizeModule.hlsl
// ��ƼŬ ũ�� ��ȭ�� ó���ϴ� ��ǻƮ ���̴�

// ��ƼŬ ������ ����ü (�ٸ� ���� ��ġ�ؾ� ��)
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

// Size �Ķ����
cbuffer SizeParams : register(b0)
{
    float2 startSize; // ���� ũ��
    float2 endSize; // �� ũ��
    
    float deltaTime; // ��Ÿ �ð� (��¡ �����)
    int useRandomScale; // ���� ������ ��� ����
    float randomScaleMin; // ���� ������ �ּҰ�
    float randomScaleMax; // ���� ������ �ִ밪
    
    uint maxParticles; // �ִ� ��ƼŬ ��
    int useOscillation;
    float oscillationSpeed;
    float ppad2;
    
    float3 emitterScale;
    float ppad1;
};

// ����� ����
StructuredBuffer<ParticleData> inputParticles : register(t0);
RWStructuredBuffer<ParticleData> outputParticles : register(u0);

// ���
#define THREAD_GROUP_SIZE 1024

// ������ �ؽ� �Լ� (������ ������)
float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) * (1.0f / 4294967296.0f);
}

// ���� ��ǻƮ ���̴�
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    uint numParticles, stride;
    inputParticles.GetDimensions(numParticles, stride);
    
    // ���� �˻�
    if (particleIndex >= numParticles)
        return;
    
    // �Է� ��ƼŬ ������ ��������
    ParticleData particle = inputParticles[particleIndex];
    
    // ���� ��ƼŬ�� ó������ ����
    if (particle.age >= particle.lifeTime || particle.lifeTime <= 0.0f)
    {
        outputParticles[particleIndex] = particle;
        return;
    }
    
    // ���� �ֱ� ���� ��� (0.0 ~ 1.0)
    float lifeRatio = saturate(particle.age / particle.lifeTime);
    
    // ũ�� ���� (�̹� CPU���� ��¡�� ����� ����)
    float2 currentSize = lerp(startSize, endSize, lifeRatio);
    
    if (useOscillation)
    {
        float oscillation = (sin(particle.age * oscillationSpeed) + 1.0f) * 0.5f;
        float2 oscillationSize = lerp(startSize, endSize, oscillation);
        currentSize = oscillationSize;
    }
    
    // ���� ������ ���� (��ƼŬ���� ������ �õ� ���)
    if (useRandomScale)
    {
        // ��ƼŬ �ε����� �õ�� ����Ͽ� �ϰ��� ������ ����
        float timeSeed = particle.age * 1000.0f; // age�� �õ�� Ȱ��
        float randomScale = lerp(randomScaleMin, randomScaleMax, Hash(particleIndex * 1000 + uint(timeSeed)));
        currentSize *= randomScale;
    }

    currentSize.x *= emitterScale.x;
    currentSize.y *= emitterScale.y;
    
    // ��� ����
    particle.size = currentSize;
    
    // ��� ���ۿ� ����
    outputParticles[particleIndex] = particle;
}