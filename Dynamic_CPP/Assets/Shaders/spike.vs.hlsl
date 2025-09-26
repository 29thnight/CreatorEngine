// NewBillBoard.hlsl
cbuffer ModelConstantBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
}

struct VSInput
{
    float4 VertexPosition : POSITION0;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
    float Age : TEXCOORD2;
};

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

StructuredBuffer<ParticleData> g_Particles : register(t0);

// NewBillBoard.hlsl에서 이 부분만 수정

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    
    ParticleData particle = g_Particles[instanceID];
    
    if (particle.isActive != 1)
    {
        output.Position = float4(2.0, 2.0, 2.0, 0.0);
        output.TexCoord = float2(0, 0);
        output.TexIndex = 0;
        output.Color = float4(0, 0, 0, 0);
        return output;
    }
    
    float3 worldPos = particle.position;
    
    float3 cameraRight = normalize(float3(View._11, View._21, View._31));
    float3 cameraUp = normalize(float3(View._12, View._22, View._32));
    
    // velocity 방향으로 rotation 계산 추가
    float finalRotation = particle.rotation;
    if (length(particle.velocity) > 0.001)
    {
        float3 velocityWorld = normalize(particle.velocity);
        
        // velocity를 카메라 평면에 투영하여 2D 방향 구하기
        float rightComponent = dot(velocityWorld, cameraRight);
        float upComponent = dot(velocityWorld, cameraUp);
        
        // 텍스처 왼쪽이 velocity 방향을 향하도록 (90도 회전 추가)
        finalRotation = atan2(upComponent, rightComponent) - 3.14159265359; // -180도
    }
    
    // 회전 적용
    float sinR = sin(finalRotation);
    float cosR = cos(finalRotation);
    float3 rotatedRight = cameraRight * cosR + cameraUp * sinR;
    float3 rotatedUp = -cameraRight * sinR + cameraUp * cosR;
    
    float3 vertexPos = input.VertexPosition.x * rotatedRight * particle.size.x +
                   input.VertexPosition.y * rotatedUp * particle.size.y;
    
    worldPos += vertexPos;
    
    float4 positionW = mul(float4(worldPos, 1.0f), World);
    float4 positionV = mul(positionW, View);
    float4 positionP = mul(positionV, Projection);
    
    output.Position = positionP;
    output.TexCoord = input.TexCoord;
    output.TexIndex = 0;
    output.Color = particle.color;
    output.Age = particle.age;
    
    return output;
}