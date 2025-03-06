
cbuffer outlineProperties : register(b3)
{
    float4 outlineColor;
    float oulineThickness;
    uint Bitmask;
};

struct In
{
    float4 vPosition : SV_POSITION;
    float4 vNormalWorld : POSITION0;
};

float4 main(In input) : SV_Target
{
    float4 color = outlineColor;
    [branch]
    if (Bitmask & (1 << 4))
    {
        color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    
    return color;
}