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

// ��� ���� ����
cbuffer MovementParams : register(b0)
{
    float deltaTime; // ������ �ð�
    float gravityStrength; // �߷� ����
    int useGravity; // �߷� ��� ����
    float padding; // �е�
};

// ��ƼŬ ���� (�б�)
StructuredBuffer<ParticleData> ParticlesInput : register(t0);
// ��ƼŬ ���� (����)
RWStructuredBuffer<ParticleData> ParticlesOutput : register(u0);

// ������ �׷� ũ�� ����
#define THREAD_GROUP_SIZE 1024

// ���� ��ǻƮ ���̴� �Լ�
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // ������ ID�� ��ƼŬ �迭 ũ�⸦ �ʰ��ϸ� ����
    uint particleIndex = DTid.x;
    
    // ��ƼŬ �����͸� �Է� ���ۿ��� �б�
    ParticleData particle = ParticlesInput[particleIndex];
    
    // ��ƼŬ�� Ȱ��ȭ�� ��쿡�� ���
    if (particle.isActive)
    {
        // ����ȭ�� ���� ��� (��¡ ������ ����)
        float normalizedAge = particle.age / particle.lifeTime;
        
        // �߷� ���� (������ ���)
        if (useGravity != 0)
        {
            // ���ӵ��� �߷� ���� ����    
            particle.velocity += particle.acceleration * gravityStrength * deltaTime;
            //particle.color = float4(1, 0, 0, 1);
        }
        // ��ġ �� ȸ�� ������Ʈ
        particle.position += particle.velocity * deltaTime;
        particle.rotation += particle.rotatespeed * deltaTime;
        
    }
    
    // ���� ��ƼŬ �����͸� ��� ���ۿ� ����
    ParticlesOutput[particleIndex] = particle;
}