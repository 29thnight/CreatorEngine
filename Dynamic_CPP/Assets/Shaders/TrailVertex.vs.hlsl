// TrailVertex.hlsl - Ʈ���� ���� ���̴�

// ��� ����
cbuffer TrailConstantBuffer : register(b0)
{
    matrix g_world;
    matrix g_view;
    matrix g_projection;
    float3 g_cameraPosition;
    float g_padding;
};

// �Է� ����ü (TrailVertex�� ��ġ)
struct VertexInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 normal : NORMAL;
};

// ��� ����ü
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    // ���� �������� ��ȯ
    float4 worldPos = mul(float4(input.position, 1.0f), g_world);
    output.worldPos = worldPos.xyz;
    
    // �� �������� ��ȯ
    float4 viewPos = mul(worldPos, g_view);
    
    // �������� �������� ��ȯ
    output.position = mul(viewPos, g_projection);
    
    // �ؽ�ó ��ǥ ����
    output.texcoord = input.texcoord;
    
    // ���� ����
    output.color = input.color;
    
    // ������ ���� �������� ��ȯ
    output.normal = normalize(mul(input.normal, (float3x3) g_world));
    
    // �� ���� ��� (ī�޶󿡼� ��������)
    output.viewDir = normalize(g_cameraPosition - worldPos.xyz);
    
    return output;
}