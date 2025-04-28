
cbuffer CameraBuffer : register(b0)
{
    matrix VP;
    float3 eyePosition;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.Position = mul(VP, float4(input.Position, 1.0f));
    output.Color = input.Color;
    return output;
}