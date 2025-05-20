// LifeModule.hlsl
// �����ֱ� ���� ��ǻƮ ���̴�

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

cbuffer TimeParams : register(b0)
{
    float gDeltaTime;
    uint gMaxParticles;
    float2 gPad;
}

// ��ƼŬ ����
RWStructuredBuffer<ParticleData> gParticles : register(u0);
// ��Ȱ�� ��ƼŬ �ε��� ����
RWStructuredBuffer<uint> gInactiveParticleIndices : register(u1);
// ��Ȱ�� ��ƼŬ ī����
RWStructuredBuffer<uint> gInactiveParticleCount : register(u2);
// Ȱ�� ��ƼŬ ī����
RWStructuredBuffer<uint> gActiveParticleCounter : register(u3);

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // ù ��° ������� ī���� �ʱ�ȭ (�� �κ��� ����)
    if (DTid.x == 0)
    {
        gActiveParticleCounter[0] = 0; // Ȱ�� ī���͸� 0���� ����
    }
    
    // ��� �����尡 �ʱ�ȭ�� ��ٸ�
    GroupMemoryBarrierWithGroupSync();
    
    uint particleIndex = DTid.x;
    uint maxParticles = gMaxParticles;
    
    if (particleIndex < maxParticles)
    {
        // ��ƼŬ ������ �ε�
        ParticleData particle = gParticles[particleIndex];
        
        // ��ƼŬ�� Ȱ�� ������ ��츸 ó��
        if (particle.isActive == 1)
        {
            // ���� ����
            particle.age += gDeltaTime;
            
            // �����: ���̿� ������ �������� ǥ��
            float lifeRatio = clamp(particle.age / particle.lifeTime, 0.0, 1.0);
            particle.color = float4(lifeRatio, 1.0 - lifeRatio, 0.0, 1.0);
            
            // ������ ���� ��� ��ƼŬ ��Ȱ��ȭ
            if (particle.age >= particle.lifeTime)
            {
                particle.isActive = 0;
                
                // ��Ȱ�� �ε��� ��Ͽ� �߰�
                uint inactiveIdx;
                InterlockedAdd(gInactiveParticleCount[0], 1, inactiveIdx);
                
                if (inactiveIdx == 0)
                {
                    // ù ��° ��Ȱ�� ��ƼŬ�� Ư�� ���� ���� (������)
                    particle.color = float4(1.0, 0.0, 1.0, 1.0);
                }
                
                // ��ȿ�� �ε��� ������ ��쿡�� ����
                if (inactiveIdx < maxParticles)
                {
                    gInactiveParticleIndices[inactiveIdx] = particleIndex;
                }
            }
            else
            {
                // ������ Ȱ�� ������ ��ƼŬ ī��Ʈ ����
                InterlockedAdd(gActiveParticleCounter[0], 1);
            }
        }
        
        // ������Ʈ�� ��ƼŬ ������ ����
        gParticles[particleIndex] = particle;
    }
}
