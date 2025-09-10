// MeshColorModule.hlsl - 3D 메시 파티클용 색상 모듈

#define GRADIENT_MODE 0
#define DISCRETE_MODE 1
#define CUSTOM_FUNCTION_MODE 2

#define PULSE_FUNCTION 0
#define SINE_WAVE_FUNCTION 1
#define FLICKER_FUNCTION 2
#define LINEAR_FUNCTION 3
#define EXPONENTIAL_FUNCTION 4

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

cbuffer ColorParams : register(b0)
{
    float deltaTime;
    int transitionMode;
    int gradientSize;
    int discreteColorsSize;
    
    int customFunctionType;
    float customParam1;
    float customParam2;
    float customParam3;
    
    float customParam4;
    uint maxParticles;
    float2 padding;
};

struct GradientPoint
{
    float time;
    float4 color;
};

StructuredBuffer<MeshParticleData> InputParticles : register(t0);
StructuredBuffer<GradientPoint> GradientBuffer : register(t1);
StructuredBuffer<float4> DiscreteColors : register(t2);

RWStructuredBuffer<MeshParticleData> OutputParticles : register(u0);

float4 EvaluateGradient(float t)
{
    if (gradientSize <= 0)
        return float4(1, 1, 1, 1);
    
    if (t <= GradientBuffer[0].time)
        return GradientBuffer[0].color;
    if (t >= GradientBuffer[gradientSize - 1].time)
        return GradientBuffer[gradientSize - 1].color;
    
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

float4 EvaluateDiscrete(float t)
{
    if (discreteColorsSize <= 0)
        return float4(1, 1, 1, 1);
    
    int index = (int) (t * discreteColorsSize);
    index = clamp(index, 0, discreteColorsSize - 1);
    return DiscreteColors[index];
}

float4 EvaluateCustomFunction(float t, float totalTime)
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
        
        default:
            return float4(1, 1, 1, 1);
    }
}

float4 EvaluateColor(float t, float totalTime)
{
    switch (transitionMode)
    {
        case GRADIENT_MODE:
            return EvaluateGradient(t);
            
        case DISCRETE_MODE:
            return EvaluateDiscrete(t);
            
        case CUSTOM_FUNCTION_MODE:
            return EvaluateCustomFunction(t, totalTime);
            
        default:
            return float4(1, 1, 1, 1);
    }
}

#define THREAD_GROUP_SIZE 1024

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    
    if (particleIndex >= maxParticles)
        return;
    
    MeshParticleData particle = InputParticles[particleIndex];
    
    if (particle.isActive)
    {
        float normalizedAge = saturate(particle.age / max(particle.lifeTime, 0.001f));
        float totalTime = particle.age;
        
        particle.color = EvaluateColor(normalizedAge, totalTime);
    }
    
    OutputParticles[particleIndex] = particle;
}