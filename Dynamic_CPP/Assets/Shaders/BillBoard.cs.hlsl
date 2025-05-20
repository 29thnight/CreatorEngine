// BillboardCompute.hlsl
struct BillboardInput
{
    float3 Position;
    float Padding1;
    float2 Scale;
    uint TexIndex;
    float Padding2;
    float4 Color;
};

struct VertexOutput
{
    float4 Position;
    float2 TexCoord;
    uint TexIndex;
    float Padding;
    float4 Color;
};

cbuffer CameraConstants : register(b0)
{
    matrix View;
    matrix Projection;
    float3 CameraRight;
    float Padding;
    float3 CameraUp;
    uint BillboardCount;
};

// ������ �Է� ������
StructuredBuffer<BillboardInput> InputBillboards : register(t0);

// ��� ���� (��ȯ�� ����)
RWStructuredBuffer<VertexOutput> OutputVertices : register(u0);

// ������ ���̺� (�� ������ ���� ��ǥ�� �ؽ�ó ��ǥ)
static const float2 QuadPositions[4] =
{
    float2(-1, -1), // ���ϴ�
    float2(1, -1), // ���ϴ� 
    float2(-1, 1), // �»��
    float2(1, 1) // ����
};

static const float2 QuadTexCoords[4] =
{
    float2(0, 1), // ���ϴ�
    float2(1, 1), // ���ϴ�
    float2(0, 0), // �»��
    float2(1, 0) // ����
};

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint billboardIndex = DTid.x;
    
    // �ε����� ������ ����� ����
    if (billboardIndex >= BillboardCount)
        return;
    
    // ������ ������ ��������
    BillboardInput billboard = InputBillboards[billboardIndex];
    
    // 4���� ���� ó�� (������ �� ����)
    for (int i = 0; i < 4; i++)
    {
        // ������ ���� ��ǥ ��������
        float2 localPos = QuadPositions[i];
        
        // ������ ��ġ ���
        float3 worldPos = billboard.Position;
        worldPos += CameraRight * localPos.x * billboard.Scale.x;
        worldPos += CameraUp * localPos.y * billboard.Scale.y;
        
        // ����-��-���� ��ȯ
        float4 clipPos = mul(mul(float4(worldPos, 1.0f), View), Projection);
        
        // ��� ���� (������ �� 4���� ����)
        uint vertexIndex = billboardIndex * 4 + i;
        
        VertexOutput vertex;
        vertex.Position = clipPos;
        vertex.TexCoord = QuadTexCoords[i];
        vertex.Padding = 0;
        vertex.TexIndex = billboard.TexIndex;
        vertex.Color = billboard.Color;
        
        OutputVertices[vertexIndex] = vertex;
    }
}