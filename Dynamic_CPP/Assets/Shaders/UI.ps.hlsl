#include "Sampler.hlsli"


Texture2D Sprite : register(t0);
struct UIPSINtput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};
float4 main(UIPSINtput IN) : SV_TARGET
{
    
    float2 tex = IN.texCoord;
    return Sprite.Sample(LinearSampler, tex);
}