// TrailSpawnModule.hlsl - 트레일용 간단한 스폰 모듈

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
    float rotateSpeed;
    float2 pad4;
    float4 color;
    uint isActive;
    float3 pad5;
};

struct TrailSegment
{
    uint sourceParticleID;
    float pad1;
    float pad2;
    float pad3;
    float3 previousPosition;
    float segmentAge;
};

cbuffer TrailSpawnParameters : register(b0)
{
    float gDeltaTime;
    float gCurrentTime;
    uint gMaxTrailParticles;
    float gTrailLifetime;
    
    uint gSourceParticleCount;
    float gMinDistance;
    float2 gSize;
    
    float4 gColor;
}

StructuredBuffer<ParticleData> gSourceParticles : register(t0);
RWStructuredBuffer<ParticleData> gTrailParticles : register(u0);
RWStructuredBuffer<TrailSegment> gTrailData : register(u1);

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint trailIdx = DTid.x;
    
    if (trailIdx >= gMaxTrailParticles)
        return;
    
    ParticleData trail = gTrailParticles[trailIdx];
    TrailSegment data = gTrailData[trailIdx];
    
    if (trail.isActive == 1)
    {
        trail.age += gDeltaTime;
        
        if (trail.age >= trail.lifeTime)
        {
            trail.isActive = 0;
        }
    }
    
    uint sourceIdx = trailIdx % gSourceParticleCount;
    
    if (sourceIdx < gSourceParticleCount && gSourceParticleCount > 0)
    {
        ParticleData source = gSourceParticles[sourceIdx];
        
        if (source.isActive == 1)
        {
            float moveDistance = length(source.position - data.previousPosition);
            
            if (moveDistance >= gMinDistance || data.sourceParticleID != sourceIdx)
            {
                if (trail.isActive == 0)
                {
                    trail.position = source.position;
                    trail.velocity = source.velocity;
                    trail.acceleration = float3(0, 0, 0);
                    trail.size = gSize;
                    trail.age = 0.0;
                    trail.lifeTime = gTrailLifetime;
                    trail.rotation = 0.0;
                    trail.rotateSpeed = 0.0;
                    trail.color = gColor;
                    trail.isActive = 1;
                    
                    data.previousPosition = source.position;
                    data.sourceParticleID = sourceIdx;
                }
            }
        }
    }
    
    gTrailParticles[trailIdx] = trail;
    gTrailData[trailIdx] = data;
}