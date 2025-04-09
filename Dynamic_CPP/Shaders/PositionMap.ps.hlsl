struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localPos : TEXCOORD0; // ���� ������ ��ǥ ����
};

float4 main(PS_INPUT input) : SV_Target
{
    //return float4(0, 1, 0, 1);
    return float4(input.localPos, 1.0f); // ���� ��ǥ ����
}