#pragma once

// Shared world frame ABI after skin/object transforms (80 bytes).
struct LXSurfacePoint
{
    float4 uvLod;
    float4 position;
    float4 normal;
    float4 tangent;
    float4 bitangent;
};

bool LXValidSurfaceVector(float4 value)
{
    return all(isfinite(value)) && all(abs(value) <= 1e6);
}

bool LXValidSurfacePoint(LXSurfacePoint point, float3 eye)
{
    const float3 view = eye - point.position.xyz;
    const bool tangent = dot(point.tangent.xyz, point.tangent.xyz) > 1e-20;
    const bool bitangent = dot(point.bitangent.xyz, point.bitangent.xyz) > 1e-20;
    return LXValidSurfaceVector(point.uvLod) && LXValidSurfaceVector(point.position)
        && LXValidSurfaceVector(point.normal) && LXValidSurfaceVector(point.tangent)
        && LXValidSurfaceVector(point.bitangent) && point.position.w >= 0
        && point.uvLod.w >= 0 && point.uvLod.w <= 32
        && dot(point.normal.xyz, point.normal.xyz) > 1e-20
        // Both missing axes deliberately disable tangent-space perturbation.
        && tangent == bitangent
        && all(isfinite(view)) && dot(view, view) > 1e-20;
}
