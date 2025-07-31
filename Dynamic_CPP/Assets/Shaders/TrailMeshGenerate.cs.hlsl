// TrailMeshGenerate.hlsl - 트레일 메쉬 생성 컴퓨트 셰이더

// 상수 버퍼
cbuffer TrailMeshParams : register(b0)
{
    float g_minDistance; // 트레일 생성 최소 거리
    float g_trailWidth; // 트레일 폭
    float g_deltaTime; // 프레임 델타타임
    uint g_maxParticles; // 최대 파티클 수
    
    uint g_enableTrail; // 트레일 활성화 여부
    float g_velocityThreshold; // 속도 임계값
    float g_maxTrailLength; // 최대 트레일 길이
    float g_widthOverLength; // 길이에 따른 폭 감소
    
    float4 g_trailColor; // 트레일 색상
    
    float g_uvTiling; // UV 타일링
    float g_uvScrollSpeed; // UV 스크롤 속도
    float g_currentTime; // 현재 시간
    float g_pad1; // 패딩
};

// 파티클 데이터 구조체 (기존 ParticleData와 동일해야 함)
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

// 트레일 정점 구조체
struct TrailVertex
{
    float3 position; // 정점 위치
    float2 texcoord; // UV 좌표
    float4 color; // 정점 색상
    float3 normal; // 법선
    float pad; // 패딩
};

// 입력 리소스
StructuredBuffer<MeshParticleData> g_particles : register(t0); // 현재 파티클 데이터
StructuredBuffer<float3> g_prevPositions : register(t1); // 이전 위치 데이터

// 출력 리소스
RWStructuredBuffer<float3> g_prevPositionsOut : register(u0); // 이전 위치 업데이트
RWByteAddressBuffer g_vertices : register(u1);
RWByteAddressBuffer g_indices : register(u2);
RWStructuredBuffer<uint> g_counters : register(u3); // 카운터 [0]=vertex count, [1]=index count

