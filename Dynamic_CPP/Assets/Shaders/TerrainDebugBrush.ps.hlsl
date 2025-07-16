#include "Sampler.hlsli"
Texture2D targetTexture : register(t0);
Texture2D maskTexture : register(t1);

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
    int maskTextureWidth; // Width of the mask texture
    int maskTextureHeight; // Height of the mask texture
    bool isEditing; // Flag to indicate if editing is enabled
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 clipPos = IN.pos;
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 uv = 0.5 * ndc.xy + 0.5; // Convert NDC to UV space
    uv.y = -uv.y;
    //uv.y = 1.0 - uv.y;

    float dist = length(IN.wPosition.xz - gBrushWorldPosition);
    float invBrushRadius = 1.0 / gBrushRadius;
    float clampRadius = dist * invBrushRadius;
    
    float3 tex= targetTexture.Sample(LinearSampler, uv);
    
    float3 color = tex;
    
    if (!isEditing)
    {
        // If not editing, return the original texture color
        return float4(tex, 1);
    }
    
    float dx = IN.wPosition.x - gBrushWorldPosition.x;
    float dy = IN.wPosition.z - gBrushWorldPosition.y;
    
    float2 brushUV = float2(dx, dy) / (2.0f * gBrushRadius);
    brushUV.y = -brushUV.y; // Invert Y coordinate for texture sampling
    brushUV = brushUV + 0.5;
    
    
    //float2 maskUV = (IN.wPosition.xz - gBrushWorldPosition) / gBrushRadius * 0.5 + 0.5;

    //maskUV.y = -maskUV.y;
    
   // float3 maskTex = maskTexture.Sample(ClampSampler, maskUV);
  
    

    float maskValue = 0.0;
    if (all(brushUV >= 0.0) && all(brushUV <= 1.0))
    {
        maskValue = maskTexture.Sample(ClampSampler, brushUV).r;
    }
    
    
    
    if (maskTextureWidth>0)
    {
        if (maskValue > 0.0)
        {
            // If mask value is greater than 0, apply the mask
            color = lerp(float3(0, maskValue, 0), tex, 0.8);
        }
        else
        {
            // If mask value is 0, just return the original texture color
            color = tex;
        }
    }
    else
    {
        if (dist < gBrushRadius)
        {
        // If within brush radius, return a color (e.g., white)
        
         color = lerp(float3(0, clampRadius, 1.0 - clampRadius), tex, 0.8);
        }
        
    }
    
    
    return float4(color, 1);
}