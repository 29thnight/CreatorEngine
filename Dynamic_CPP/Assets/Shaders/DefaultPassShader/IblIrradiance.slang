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

// Store E/PI. Balance-heuristic MIS captures small bright emitters without
// clamping their energy or baking a fixed cosine sample pattern into the cube.
[shader("fragment")]
float4 PSMain(VSOut input) : SV_TARGET
{
    float3 n = normalize(input.texCoord);
    uint size = uint(gParams.y);
    float total = gImportanceMarginal.Load(int3(0, 6u * size - 1u, 0)).r;
    float3 sum = float3(0.0);
    for (uint i = 0u; i < 2u * kIblImportanceSamples; ++i)
    {
        float3 l, radiance;
        float environmentPdf;
        if (i < kIblImportanceSamples)
        {
            l = SampleHemisphereCosine(IblIntegrationXi(i), n);
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
        float cosinePdf = saturate(dot(n, l)) * INV_PI;
        float pdf = 0.5 * (cosinePdf + environmentPdf);
        sum += radiance * (cosinePdf / max(pdf, 1e-20));
    }
    return float4(SanitizeRadiance(sum / float(2u * kIblImportanceSamples)), 1.0);
}
