// Vertex Shader
struct VS_INPUT
{
    float3 Center : POSITION; // ������ �߽� ��ġ
};

struct VS_OUTPUT
{
    float3 Center : POSITION; // ������ �߽� ��ġ (�״�� ����)
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Center = input.Center;
    return output;
}