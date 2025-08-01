// TrailMeshGenerate.hlsl - Ʈ���� �޽� ���� ��ǻƮ ���̴�

// ��� ����
cbuffer TrailMeshParams : register(b0)
{
    float g_minDistance; // Ʈ���� ���� �ּ� �Ÿ�
    float g_trailWidth; // Ʈ���� ��
    float g_deltaTime; // ������ ��ŸŸ��
    uint g_maxParticles; // �ִ� ��ƼŬ ��
    
    uint g_enableTrail; // Ʈ���� Ȱ��ȭ ����
    float g_velocityThreshold; // �ӵ� �Ӱ谪
    float g_maxTrailLength; // �ִ� Ʈ���� ����
    float g_widthOverLength; // ���̿� ���� �� ����
    
    float4 g_trailColor; // Ʈ���� ����
    
    float g_uvTiling; // UV Ÿ�ϸ�
    float g_uvScrollSpeed; // UV ��ũ�� �ӵ�
    float g_currentTime; // ���� �ð�
    float g_pad1; // �е�
};

// ��ƼŬ ������ ����ü (���� ParticleData�� �����ؾ� ��)
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

// Ʈ���� ���� ����ü
struct TrailVertex
{
    float3 position; // ���� ��ġ
    float2 texcoord; // UV ��ǥ
    float4 color; // ���� ����
    float3 normal; // ����
    float pad; // �е�
};

// �Է� ���ҽ�
StructuredBuffer<MeshParticleData> g_particles : register(t0); // ���� ��ƼŬ ������
StructuredBuffer<float3> g_prevPositions : register(t1); // ���� ��ġ ������

// ��� ���ҽ�
RWStructuredBuffer<float3> g_prevPositionsOut : register(u0); // ���� ��ġ ������Ʈ
RWByteAddressBuffer g_vertices : register(u1);
RWByteAddressBuffer g_indices : register(u2);
RWStructuredBuffer<uint> g_counters : register(u3); // ī���� [0]=vertex count, [1]=index count

