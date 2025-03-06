// VS_Wireframe.hlsl
struct VS_INPUT
{
    float3 pos : POSITION; // ���� ��ġ
    float3 normal : NORMAL; // ���� ����
    float2 uv : TEXCOORD; // �ؽ�ó ��ǥ(�� �Ƚ�)
};

cbuffer CameraBuffer : register(b0)
{
    matrix VP;
    float3 CameraPosition;
    float Exposure;
    float3 ViewDir;
    float _placeholder4;
    float3 Up;
    float _placeholder5;
    float3 Right;
    float _placeholder6;
};

cbuffer ModelBuffer : register(b1)
{
    matrix Model;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 1.0f), Model);
    output.pos = mul(output.pos, VP);
    output.color = float4(input.normal, 1);
    return output;
}