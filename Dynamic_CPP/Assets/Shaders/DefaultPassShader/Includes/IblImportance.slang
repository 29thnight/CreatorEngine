#pragma once
#include "Ibl.slang"

// Equal counts from the environment distribution and the cosine/GGX distribution.
// Radiance and the environment PDF use the same piecewise constant cube cells.
// Filtering radiance across a tiny HDR sun while retaining a cell PDF introduces
// high-variance samples even when the two sampling strategies are combined.
static const uint kIblImportanceSamples = 1024u;
Texture2D<float4> gImportanceRows : register(t1); // RGB radiance, A row CDF
Texture2D<float4> gImportanceMarginal : register(t2); // R marginal CDF
Texture2D<float4> gImportanceSamples : register(t3); // direction/PDF, then radiance

float IblCellJacobian(float2 uv)
{
    return pow(1.0 + dot(uv, uv), -1.5);
}

float IblCellWeight(float3 radiance, float2 uv)
{
    return dot(radiance, float3(0.2126, 0.7152, 0.0722)) * IblCellJacobian(uv);
}

float3 IblFaceDirection(uint face, float2 uv)
{
    if (face == 0u) return normalize(float3(1.0, -uv.y, -uv.x));
    if (face == 1u) return normalize(float3(-1.0, -uv.y, uv.x));
    if (face == 2u) return normalize(float3(uv.x, 1.0, uv.y));
    if (face == 3u) return normalize(float3(uv.x, -1.0, -uv.y));
    if (face == 4u) return normalize(float3(uv.x, -uv.y, 1.0));
    return normalize(float3(-uv.x, -uv.y, -1.0));
}

float2 IblDirectionUv(float3 d, out uint face)
{
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x >= 0.0 ? 0u : 1u;
        return float2(d.x >= 0.0 ? -d.z : d.z, -d.y) / a.x;
    }
    if (a.y >= a.z)
    {
        face = d.y >= 0.0 ? 2u : 3u;
        return float2(d.x, d.y >= 0.0 ? d.z : -d.z) / a.y;
    }
    face = d.z >= 0.0 ? 4u : 5u;
    return float2(d.z >= 0.0 ? d.x : -d.x, -d.y) / a.z;
}

float IblEnvironmentPdf(float3 radiance, float2 center, float2 uv, uint size, float total)
{
    if (total <= 0.0) return 0.25 * INV_PI;
    return IblCellWeight(radiance, center) / total
        * (0.25 * float(size * size)) / IblCellJacobian(uv);
}

float3 IblLookupCell(float3 direction, uint size, float total, out float pdf)
{
    uint face;
    float2 uv = IblDirectionUv(direction, face);
    uint2 cell = min(uint2(saturate(uv * 0.5 + 0.5) * float(size)), size - 1u);
    float3 radiance = gImportanceRows.Load(int3(cell.x, face * size + cell.y, 0)).rgb;
    float2 center = (float2(cell) + 0.5) * (2.0 / float(size)) - 1.0;
    pdf = IblEnvironmentPdf(radiance, center, uv, size, total);
    return radiance;
}

float2 IblIntegrationXi(uint index)
{
    return float2((float(index) + 0.5) / float(kIblImportanceSamples), RadicalInverseVdC(index));
}
