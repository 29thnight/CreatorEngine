
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

cbuffer ModelBuffer                      : register(b1)
{
    matrix              ModelMatrix;
};

cbuffer LightSpaceBuffer                     : register(b2)
{
	matrix LightSpaceMatrix;
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
    float3x3 vTBN        	: TANGENT0;     // float3 vTangent         : TANGENT0; // float3 vBiTangent       : TANGENT1;
    float2 vTexcoord        : TEXCOORD0;
    float3 vPixelWorldPos   : POSITION0;
    float4 vPixelLightSpacePos   : POSITION1;
    float4 vPosition        : SV_POSITION;
};


// Vertex Shader

Out main(in Vertex vertex)
{
    Out output;
    // output.vPosition = float4(vertex.position, 1.0f);
	float3x3 mat = (float3x3)ModelMatrix;
    output.vPixelWorldPos = mul(float4(vertex.position, 1.0f), ModelMatrix).xyz;
    output.vPosition = mul(float4(output.vPixelWorldPos, 1.0f), VP);
	
	float3 t = mul(vertex.tangent, mat);
	float3 bt = mul(vertex.bitangent, mat);
	float3 n = mul(vertex.normal, mat);

	output.vTBN = float3x3(t,bt,n);
	
	output.vPixelLightSpacePos = mul(float4(output.vPixelWorldPos, 1.0f), LightSpaceMatrix);
    output.vTexcoord = vertex.texcoord;

    return output;
}