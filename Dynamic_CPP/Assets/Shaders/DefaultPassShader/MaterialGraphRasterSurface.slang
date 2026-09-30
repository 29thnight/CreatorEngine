#include "Includes/MaterialGraphSurfacePoint.slang"

cbuffer LXRasterConstants : register(b0)
{
    row_major float4x4 lx_rasterViewProjection;
    uint4 lx_rasterViewportTexture;
    float4 lx_rasterLod;
};
StructuredBuffer<LXSurfacePoint> lx_rasterVertices : register(t6);

struct LXRasterVarying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 world : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 bitangent : TEXCOORD4;
};

[shader("vertex")]
LXRasterVarying LXRasterVS(uint id : SV_VertexID)
{
    const LXSurfacePoint point = lx_rasterVertices[id];
    LXRasterVarying result;
    result.position = mul(float4(point.position.xyz, 1), lx_rasterViewProjection);
    result.uv = point.uvLod.xy;
    result.world = point.position.xyz;
    result.normal = point.normal.xyz;
    result.tangent = point.tangent.xyz;
    result.bitangent = point.bitangent.xyz;
    return result;
}

struct LXRasterTargets
{
    float4 uvLod : SV_Target0;
    float4 position : SV_Target1;
    float4 normal : SV_Target2;
    float4 tangent : SV_Target3;
    float4 bitangent : SV_Target4;
    float4 derivatives : SV_Target5;
};

LXRasterTargets LXCaptureRaster(LXRasterVarying input)
{
    LXRasterTargets result;
    const float2 dx = ddx_fine(input.uv);
    const float2 dy = ddy_fine(input.uv);
    const float2 extent = float2(lx_rasterViewportTexture.zw);
    const float rho = max(length(dx * extent), length(dy * extent));
    const float lod = clamp(log2(max(rho, 1e-20)) + lx_rasterLod.y, 0, lx_rasterLod.x);
    result.uvLod = float4(input.uv, 0, lod);
    // MRT writes obey depth testing; UAV pixel writes could race occluded triangles.
    result.position = float4(input.world, lx_rasterLod.z);
    result.normal = float4(input.normal, 0);
    result.tangent = float4(input.tangent, 0);
    result.bitangent = float4(input.bitangent, 0);
    result.derivatives = float4(dx, dy);
    return result;
}

[shader("fragment")]
LXRasterTargets LXRasterPS(LXRasterVarying input)
{
    return LXCaptureRaster(input);
}

Texture2D<float4> lx_rasterOpaqueOwners : register(t7);

[shader("fragment")]
LXRasterTargets LXRasterSharedPS(LXRasterVarying input)
{
    // Evaluate derivatives before any ownership-dependent divergent discard.
    const LXRasterTargets result = LXCaptureRaster(input);
    // Depth equality does not distinguish coplanar draws of different materials.
    // Keep the exact opaque winner selected by the shared prepass.
    if (lx_rasterOpaqueOwners.Load(int3(int2(input.position.xy), 0)).w != lx_rasterLod.z)
    {
        discard;
    }
    return result;
}

Texture2D<float4> lx_rasterInputs_0 : register(t0);
Texture2D<float4> lx_rasterInputs_1 : register(t1);
Texture2D<float4> lx_rasterInputs_2 : register(t2);
Texture2D<float4> lx_rasterInputs_3 : register(t3);
Texture2D<float4> lx_rasterInputs_4 : register(t4);
Texture2D<float4> lx_rasterInputs_5 : register(t5);

float4 LXLoadRasterInput(uint field, int3 pixel)
{
    switch (field)
    {
        case 0u:
            return lx_rasterInputs_0.Load(pixel);
        case 1u:
            return lx_rasterInputs_1.Load(pixel);
        case 2u:
            return lx_rasterInputs_2.Load(pixel);
        case 3u:
            return lx_rasterInputs_3.Load(pixel);
        case 4u:
            return lx_rasterInputs_4.Load(pixel);
        case 5u:
            return lx_rasterInputs_5.Load(pixel);
        default:
            return float4(0);
    }
}
RWStructuredBuffer<LXSurfacePoint> lx_rasterPixels : register(u0);

[shader("compute")]
[numthreads(32, 1, 1)]
void LXResolveRaster(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_rasterViewportTexture.x * lx_rasterViewportTexture.y)
    {
        return;
    }
    const int3 pixel = int3(id.x % lx_rasterViewportTexture.x, id.x / lx_rasterViewportTexture.x, 0);
    LXSurfacePoint result = (LXSurfacePoint)0;
    const float4 position = lx_rasterInputs_1.Load(pixel);
    if (position.w > 0)
    {
        result.uvLod = lx_rasterInputs_0.Load(pixel);
        result.position = float4(position.xyz, 0);
        result.normal = lx_rasterInputs_2.Load(pixel);
        result.tangent = lx_rasterInputs_3.Load(pixel);
        result.bitangent = lx_rasterInputs_4.Load(pixel);
    }
    else
    {
        result.position.w = -1;
    }
    lx_rasterPixels[id.x] = result;
}
