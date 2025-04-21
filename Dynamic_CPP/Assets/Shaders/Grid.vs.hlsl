// ��� ����: ī�޶��� ��-�������� ����� �����մϴ�.
cbuffer GridConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix projection;
}

cbuffer cameraPos : register(b1)
{
    float4 cameraPos;
}

// �Է� ���� ����ü: ������ ���� ��ǥ���� ��ġ�� ����
struct VS_INPUT
{
    float3 pos : POSITION;
};

// ��� ����ü: Ŭ�� ��ǥ�� �Բ� ���� ��ǥ�� TEXCOORD0�� �����մϴ�.
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

// Vertex Shader: �Է� ��ġ�� ���� ��ǥ�� �״�� ����ϰ�, viewProj�� ���� Ŭ�� ��ǥ�� ��ȯ�մϴ�.
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    
    int3 cPos = cameraPos;
    cPos.y = 0;
    
    // �Է� ������ ���� �������� ��ȯ
    float4 worldPos = mul(world, float4(input.pos + cPos.xyz, 1.0));
    output.worldPos = worldPos.xyz;
    
    // ���� ��ǥ�� ��, ���������� ���� Ŭ�� �������� ��ȯ
    float4 viewPos = mul(view, worldPos);
    output.pos = mul(projection, viewPos);
    
    return output;
}