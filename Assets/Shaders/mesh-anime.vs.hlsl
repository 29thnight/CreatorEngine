#define MAX_BONE_WEIGHTS 4
#define MAX_JOINTS 128

cbuffer CameraBuffer                     : register(b0)
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
//may_be_unused
cbuffer ModelBuffer : register(b1)
{
    matrix  ModelMatrix;
};

cbuffer LightSpaceBuffer                     : register(b2)
{
	matrix LightSpaceMatrix;
};

cbuffer JointBuffer : register(b3)
{
    matrix Pallete[MAX_JOINTS];
}

struct Vertex
{
	float3	position        : POSITION;
	float3	normal          : NORMAL;
	float3	tangent         : TANGENT0;
	float3	bitangent       : TANGENT1;
	float2	texcoord        : TEXCOORD0;
    int BoneIndices[MAX_BONE_WEIGHTS] : BONEINDICES;
    float BoneWeights[MAX_BONE_WEIGHTS] : BONEWEIGHTS;
};

struct Out
{
    float3x3 vTBN        	: TANGENT0;     // float3 vTangent         : TANGENT0; // float3 vBiTangent       : TANGENT1;
    float2 vTexcoord        : TEXCOORD0;
    float3 vPixelWorldPos   : POSITION0;
    float4 vPixelLightSpacePos   : POSITION1;
    float4 vPosition        : SV_POSITION;
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
	
    output.vPixelWorldPos = mul(float4(vertex.position, 1), mat).xyz;
    output.vPosition = mul(float4(output.vPixelWorldPos, 1.0f), VP);
	
    float3 t = mul(vertex.tangent, (float3x3) mat);
    float3 bt = mul(vertex.bitangent, (float3x3) mat);
    float3 n = mul(vertex.normal, (float3x3) mat);

	output.vTBN = float3x3(t,bt,n);
	
	output.vPixelLightSpacePos = mul(float4(output.vPixelWorldPos, 1.0f), LightSpaceMatrix);
    output.vTexcoord = vertex.texcoord;

    return output;
}