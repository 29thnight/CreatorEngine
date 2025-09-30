// TrailBillboard.hlsl - 트레일 전용 버텍스 셰이더
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
    
    ParticleData particle = g_Particles[instanceID];
    
    if (particle.isActive != 1)
    {
        output.Position = float4(2.0, 2.0, 2.0, 0.0);
        output.TexCoord = float2(0, 0);
        output.TexIndex = 0;
        output.Color = float4(0, 0, 0, 0);
        output.Age = 0.0;
        return output;
    }
    
    float3 worldPos = particle.position;
    
    float3 cameraRight = normalize(float3(View._11, View._21, View._31));
    float3 cameraUp = normalize(float3(View._12, View._22, View._32));
    float3 cameraForward = normalize(float3(View._13, View._23, View._33));
    
    float speed = length(particle.velocity);
    float3 velocityDir = speed > 0.001 ? normalize(particle.velocity) : float3(0, 1, 0);
    
    float lifeRatio = particle.age / particle.lifeTime;
    float2 currentSize = particle.size * (1.0 - lifeRatio * 0.5);
    float currentAlpha = particle.color.a * (1.0 - lifeRatio * 0.8);
    
    float3 right, up;
    
    if (speed > 0.1)
    {
        float stretchAmount = speed * 0.05;
        
        up = velocityDir;
        right = normalize(cross(cameraForward, up));
        
        if (length(right) < 0.001)
        {
            right = normalize(cross(cameraUp, up));
        }
        
        float3 vertexPos = input.VertexPosition.x * right * currentSize.x +
                          input.VertexPosition.y * up * (currentSize.y + stretchAmount);
        
        worldPos += vertexPos;
    }
    else
    {
        float sinR = sin(particle.rotation);
        float cosR = cos(particle.rotation);
        float3 rotatedRight = cameraRight * cosR + cameraUp * sinR;
        float3 rotatedUp = -cameraRight * sinR + cameraUp * cosR;
        
        float3 vertexPos = input.VertexPosition.x * rotatedRight * currentSize.x +
                          input.VertexPosition.y * rotatedUp * currentSize.y;
        
        worldPos += vertexPos;
    }
    
    float4 positionW = mul(float4(worldPos, 1.0f), World);
    float4 positionV = mul(positionW, View);
    float4 positionP = mul(positionV, Projection);
    
    output.Position = positionP;
    output.TexCoord = input.TexCoord;
    output.TexIndex = instanceID;
    output.Color = float4(particle.color.rgb, currentAlpha);
    output.Age = particle.age;
    
    return output;
}