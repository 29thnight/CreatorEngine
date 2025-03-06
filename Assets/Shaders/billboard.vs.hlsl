// Vertex Shader
struct VS_INPUT
{
    float3 Center : POSITION; // 빌보드 중심 위치
};

struct VS_OUTPUT
{
    float3 Center : POSITION; // 빌보드 중심 위치 (그대로 전달)
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Center = input.Center;
    return output;
}