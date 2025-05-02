RWTexture2D<float4> TargetTexture : register(u0); // ≈∏∞Ÿ ≈ÿΩ∫√≥ (UAV)
Texture2D<float4> InputTexture : register(t0);
SamplerState litSample : register(s0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 uv = DTid.xy;
    
    float4 color = InputTexture.Load(int3(uv, 0));
    
    TargetTexture[uv] = color;

    
}