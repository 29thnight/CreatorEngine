// MovementModule.hlsl
// 파티클 이동에 관한 컴퓨트 셰이더

// 파티클 구조체 정의
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

// 상수 버퍼 정의
cbuffer MovementParams : register(b0)
{
    float deltaTime; // 프레임 시간
    float gravityStrength; // 중력 강도
    int useGravity; // 중력 사용 여부
    float padding; // 패딩
};

// 파티클 버퍼 (읽기)
StructuredBuffer<ParticleData> ParticlesInput : register(t0);
// 파티클 버퍼 (쓰기)
RWStructuredBuffer<ParticleData> ParticlesOutput : register(u0);

// 스레드 그룹 크기 정의
#define THREAD_GROUP_SIZE 1024

// 메인 컴퓨트 셰이더 함수
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 스레드 ID가 파티클 배열 크기를 초과하면 종료
    uint particleIndex = DTid.x;
    
    // 파티클 데이터를 입력 버퍼에서 읽기
    ParticleData particle = ParticlesInput[particleIndex];
    
    // 파티클이 활성화된 경우에만 계산
    if (particle.isActive)
    {
        // 정규화된 나이 계산 (이징 적용을 위해)
        float normalizedAge = particle.age / particle.lifeTime;
        
        // 중력 적용 (설정된 경우)
        if (useGravity != 0)
        {
            // 가속도에 중력 강도 적용    
            particle.velocity += particle.acceleration * gravityStrength * deltaTime;
            //particle.color = float4(1, 0, 0, 1);
        }
        // 위치 및 회전 업데이트
        particle.position += particle.velocity * deltaTime;
        particle.rotation += particle.rotatespeed * deltaTime;
        
    }
    
    // 계산된 파티클 데이터를 출력 버퍼에 쓰기
    ParticlesOutput[particleIndex] = particle;
}