// ColorModule.hlsl

// 색상 전환 모드
#define GRADIENT_MODE 0
#define DISCRETE_MODE 1
#define CUSTOM_FUNCTION_MODE 2

// 사용자 정의 함수 타입
#define PULSE_FUNCTION 0
#define SINE_WAVE_FUNCTION 1
#define FLICKER_FUNCTION 2
#define RANDOM_COLOR_FUNCTION 3
#define EXPONENTIAL_FUNCTION 4
#define LINEAR_FUNCTION 5

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

// C++와 일치하는 상수 버퍼 구조
cbuffer ColorParams : register(b0)
{
    float deltaTime; // 델타 타임
    int transitionMode; // 색상 전환 모드
    int gradientSize; // 그라데이션 포인트 개수
    int discreteColorsSize; // 이산 색상 개수
    
    int customFunctionType; // 커스텀 함수 타입
    float customParam1; // 커스텀 파라미터 1
    float customParam2; // 커스텀 파라미터 2
    float customParam3; // 커스텀 파라미터 3
    
    float customParam4; // 커스텀 파라미터 4
    uint maxParticles; // 최대 파티클 수
    float2 padding; // 16바이트 정렬을 위한 패딩
};

// C++와 일치하는 그라데이션 포인트 구조체
struct GradientPoint
{
    float time;
    float4 color;
};

// 입력/출력 버퍼
StructuredBuffer<ParticleData> InputParticles : register(t0);
StructuredBuffer<GradientPoint> GradientBuffer : register(t1);
StructuredBuffer<float4> DiscreteColors : register(t2);

RWStructuredBuffer<ParticleData> OutputParticles : register(u0);

float Hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) * (1.0f / 4294967296.0f);
}

// 그라데이션 색상 평가
float4 EvaluateGradient(float t)
{
    if (gradientSize <= 0)
        return float4(1, 1, 1, 1);
    
    // 경계값 처리
    if (t <= GradientBuffer[0].time)
        return GradientBuffer[0].color;
    if (t >= GradientBuffer[gradientSize - 1].time)
        return GradientBuffer[gradientSize - 1].color;
    
    // 해당 구간 찾기
    for (int i = 0; i < gradientSize - 1; i++)
    {
        if (t >= GradientBuffer[i].time && t <= GradientBuffer[i + 1].time)
        {
            float localT = (t - GradientBuffer[i].time) /
                          (GradientBuffer[i + 1].time - GradientBuffer[i].time);
            return lerp(GradientBuffer[i].color, GradientBuffer[i + 1].color, localT);
        }
    }
    
    return GradientBuffer[0].color;
}

// 이산 색상 평가
float4 EvaluateDiscrete(float t)
{
    if (discreteColorsSize <= 0)
        return float4(1, 1, 1, 1);
    
    int index = (int) (t * discreteColorsSize);
    index = clamp(index, 0, discreteColorsSize - 1);
    return DiscreteColors[index];
}

// 사용자 정의 함수들
float4 EvaluateCustomFunction(float t, float totalTime, uint particleIndex)
{
    switch (customFunctionType)
    {
        case PULSE_FUNCTION:
        {
                if (discreteColorsSize >= 2)
                {
                    float pulseValue = sin(totalTime * customParam4) * 0.5 + 0.5;
                    return lerp(DiscreteColors[0], DiscreteColors[1], pulseValue);
                }
                return float4(1, 1, 1, 1);
            }
        
        case SINE_WAVE_FUNCTION:
        {
                if (discreteColorsSize >= 2)
                {
                    float phase = customParam2;
                    float frequency = customParam1;
                    float sineValue = sin(t * frequency + phase + totalTime) * 0.5 + 0.5;
                    return lerp(DiscreteColors[0], DiscreteColors[1], sineValue);
                }
                return float4(1, 1, 1, 1);
            }
        
        case FLICKER_FUNCTION:
        {
                if (discreteColorsSize > 0)
                {
                    float speed = customParam1;
                    float flickerTime = totalTime * speed;
                    int colorIndex = (int) (flickerTime) % discreteColorsSize;
                    return DiscreteColors[colorIndex];
                }
                return float4(1, 1, 1, 1);
            }
        
        case LINEAR_FUNCTION:
        {
                float4 targetColor = float4(customParam1, customParam2, customParam3, customParam4);
                return lerp(float4(1, 1, 1, 1), targetColor, t);
            }
        
        case EXPONENTIAL_FUNCTION:
        {
                float expValue = 1.0 - exp(-customParam1 * t);
                float4 targetColor = float4(customParam2, customParam3, customParam4, 1.0);
                return lerp(float4(1, 1, 1, 1), targetColor, expValue);
            }
        
        case RANDOM_COLOR_FUNCTION:
        {
                if (discreteColorsSize > 0)
                {
                // 파티클별로 색상 풀에서 하나씩 랜덤 선택
                    uint seed = particleIndex * 2654435761u;
                    int colorIndex = (int) (Hash(seed) * discreteColorsSize);
                    colorIndex = clamp(colorIndex, 0, discreteColorsSize - 1);
                    return DiscreteColors[colorIndex];
                }
                return float4(1, 1, 1, 1);
            }
        
        default:
            return float4(1, 1, 1, 1);
    }
}

// 메인 색상 평가 함수
float4 EvaluateColor(float t, float totalTime, uint particleIndex)
{
    switch (transitionMode)
    {
        case GRADIENT_MODE:
            return EvaluateGradient(t);
            
        case DISCRETE_MODE:
            return EvaluateDiscrete(t);
            
        case CUSTOM_FUNCTION_MODE:
            return EvaluateCustomFunction(t, totalTime, particleIndex);
            
        default:
            return float4(1, 1, 1, 1);
    }
}

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    
    // maxParticles를 사용한 안전한 범위 확인
    if (particleIndex >= maxParticles)
        return;
    
    // 입력 파티클 데이터 로드
    ParticleData particle = InputParticles[particleIndex];
    
    // 활성 파티클만 처리
    if (particle.isActive)
    {
        // 정규화된 나이 계산 (0.0 ~ 1.0)
        float normalizedAge = saturate(particle.age / max(particle.lifeTime, 0.001f));
        
        // 총 시간 계산 (전체 시스템 시간, 애니메이션용)
        float totalTime = particle.age;
        
        // 색상 계산
        particle.color = EvaluateColor(normalizedAge, totalTime, particleIndex);
    }
    
    // 결과 저장
    OutputParticles[particleIndex] = particle;
}