struct VS_INPUT
{
    float4 position : POSITION0; // �⺻ ���ؽ� ��ġ
};

struct InstanceData
{
    float4 position : POSITION1; // �ν��Ͻ� ��ġ
    float2 size : TEXCOORD0; // �ν��Ͻ� ũ��
    uint id : TEXCOORD1;
    float4 color : COLOR0;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 size : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    uint id : TEXCOORD2;
    float4 color : COLOR0;
};

VS_OUTPUT main(VS_INPUT input, InstanceData data, uint instanceId : SV_InstanceID)
{
    VS_OUTPUT output;
    
    output.position = data.position;
    output.size = data.size;
    output.worldPos = data.position.xyz;
    output.id = data.id;
    output.color = data.color;
    
    return output;
}