[numthreads(1024, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    
    // ���� üũ
    if (particleIndex >= g_maxParticles)
        return;
        
    // Ʈ������ ��Ȱ��ȭ�� ��� ����
    if (g_enableTrail == 0)
        return;
    
    // ��ƼŬ ������ ��������
    MeshParticleData particle = g_particles[particleIndex];
    
    // ��Ȱ�� ��ƼŬ�� �ǳʶٱ�
    if (particle.isActive == 0)
    {
        // ���� ��ġ�� ���� ��ġ�� ������Ʈ (���� �������� ����)
        g_prevPositionsOut[particleIndex] = particle.position;
        return;
    }
    
    // ���� ��ġ�� ���� ��ġ
    float3 currentPos = particle.position;
    float3 prevPos = g_prevPositions[particleIndex];
    
    // �̵� ���� ���
    float3 movement = currentPos - prevPos;
    float distance = length(movement);
    
    // �ӵ� üũ
    float speed = length(particle.velocity);
    
    // Ʈ���� ���� ���� üũ
    bool shouldCreateTrail = (distance > g_minDistance) &&
                            (speed > g_velocityThreshold) &&
                            (distance <= g_maxTrailLength);
    
    // ���� ��ġ ������Ʈ (�׻� ����)
    g_prevPositionsOut[particleIndex] = currentPos;
    
    if (!shouldCreateTrail)
        return;
    
    // Ʈ���� ���� ���
    float3 trailDirection = normalize(movement);
    
    // ī�޶� ���� (�ӽ÷� up ���� ���, �����δ� �� ��Ʈ�������� �����;� ��)
    float3 up = float3(0.0f, 1.0f, 0.0f);
    
    // Ʈ���Ͽ� ������ ���� ��� (������ ����)
    float3 right = normalize(cross(trailDirection, up));
    
    // up ���� ���� (��Ȯ�� ���� ����)
    up = normalize(cross(right, trailDirection));
    
    // ���̿� ���� �� ����
    float widthScale = lerp(1.0f, 1.0f - g_widthOverLength, distance / g_maxTrailLength);
    float actualWidth = g_trailWidth * widthScale * 0.5f; // ����
    
    // ���� �ε��� ��� (��ƼŬ�� 4�� ����)
    uint vertexBase = 0;
    InterlockedAdd(g_counters[0], 4, vertexBase); // ���������� ���� ī���� ����
    
    // �ε��� ��� (��ƼŬ�� 6�� �ε���)
    uint indexBase = 0;
    InterlockedAdd(g_counters[1], 6, indexBase); // ���������� �ε��� ī���� ����
    
    // UV ��ũ�� ���
    float uvOffset = g_uvScrollSpeed * g_currentTime;
    
    // �Ÿ� ��� UV ����
    float uvLength = distance * g_uvTiling;
    
    // ���� ��� (�Ÿ��� �ӵ��� ���� ���� ����)
    float4 trailColor = g_trailColor;
    trailColor.a *= saturate(speed / (g_velocityThreshold * 10.0f)); // �ӵ��� ���� ����
    trailColor.a *= widthScale; // �� ���ҿ� ���� ���� ����
    
    // ���� ���� (Ʈ���� ����)
    float3 normal = trailDirection;
    
    // ���� 0: ���� ��ġ - ����
    uint vertex0Offset = (vertexBase + 0) * 44;
    g_vertices.Store3(vertex0Offset, asuint(prevPos - right * actualWidth)); // position (12����Ʈ)
    g_vertices.Store2(vertex0Offset + 12, asuint(float2(0.0f, uvOffset))); // texcoord (8����Ʈ)
    g_vertices.Store4(vertex0Offset + 20, asuint(trailColor)); // color (16����Ʈ)
    g_vertices.Store3(vertex0Offset + 36, asuint(normal)); // normal (12����Ʈ)
    // pad�� ���� (Store�� 0�� �ᵵ ������ �ʼ��� �ƴ�)

    // ���� 1: ���� ��ġ - ������
    uint vertex1Offset = (vertexBase + 1) * 44;
    g_vertices.Store3(vertex1Offset, asuint(prevPos + right * actualWidth));
    g_vertices.Store2(vertex1Offset + 12, asuint(float2(1.0f, uvOffset)));
    g_vertices.Store4(vertex1Offset + 20, asuint(trailColor));
    g_vertices.Store3(vertex1Offset + 36, asuint(normal));

    // ���� 2: ���� ��ġ - ����
    uint vertex2Offset = (vertexBase + 2) * 44;
    g_vertices.Store3(vertex2Offset, asuint(currentPos - right * actualWidth));
    g_vertices.Store2(vertex2Offset + 12, asuint(float2(0.0f, uvOffset + uvLength)));
    g_vertices.Store4(vertex2Offset + 20, asuint(trailColor));
    g_vertices.Store3(vertex2Offset + 36, asuint(normal));

    // ���� 3: ���� ��ġ - ������
    uint vertex3Offset = (vertexBase + 3) * 44;
    g_vertices.Store3(vertex3Offset, asuint(currentPos + right * actualWidth));
    g_vertices.Store2(vertex3Offset + 12, asuint(float2(1.0f, uvOffset + uvLength)));
    g_vertices.Store4(vertex3Offset + 20, asuint(trailColor));
    g_vertices.Store3(vertex3Offset + 36, asuint(normal));
    
    // ù ��° �ﰢ�� (0, 1, 2)
    g_indices.Store((indexBase + 0) * 4, vertexBase + 0);
    g_indices.Store((indexBase + 1) * 4, vertexBase + 1);
    g_indices.Store((indexBase + 2) * 4, vertexBase + 2);

    // �� ��° �ﰢ�� (1, 3, 2)
    g_indices.Store((indexBase + 3) * 4, vertexBase + 1);
    g_indices.Store((indexBase + 4) * 4, vertexBase + 3);
    g_indices.Store((indexBase + 5) * 4, vertexBase + 2);
}