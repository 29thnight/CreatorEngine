// ColorModule.hlsl

// ���� ��ȯ ���
#define GRADIENT_MODE 0
#define DISCRETE_MODE 1
#define CUSTOM_FUNCTION_MODE 2

// ����� ���� �Լ� Ÿ��
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

// C++�� ��ġ�ϴ� ��� ���� ����
cbuffer ColorParams : register(b0)
{
    float deltaTime; // ��Ÿ Ÿ��
    int transitionMode; // ���� ��ȯ ���
    int gradientSize; // �׶��̼� ����Ʈ ����
    int discreteColorsSize; // �̻� ���� ����
    
    int customFunctionType; // Ŀ���� �Լ� Ÿ��
    float customParam1; // Ŀ���� �Ķ���� 1
    float customParam2; // Ŀ���� �Ķ���� 2
    float customParam3; // Ŀ���� �Ķ���� 3
    
    float customParam4; // Ŀ���� �Ķ���� 4
    uint maxParticles; // �ִ� ��ƼŬ ��
    float2 padding; // 16����Ʈ ������ ���� �е�
};

// C++�� ��ġ�ϴ� �׶��̼� ����Ʈ ����ü
struct GradientPoint
{
    float time;
    float4 color;
};

// �Է�/��� ����
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

// �׶��̼� ���� ��
float4 EvaluateGradient(float t)
{
    if (gradientSize <= 0)
        return float4(1, 1, 1, 1);
    
    // ��谪 ó��
    if (t <= GradientBuffer[0].time)
        return GradientBuffer[0].color;
    if (t >= GradientBuffer[gradientSize - 1].time)
        return GradientBuffer[gradientSize - 1].color;
    
    // �ش� ���� ã��
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

// �̻� ���� ��
float4 EvaluateDiscrete(float t)
{
    if (discreteColorsSize <= 0)
        return float4(1, 1, 1, 1);
    
    int index = (int) (t * discreteColorsSize);
    index = clamp(index, 0, discreteColorsSize - 1);
    return DiscreteColors[index];
}

// ����� ���� �Լ���
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
                // ��ƼŬ���� ���� Ǯ���� �ϳ��� ���� ����
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

// ���� ���� �� �Լ�
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
    
    // maxParticles�� ����� ������ ���� Ȯ��
    if (particleIndex >= maxParticles)
        return;
    
    // �Է� ��ƼŬ ������ �ε�
    ParticleData particle = InputParticles[particleIndex];
    
    // Ȱ�� ��ƼŬ�� ó��
    if (particle.isActive)
    {
        // ����ȭ�� ���� ��� (0.0 ~ 1.0)
        float normalizedAge = saturate(particle.age / max(particle.lifeTime, 0.001f));
        
        // �� �ð� ��� (��ü �ý��� �ð�, �ִϸ��̼ǿ�)
        float totalTime = particle.age;
        
        // ���� ���
        particle.color = EvaluateColor(normalizedAge, totalTime, particleIndex);
    }
    
    // ��� ����
    OutputParticles[particleIndex] = particle;
}