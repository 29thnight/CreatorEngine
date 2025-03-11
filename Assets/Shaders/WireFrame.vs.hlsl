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

cbuffer CameraPos : register(b3)
{
    float3 cameraPos;
}

struct AppData
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 boneIds : BLENDINDICES;
    float4 boneWeight : BLENDWEIGHT;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 cameraDir : TEXCOORD0;
    float3 noraml : TEXCOORD1;
};

VertexShaderOutput main(AppData IN)
{
    VertexShaderOutput OUT;
    
    matrix vp = mul(projection, view);
    OUT.position = mul(float4(IN.position, 1.0f), model);
    OUT.position = mul(OUT.position, vp);
    OUT.color = float4(0, 1, 0, 1);
    OUT.noraml = IN.normal;
    OUT.cameraDir = normalize(cameraPos - OUT.position.xyz);
    
    return OUT;
}
