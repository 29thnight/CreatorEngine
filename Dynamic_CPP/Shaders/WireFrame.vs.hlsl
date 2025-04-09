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

cbuffer BoneTransformation : register(b3)
{
    matrix BoneTransforms[512];
}

cbuffer CameraPos : register(b4)
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
    float3 pos : TEXCOORD2;
};

VertexShaderOutput main(AppData IN)
{
    VertexShaderOutput OUT;
    
    if (IN.boneWeight[0] > 0)
    {
        matrix boneTransform = IN.boneWeight[0] * BoneTransforms[IN.boneIds[0]];
        for (int i = 1; i < 4; ++i)
        {
            boneTransform += IN.boneWeight[i] * BoneTransforms[IN.boneIds[i]];
        }
        IN.position = mul(boneTransform, float4(IN.position, 1.0f));
    }
    
    matrix vp = mul(projection, view);
    OUT.pos = mul(model, float4(IN.position, 1.0f));
    OUT.position = mul(vp, float4(OUT.pos, 1.0f));
    OUT.pos = OUT.position.xyz;
    OUT.color = float4(0, 1, 0, 1);
    OUT.noraml = IN.normal;
    OUT.cameraDir = normalize(cameraPos - OUT.pos.xyz);
    
    return OUT;
}
