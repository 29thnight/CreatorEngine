#define MAX_BONE_WEIGHTS 4
#define MAX_JOINTS 128

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

cbuffer JointBuffer : register(b1)
{
    matrix Pallete[MAX_JOINTS];
}

cbuffer LightSpaceBuffer : register(b2)
{
    matrix LightSpaceMatrix;
};

cbuffer outlineProperties : register(b3)
{
    float4 outlineColor;
    float oulineThickness;
    uint Bitmask;
};

struct Vertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT0;
    float3 bitangent : TANGENT1;
    float2 texcoord : TEXCOORD0;
    int BoneIndices[MAX_BONE_WEIGHTS] : BONEINDICES;
    float BoneWeights[MAX_BONE_WEIGHTS] : BONEWEIGHTS;
};


struct Out
{
    float4 vPosition : SV_POSITION;
    float4 vNormalWorld : POSITION0;
};

Out main(in Vertex vertex)
{
    Out output;

    float4x4 mat = (float4x4) 0;
	
	[unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (vertex.BoneIndices[i] == -1)
            continue;
        mat += Pallete[vertex.BoneIndices[i]] * vertex.BoneWeights[i];
    }
    
    vertex.position += vertex.normal * oulineThickness;
    float4 vPixelWorldPos = mul(float4(vertex.position, 1.0f), mat);
    output.vPosition = mul(vPixelWorldPos, VP);
    output.vNormalWorld = float4(mul(float4(vertex.normal, 0.f), mat));
    
    return output;

}