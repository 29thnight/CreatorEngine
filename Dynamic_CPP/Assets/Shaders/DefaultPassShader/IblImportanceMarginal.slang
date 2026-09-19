#include "Includes/IblImportance.slang"
cbuffer IblDrawConstants : register(b0)
{
    float4 gForward; float4 gRight; float4 gUp; float4 gParams;
};

[shader("fragment")]
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    uint size = uint(gParams.z);
    float sum = 0.0;
    for (uint y = 0u; y <= uint(position.y); ++y)
        sum += gImportanceRows.Load(int3(size - 1u, y, 0)).a;
    return float4(sum, 0.0, 0.0, 1.0);
}
