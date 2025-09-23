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

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    
    // SRV에서 파티클 데이터 읽기
    ParticleData particle = g_Particles[instanceID];
    
    // 파티클이 활성화되지 않았으면 화면 밖으로 이동
    if (particle.isActive != 1)
    {
        output.Position = float4(2.0, 2.0, 2.0, 0.0);
        output.TexCoord = float2(0, 0);
        output.TexIndex = 0;
        output.Color = float4(0, 0, 0, 0);
        return output;
    }
    
    // 인스턴스 월드 위치 설정
    float3 worldPos = particle.position;
    
    // 회전 적용
    float sinR = sin(particle.rotation);
    float cosR = cos(particle.rotation);
    
    // 기본 정점 위치에 파티클 스케일과 회전 적용
    float2 rotatedPos;
    rotatedPos.x = input.VertexPosition.x * cosR - input.VertexPosition.y * sinR;
    rotatedPos.y = input.VertexPosition.x * sinR + input.VertexPosition.y * cosR;
    
    float3 vertexPos = float3(rotatedPos.x * particle.size.x,
                             rotatedPos.y * particle.size.y,
                             0.0f);
    
    // 정점 위치를 인스턴스 위치에 더함
    worldPos += vertexPos;
    
    // 월드-뷰-투영 변환 적용
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