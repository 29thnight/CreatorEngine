// LifeModule.hlsl
// 생명주기 관리 컴퓨트 셰이더

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

// 파티클 버퍼
RWStructuredBuffer<ParticleData> gParticles : register(u0);
// 비활성 파티클 인덱스 버퍼
RWStructuredBuffer<uint> gInactiveParticleIndices : register(u1);
// 비활성 파티클 카운터
RWStructuredBuffer<uint> gInactiveParticleCount : register(u2);
// 활성 파티클 카운터
RWStructuredBuffer<uint> gActiveParticleCounter : register(u3);

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 첫 번째 스레드는 카운터 초기화 (이 부분은 유지)
    if (DTid.x == 0)
    {
        gActiveParticleCounter[0] = 0; // 활성 카운터를 0으로 리셋
    }
    
    // 모든 스레드가 초기화를 기다림
    GroupMemoryBarrierWithGroupSync();
    
    uint particleIndex = DTid.x;
    uint maxParticles = gMaxParticles;
    
    if (particleIndex < maxParticles)
    {
        // 파티클 데이터 로드
        ParticleData particle = gParticles[particleIndex];
        
        // 파티클이 활성 상태인 경우만 처리
        if (particle.isActive == 1)
        {
            // 나이 증가
            particle.age += gDeltaTime;
            
            // 디버깅: 나이와 수명을 색상으로 표시
            float lifeRatio = clamp(particle.age / particle.lifeTime, 0.0, 1.0);
            particle.color = float4(lifeRatio, 1.0 - lifeRatio, 0.0, 1.0);
            
            // 수명이 다한 경우 파티클 비활성화
            if (particle.age >= particle.lifeTime)
            {
                particle.isActive = 0;
                
                // 비활성 인덱스 목록에 추가
                uint inactiveIdx;
                InterlockedAdd(gInactiveParticleCount[0], 1, inactiveIdx);
                
                if (inactiveIdx == 0)
                {
                    // 첫 번째 비활성 파티클로 특수 색상 설정 (디버깅용)
                    particle.color = float4(1.0, 0.0, 1.0, 1.0);
                }
                
                // 유효한 인덱스 범위인 경우에만 저장
                if (inactiveIdx < maxParticles)
                {
                    gInactiveParticleIndices[inactiveIdx] = particleIndex;
                }
            }
            else
            {
                // 여전히 활성 상태인 파티클 카운트 증가
                InterlockedAdd(gActiveParticleCounter[0], 1);
            }
        }
        
        // 업데이트된 파티클 데이터 저장
        gParticles[particleIndex] = particle;
    }
}
