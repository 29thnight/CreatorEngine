#include "Includes/IblImportance.slang"

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams; // x roughness, y importance grid size, z environment size, w mip count
};
TextureCube gCube : register(t0);
SamplerState gSampler : register(s0);
struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

// Normalized GGX convolution for the first half of the split-sum approximation.
[shader("fragment")]
float4 PSMain(VSOut input) : SV_TARGET
{
    float3 n = normalize(input.texCoord);
    float roughness = saturate(gParams.x);
    // A mirror must preserve the source texels, including small HDR emitters.
    if (roughness == 0.0)
        return float4(SanitizeRadiance(gCube.SampleLevel(gSampler, n, 0.0).rgb), 1.0);

    uint size = uint(gParams.y);
    float total = gImportanceMarginal.Load(int3(0, 6u * size - 1u, 0)).r;
    float alpha = roughness * roughness;
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < 2u * kIblImportanceSamples; ++i)
    {
        float3 l, radiance;
        float environmentPdf;
        if (i < kIblImportanceSamples)
        {
            float3 h = ImportanceSampleGGX(IblIntegrationXi(i), n, roughness);
            l = normalize(2.0 * dot(n, h) * h - n);
            radiance = IblLookupCell(l, size, total, environmentPdf);
        }
        else
        {
            uint index = i - kIblImportanceSamples;
            float4 sample = gImportanceSamples.Load(int3(index, 0, 0));
            l = sample.xyz;
            environmentPdf = sample.w;
            radiance = gImportanceSamples.Load(int3(index, 1, 0)).rgb;
        }
        float ndotl = dot(n, l);
        if (ndotl <= 0.0) continue;
        // V=N, hence NoH=sqrt((1+NoL)/2) and reflected-direction PDF=D/4.
        float ggxPdf = 0.25 * DistributionGGX(sqrt(0.5 * (1.0 + ndotl)), alpha);
        float pdf = 0.5 * (ggxPdf + environmentPdf);
        float w = ndotl * ggxPdf / max(pdf, 1e-20);
        sum += radiance * w;
        weight += w;
    }
    return float4(SanitizeRadiance(sum / max(weight, 1e-20)), 1.0);
}
