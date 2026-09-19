#include "Includes/IblImportance.slang"
cbuffer IblDrawConstants : register(b0)
{
    float4 gForward; float4 gRight; float4 gUp; float4 gParams;
};

[shader("fragment")]
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    uint size = uint(gParams.z);
    float total = gImportanceMarginal.Load(int3(0, 6u * size - 1u, 0)).r;
    float2 xi = IblIntegrationXi(uint(position.x));
    if (total <= 0.0)
    {
        float z = 1.0 - 2.0 * xi.x;
        float r = sqrt(max(1.0 - z * z, 0.0));
        return uint(position.y) == 0u
            ? float4(r * cos(2.0 * PI * xi.y), r * sin(2.0 * PI * xi.y), z, 0.25 * INV_PI)
            : float4(0.0);
    }

    float target = xi.x * total;
    uint low = 0u, high = 6u * size - 1u;
    while (low < high)
    {
        uint mid = (low + high) / 2u;
        if (gImportanceMarginal.Load(int3(0, mid, 0)).r <= target) low = mid + 1u;
        else high = mid;
    }
    uint row = low;
    float before = row == 0u ? 0.0 : gImportanceMarginal.Load(int3(0, row - 1u, 0)).r;
    float after = gImportanceMarginal.Load(int3(0, row, 0)).r;
    float fy = saturate((target - before) / max(after - before, 1e-20));
    float rowTotal = gImportanceRows.Load(int3(size - 1u, row, 0)).a;
    target = xi.y * rowTotal;
    low = 0u; high = size - 1u;
    while (low < high)
    {
        uint mid = (low + high) / 2u;
        if (gImportanceRows.Load(int3(mid, row, 0)).a <= target) low = mid + 1u;
        else high = mid;
    }
    uint x = low;
    before = x == 0u ? 0.0 : gImportanceRows.Load(int3(x - 1u, row, 0)).a;
    float4 cell = gImportanceRows.Load(int3(x, row, 0));
    float fx = saturate((target - before) / max(cell.a - before, 1e-20));
    float2 uv = (float2(x, row % size) + float2(fx, fy)) * (2.0 / float(size)) - 1.0;
    float2 center = (float2(x, row % size) + 0.5) * (2.0 / float(size)) - 1.0;
    return uint(position.y) == 0u
        ? float4(IblFaceDirection(row / size, uv), IblEnvironmentPdf(cell.rgb, center, uv, size, total))
        : float4(cell.rgb, 1.0);
}
