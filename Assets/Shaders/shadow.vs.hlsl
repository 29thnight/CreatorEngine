cbuffer LightSpaceBuffer                     : register(b0)
{
	matrix LightSpaceMatrix;
};

cbuffer ModelBuffer                     	: register(b1)          
{
	matrix Model;
};

struct Vertex
{
	float3	position        : POSITION;
	float3	normal          : NORMAL;
	float3	tangent         : TANGENT0;
	float3	bitangent       : TANGENT1;
	float2	texcoord        : TEXCOORD0;
};

struct Out
{
    float4 vPosition        : SV_POSITION;
};

Out main(Vertex input)
{
	Out output;
	output.vPosition = mul(float4(input.position, 1.0f), Model);
	output.vPosition = mul(output.vPosition, LightSpaceMatrix);

	return output;
}