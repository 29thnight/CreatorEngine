#include "Includes/IblImportance.slang"
cbuffer IblDrawConstants : register(b0)
{
    float4 gForward; float4 gRight; float4 gUp;
    float4 gParams; // y = source mip, z = importance cube size
};
TextureCube gCube : register(t0);
SamplerState gSampler : register(s0);

[shader("fragment")]
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    uint size = uint(gParams.z);
    uint2 pixel = uint2(position.xy);
    uint face = pixel.y / size;
    float v = (float(pixel.y % size) + 0.5) * (2.0 / float(size)) - 1.0;
    float weight = 0.0;
    float3 radiance = float3(0.0);
    for (uint x = 0u; x <= pixel.x; ++x)
    {
        float2 uv = float2((float(x) + 0.5) * (2.0 / float(size)) - 1.0, v);
        radiance = SanitizeRadiance(gCube.SampleLevel(gSampler,
            IblFaceDirection(face, uv), gParams.y).rgb);
        weight += IblCellWeight(radiance, uv);
    }
    return float4(radiance, weight);
}
