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
    float3 localPos : TEXCOORD0; // 월드 공간의 좌표 전달
    float3 localNor : TEXCOORD1; // 월드 공간의 노멀 좌표 전달
};

cbuffer posBuffer : register(b0)
{
    int posMapWidth;
    int posMapHeight;
};

PS_INPUT main(VS_Input input)
{
    PS_INPUT output;
    
    float2 texelSize = float2(1.0 / posMapWidth, 1.0 / posMapHeight);
    
    float2 uv = input.texCoord;// + texelSize * 0.5;
    
    //output.position = float4(input.texCoord * 2.0 - 1.0, 0, 1);
    output.position = float4((uv.x - 0.5f) * 2, -(uv.y - 0.5f) * 2, 0, 1);
    // 월드 좌표 전달
    output.localPos = input.position;
    output.localNor = input.normal;

    return output;
}