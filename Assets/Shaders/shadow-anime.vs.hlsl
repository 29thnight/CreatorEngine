#define MAX_BONE_WEIGHTS 4
#define MAX_JOINTS 128

cbuffer LightSpaceBuffer                     : register(b0)
{
	matrix LightSpaceMatrix;
};

cbuffer ModelBuffer                     	: register(b1)          
{
	matrix Model;
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
    float4 vPosition        : SV_POSITION;
};

Out main(Vertex input)
{
	Out output;
    float4x4 mat = (float4x4) 0;
	
	[unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (input.BoneIndices[i] == -1)
            continue;
        mat += Pallete[input.BoneIndices[i]] * input.BoneWeights[i];
    }
	
    output.vPosition = mul(float4(input.position, 1), mat);
	output.vPosition = mul(output.vPosition, LightSpaceMatrix);

	return output;
}