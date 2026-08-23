// vk.selftest가 실제 scene.glb Mesh의 persistent vertex/index buffer를 그린다.
// 두 축 벡터는 CPU가 mesh bounds의 가장 넓은 두 축을 골라 clip 공간으로 맞춘다.

cbuffer MeshConstants : register(b0)
{
    float4 axisX;
    float4 axisY;
    float4 tint;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOut
{
    float4 position : SV_Position;
};

VSOut VSMain(VSInput input)
{
    const float4 localPosition = float4(input.position, 1.0f);
    VSOut output;
    output.position = float4(dot(localPosition, axisX), dot(localPosition, axisY),
        0.0f, 1.0f);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    return float4(tint.rgb, 1.0f);
}
