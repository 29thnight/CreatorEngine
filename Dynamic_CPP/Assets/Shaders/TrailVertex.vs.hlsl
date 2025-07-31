// TrailVertex.hlsl - ������ Ʈ���� ���� ���̴�
cbuffer TrailConstantBuffer : register(b0)
{
    matrix g_world;
    matrix g_view;
    matrix g_projection;
    float3 g_cameraPosition;
    float g_time; // padding ��� �ð� �� Ȱ��
};

struct VertexInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
    float depth : TEXCOORD3; // ���� ��� ȿ����
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    // ����-��-�������� ��ȯ�� �ѹ���
    float4 worldPos = mul(float4(input.position, 1.0f), g_world);
    float4 viewPos = mul(worldPos, g_view);
    output.position = mul(viewPos, g_projection);
    
    output.worldPos = worldPos.xyz;
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    // ���� ��ȯ (������ ���)
    output.normal = normalize(mul(input.normal, (float3x3) g_world));
    
    // �� ����
    output.viewDir = normalize(g_cameraPosition - worldPos.xyz);
    
    // ���� ���� (0~1)
    output.depth = output.position.z / output.position.w;
    
    return output;
}
