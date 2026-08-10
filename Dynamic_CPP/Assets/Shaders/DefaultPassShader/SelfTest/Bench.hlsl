cbuffer PerDraw : register(b0)
{
    row_major float4x4 gWorld;
    float4             gColor;
};

struct VSIn  { float3 position : POSITION; };
struct VSOut { float4 position : SV_POSITION; float4 color : COLOR0; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.position = mul(float4(input.position, 1.0f), gWorld);
    output.color = gColor;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
