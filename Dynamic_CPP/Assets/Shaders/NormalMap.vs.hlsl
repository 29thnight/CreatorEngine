struct VS_Input
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 boneIds : BLENDINDICES;
    float4 boneWeight : BLENDWEIGHT;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localNor : TEXCOORD0; // 월드 공간의 좌표 전달
};

PS_INPUT main(VS_Input input)
{
    PS_INPUT output;
    //output.position = float4(input.texCoord * 2.0 - 1.0, 0, 1);
    output.position = float4((input.texCoord.x - 0.5f) * 2, -(input.texCoord.y - 0.5f) * 2, 0, 1);
    // 월드 좌표 전달
    output.localNor = input.normal;

    return output;
}