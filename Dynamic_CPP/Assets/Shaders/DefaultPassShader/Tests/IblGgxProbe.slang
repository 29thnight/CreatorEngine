#include "../Includes/Brdf.slang"
#ifdef __spirv__
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> gOutput : register(u0);

[numthreads(4, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const float roughness[4] = { 0.032, 0.05, 0.1, 0.2 };
    float alpha = roughness[id.x] * roughness[id.x];
    gOutput[id.xy] = float4(DistributionGGX(1.0, alpha),
        DistributionGGX(0.99, alpha), 0.0, 1.0);
}
