RWTexture2D<float4> TargetTexture : register(u0); // ≈∏∞Ÿ ≈ÿΩ∫√≥ (UAV)
Texture2D<float4> InputTexture : register(t0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 uv = DTid.xy;
    
    float3 colorSum = float3(0, 0, 0);
    
    for (int i = -1; i < 2; ++i)
    {
        for (int j = -1; j < 2; ++j)
        {
            int2 sampleCoord = uv + int2(i, j);
            colorSum += InputTexture.Load(int3(sampleCoord, 0));
        }
    }
    
    float3 resolvedColor = colorSum / 9.0;
    
    TargetTexture[uv] = float4(normalize(resolvedColor), 0);
    
    //float4 color = InputTexture.Load(int3(uv, 0));
    
    //TargetTexture[uv] = float4(normalize(color.xyz), 0);
}