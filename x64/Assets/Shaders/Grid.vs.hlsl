// ��� ����: ī�޶��� ��-�������� ����� �����մϴ�.
cbuffer GridConstantBuffer : register(b0)
{
    float4x4 viewProj;
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
    float4 worldPos = float4(input.pos, 1.0);
    output.worldPos = input.pos;
    output.pos = mul(worldPos, viewProj);
    return output;
}