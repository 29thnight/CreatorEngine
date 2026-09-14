cbuffer GizmoCamera : register(b0)
{
    float4x4 gViewProjection;
    float4   gEyePosition;
};

struct VSIn
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.position = mul(float4(input.position, 1.0f), gViewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
