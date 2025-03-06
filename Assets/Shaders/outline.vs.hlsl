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

cbuffer ModelBuffer : register(b1)
{
    matrix ModelMatrix;
};

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
};

struct Out
{
    float4 vPosition : SV_POSITION;
    float4 vNormalWorld : POSITION0;
};

Out main(in Vertex vertex)
{
    Out output;
    float3x3 mat = (float3x3) ModelMatrix;
    vertex.position += vertex.normal * oulineThickness;
    float4 vPixelWorldPos = mul(float4(vertex.position, 1.0f), ModelMatrix);
    output.vPosition = mul(vPixelWorldPos, VP);
    output.vNormalWorld = float4(mul(vertex.normal, mat), 0.f);
    
    return output;

}