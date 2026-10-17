Texture2D<float4> source : register(t0);
SamplerState pointSampler : register(s0);
RWTexture2D<float4> output : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void VerifyCookedTexture(uint3 id : SV_DispatchThreadID)
{
    uint mip = id.y / 8;
    uint y = id.y % 8;
    uint size = 8 >> mip;
    if (mip < 4 && id.x < size && y < size)
    {
        float2 uv = (float2(id.x, y) + 0.5) / float(size);
        output[id.xy] = source.SampleLevel(pointSampler, uv, float(mip));
    }
}
