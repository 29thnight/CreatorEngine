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
float3x3 CreateRotationMatrixX(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3x3(
        1, 0, 0,
        0, c, -s,
        0, s, c
    );
}

float3x3 CreateRotationMatrixY(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3x3(
        c, 0, s,
        0, 1, 0,
        -s, 0, c
    );
}

float3x3 CreateRotationMatrixZ(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3x3(
        c, -s, 0,
        s, c, 0,
        0, 0, 1
    );
}

float3x3 CreateRotationMatrix(float3 rotation)
{
    float3x3 rotX = CreateRotationMatrixX(rotation.x);
    float3x3 rotY = CreateRotationMatrixY(rotation.y);
    float3x3 rotZ = CreateRotationMatrixZ(rotation.z);
    
    return mul(mul(rotZ, rotY), rotX);
}
StructuredBuffer<ParticleData> g_Particles : register(t0);

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    
    // SRV���� ��ƼŬ ������ �б�
    ParticleData particle = g_Particles[instanceID];
    
    // ��ƼŬ�� Ȱ��ȭ���� �ʾ����� ȭ�� ������ �̵�
    if (particle.isActive != 1)
    {
        output.Position = float4(2.0, 2.0, 2.0, 0.0); // ȭ�� ������
        output.TexCoord = float2(0, 0);
        output.TexIndex = 0;
        output.Color = float4(0, 0, 0, 0);
        return output;
    }
    
    // �ν��Ͻ� ���� ��ġ ����
    float3 worldPos = particle.position;
    
    // ī�޶� ���� ���� (�� ��Ȯ�� ������ ����� ����)
    float3 cameraRight = normalize(float3(View._11, View._21, View._31));
    float3 cameraUp = normalize(float3(View._12, View._22, View._32));
    
    float3x3 rotMatrix = CreateRotationMatrix(normalize(float3(View._13, View._23, View._33)));
    
    float rot = particle.rotation;
    if (length(particle.velocity) > 0.001)
    {
        float3 dir = normalize(particle.velocity);
        
        float4 originDir = mul(float4(worldPos + dir, 1.f), World);
        originDir = mul(originDir, View);
        originDir = mul(originDir, Projection);
        
        float4 tempDir = mul(float4(worldPos, 1.f), World);
        tempDir = mul(tempDir, View);
        tempDir = mul(tempDir, Projection);
        
        originDir.xyz /= originDir.w;
        tempDir.xyz /= tempDir.w;
        
        float2 dirScreen = normalize(originDir.xy - tempDir.xy);
        
        //dir = mul(dir, rotMatrix);
        // 2D �����忡�� velocity �������� ȸ�� (XZ ��� ����)
        rot = atan2(dirScreen.y, dirScreen.x);
    }
    
    // ȸ�� ����
    float sinR = sin(rot);
    float cosR = cos(rot);
    float3 rotatedRight = cameraRight * cosR + cameraUp * sinR;
    float3 rotatedUp = -cameraRight * sinR + cameraUp * cosR;
    
    // �⺻ ���� ��ġ�� ��ƼŬ ������ ����
    float3 vertexPos = input.VertexPosition.x * rotatedRight * particle.size.x +
                   input.VertexPosition.y * rotatedUp * particle.size.y;
    
    // ���� ��ġ�� �ν��Ͻ� ��ġ�� ����
    worldPos += vertexPos;
    
    // ����-��-���� ��ȯ ���� (���� ����)
    float4 positionW = mul(float4(worldPos, 1.0f), World);
    float4 positionV = mul(positionW, View);
    float4 positionP = mul(positionV, Projection);
    
    output.Position = positionP;
    output.TexCoord = input.TexCoord;
    output.TexIndex = instanceID;
    output.Color = particle.color;
    output.Age = particle.age;
    
    return output;
}