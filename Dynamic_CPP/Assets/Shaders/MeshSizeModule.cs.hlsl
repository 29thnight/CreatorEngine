// MeshSizeModule.hlsl
// ��ƼŬ ũ�� ��ȭ�� ó���ϴ� ��ǻƮ ���̴�

// ��ƼŬ ������ ����ü (�ٸ� ���� ��ġ�ؾ� ��)
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

// Size �Ķ����
cbuffer SizeParams : register(b0)
{
    float3 startSize;
    float pad1;
    float3 endSize;
    float deltaTime;
    
    int useRandomScale;
    float randomScaleMin;
    float randomScaleMax;
    uint maxParticles;
    
    float3 emitterScale;
    float pad2;
};

// ����� ����
StructuredBuffer<MeshParticleData> inputParticles : register(t0);
RWStructuredBuffer<MeshParticleData> outputParticles : register(u0);

#define THREAD_GROUP_SIZE 1024

// ������ �ؽ� �Լ�
uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float RandomFloat(uint seed)
{
    return float(WangHash(seed)) * (1.0f / 4294967296.0f);
}

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    
    if (particleIndex >= maxParticles)
        return;
    
    MeshParticleData particle = inputParticles[particleIndex];
    
    // ��Ȱ�� ��ƼŬ�� �ǳʶٱ�
    if (particle.isActive == 0)
    {
        outputParticles[particleIndex] = particle;
        return;
    }
    
    // ���� �ֱ� ���� ���
    float lifeRatio = saturate(particle.age / particle.lifeTime);
    
    // ũ�� ���� (�����ֱ⿡ ���� ũ�� ��ȭ)
    float3 currentSize = lerp(startSize, endSize, lifeRatio);
    
    // ���� ������ ����
    if (useRandomScale != 0)
    {
        float randomScale = lerp(randomScaleMin, randomScaleMax,
                               RandomFloat(particleIndex + 12345u));
        currentSize *= randomScale;
    }
    
    // �̹��� ��ü ������ ���� (���� ũ�� = ����ũ�� * �̹��ͽ�����)
    currentSize *= emitterScale;
    
    particle.scale = currentSize;
    outputParticles[particleIndex] = particle;
}