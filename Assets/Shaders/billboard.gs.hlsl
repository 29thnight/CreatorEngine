// Geometry Shader
struct GS_INPUT
{
    float3 Center : POSITION; // 빌보드 중심 좌표
};

struct PS_INPUT
{
    float4 Position : SV_POSITION; // 화면 공간 정점 좌표
    float2 TexCoord : TEXCOORD; // 텍스처 좌표
};

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

cbuffer BillboardSize : register(b1)
{
    float Size;
};

[maxvertexcount(4)] // 빌보드에 필요한 정점 개수
void main(point GS_INPUT input[1], inout TriangleStream<PS_INPUT> triStream)
{  
    float halfWidth = Size * 0.5f;
    
    float3 planeNormal = input[0].Center - CameraPosition;
    planeNormal.y = 0.0f;
    planeNormal = normalize(planeNormal);
    
    float3 upVector = float3(0.0f, 1.0f, 0.0f);
    
    float3 rightVector = normalize(cross(planeNormal, upVector));
    
    rightVector = rightVector * halfWidth;
    
    upVector = float3(0, Size, 0);
    
    float3 vert[4];
    vert[0] = input[0].Center - rightVector;
    vert[1] = input[0].Center + rightVector;
    vert[2] = input[0].Center - rightVector + upVector;
    vert[3] = input[0].Center + rightVector + upVector;
    
    float2 texCoord[4];
    texCoord[0] = float2(0, 1);
    texCoord[1] = float2(1, 1);
    texCoord[2] = float2(0, 0);
    texCoord[3] = float2(1, 0);
    
    PS_INPUT output;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        output.Position = mul(float4(vert[i], 1.0f), VP);
        output.TexCoord = texCoord[i];
        triStream.Append(output);
    }

}
