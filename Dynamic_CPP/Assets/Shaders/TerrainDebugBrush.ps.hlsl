struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float4 pos : POSITION0;
    float4 wPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
};

cbuffer gBrush : register(b0)
{
    float2 gBrushWorldPosition; // Brush position in UV space
    float gBrushRadius; // Brush radius in UV space
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float dist = length(IN.wPosition.xz - gBrushWorldPosition);
    
    if (dist < gBrushRadius)
    {
        // If within brush radius, return a color (e.g., white)
        return float4(0, dist / gBrushRadius, 1 - dist / gBrushRadius, 1);
    }
    else
    {
        return float4(0, 0, 0, 0);
    }
}