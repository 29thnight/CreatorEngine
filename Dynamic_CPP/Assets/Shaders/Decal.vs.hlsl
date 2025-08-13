cbuffer PerObject : register(b0)
{
    matrix model;
}

cbuffer PerFrame : register(b1)
{
    matrix view;
}

cbuffer PerApplication : register(b2)
{
    matrix projection;
}

// ���� �Է� ����ü
struct VS_INPUT
{
    float3 position : POSITION;
};

// �����׸�Ʈ ���̴��� ���޵� ����ü
struct VS_OUTPUT
{
    float4 position : SV_Position; // Ŭ�� ���� ��ǥ
};

// ���� ���̴� ���� �Լ�
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output = (VS_OUTPUT) 0;

    // ����-��-�������� ��ȯ
    matrix vp = mul(projection, view);
    matrix wvpMatrix = mul(vp, model);

    // ���� ��ġ�� Ŭ�� �������� ��ȯ
    output.position = mul(wvpMatrix, float4(input.position, 1.0f));

    return output;
}