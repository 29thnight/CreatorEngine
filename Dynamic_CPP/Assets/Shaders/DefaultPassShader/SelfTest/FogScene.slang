#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gColor : register(u0);
RWTexture2D<float>  gDepth : register(u1);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gCloud : register(u2);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gBlueNoise : register(u3);

cbuffer SceneParams : register(b0)
{
    uint2 gScreenSize;
    uint2 gNoiseSize;
    uint2 gCloudSize;
    uint2 gPadding;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (all(id.xy < gScreenSize))
    {
        gColor[id.xy] = float4(0.5f, 0.5f, 0.5f, 1.0f);
        gDepth[id.xy] = 0.99f;
    }

    if (all(id.xy < gCloudSize))
        gCloud[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (all(id.xy < gNoiseSize))
    {
        const uint value = (id.x * 37u + id.y * 101u) & 0xFFu;
        const float noise = value / 255.0f;
        gBlueNoise[id.xy] = float4(noise, noise, noise, 1.0f);
    }
}
