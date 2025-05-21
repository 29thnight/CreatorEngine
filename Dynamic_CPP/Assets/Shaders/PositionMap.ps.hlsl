struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localPos : TEXCOORD0; // ���� ������ ��ǥ ����
    float3 localNor : TEXCOORD1; // ���� ������ ��� ��ǥ ����
};

struct Output
{
    float4 positionMap : SV_TARGET0;
    float4 normalMap : SV_TARGET1;
};

Output main(PS_INPUT input) : SV_Target
{
    //return float4(0, 1, 0, 1);
    //return float4(input.localPos, 1.0f); // ���� ��ǥ ����
    
    Output output;
    output.positionMap = float4(input.localPos, 1.0f); // ���� ��ǥ ����
    output.normalMap = float4(input.localNor, 1.0f); // ���� ��� ����
    return output;
}