[numthreads(1024, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    
    // 범위 체크
    if (particleIndex >= g_maxParticles)
        return;
        
    // 트레일이 비활성화된 경우 리턴
    if (g_enableTrail == 0)
        return;
    
    // 파티클 데이터 가져오기
    MeshParticleData particle = g_particles[particleIndex];
    
    // 비활성 파티클은 건너뛰기
    if (particle.isActive == 0)
    {
        // 이전 위치는 현재 위치로 업데이트 (다음 프레임을 위해)
        g_prevPositionsOut[particleIndex] = particle.position;
        return;
    }
    
    // 현재 위치와 이전 위치
    float3 currentPos = particle.position;
    float3 prevPos = g_prevPositions[particleIndex];
    
    // 이동 벡터 계산
    float3 movement = currentPos - prevPos;
    float distance = length(movement);
    
    // 속도 체크
    float speed = length(particle.velocity);
    
    // 트레일 생성 조건 체크
    bool shouldCreateTrail = (distance > g_minDistance) &&
                            (speed > g_velocityThreshold) &&
                            (distance <= g_maxTrailLength);
    
    // 이전 위치 업데이트 (항상 수행)
    g_prevPositionsOut[particleIndex] = currentPos;
    
    if (!shouldCreateTrail)
        return;
    
    // 트레일 방향 계산
    float3 trailDirection = normalize(movement);
    
    // 카메라 방향 (임시로 up 벡터 사용, 실제로는 뷰 매트릭스에서 가져와야 함)
    float3 up = float3(0.0f, 1.0f, 0.0f);
    
    // 트레일에 수직인 벡터 계산 (오른쪽 방향)
    float3 right = normalize(cross(trailDirection, up));
    
    // up 벡터 재계산 (정확한 수직 벡터)
    up = normalize(cross(right, trailDirection));
    
    // 길이에 따른 폭 조절
    float widthScale = lerp(1.0f, 1.0f - g_widthOverLength, distance / g_maxTrailLength);
    float actualWidth = g_trailWidth * widthScale * 0.5f; // 반폭
    
    // 정점 인덱스 계산 (파티클당 4개 정점)
    uint vertexBase = 0;
    InterlockedAdd(g_counters[0], 4, vertexBase); // 원자적으로 정점 카운터 증가
    
    // 인덱스 계산 (파티클당 6개 인덱스)
    uint indexBase = 0;
    InterlockedAdd(g_counters[1], 6, indexBase); // 원자적으로 인덱스 카운터 증가
    
    // UV 스크롤 계산
    float uvOffset = g_uvScrollSpeed * g_currentTime;
    
    // 거리 기반 UV 길이
    float uvLength = distance * g_uvTiling;
    
    // 색상 계산 (거리와 속도에 따른 알파 조절)
    float4 trailColor = g_trailColor;
    trailColor.a *= saturate(speed / (g_velocityThreshold * 10.0f)); // 속도에 따른 알파
    trailColor.a *= widthScale; // 폭 감소에 따른 알파 감소
    
    // 법선 벡터 (트레일 방향)
    float3 normal = trailDirection;
    
    // 정점 0: 이전 위치 - 왼쪽
    uint vertex0Offset = (vertexBase + 0) * 44;
    g_vertices.Store3(vertex0Offset, asuint(prevPos - right * actualWidth)); // position (12바이트)
    g_vertices.Store2(vertex0Offset + 12, asuint(float2(0.0f, uvOffset))); // texcoord (8바이트)
    g_vertices.Store4(vertex0Offset + 20, asuint(trailColor)); // color (16바이트)
    g_vertices.Store3(vertex0Offset + 36, asuint(normal)); // normal (12바이트)
    // pad는 생략 (Store로 0을 써도 되지만 필수는 아님)

    // 정점 1: 이전 위치 - 오른쪽
    uint vertex1Offset = (vertexBase + 1) * 44;
    g_vertices.Store3(vertex1Offset, asuint(prevPos + right * actualWidth));
    g_vertices.Store2(vertex1Offset + 12, asuint(float2(1.0f, uvOffset)));
    g_vertices.Store4(vertex1Offset + 20, asuint(trailColor));
    g_vertices.Store3(vertex1Offset + 36, asuint(normal));

    // 정점 2: 현재 위치 - 왼쪽
    uint vertex2Offset = (vertexBase + 2) * 44;
    g_vertices.Store3(vertex2Offset, asuint(currentPos - right * actualWidth));
    g_vertices.Store2(vertex2Offset + 12, asuint(float2(0.0f, uvOffset + uvLength)));
    g_vertices.Store4(vertex2Offset + 20, asuint(trailColor));
    g_vertices.Store3(vertex2Offset + 36, asuint(normal));

    // 정점 3: 현재 위치 - 오른쪽
    uint vertex3Offset = (vertexBase + 3) * 44;
    g_vertices.Store3(vertex3Offset, asuint(currentPos + right * actualWidth));
    g_vertices.Store2(vertex3Offset + 12, asuint(float2(1.0f, uvOffset + uvLength)));
    g_vertices.Store4(vertex3Offset + 20, asuint(trailColor));
    g_vertices.Store3(vertex3Offset + 36, asuint(normal));
    
    // 첫 번째 삼각형 (0, 1, 2)
    g_indices.Store((indexBase + 0) * 4, vertexBase + 0);
    g_indices.Store((indexBase + 1) * 4, vertexBase + 1);
    g_indices.Store((indexBase + 2) * 4, vertexBase + 2);

    // 두 번째 삼각형 (1, 3, 2)
    g_indices.Store((indexBase + 3) * 4, vertexBase + 1);
    g_indices.Store((indexBase + 4) * 4, vertexBase + 3);
    g_indices.Store((indexBase + 5) * 4, vertexBase + 2);
}