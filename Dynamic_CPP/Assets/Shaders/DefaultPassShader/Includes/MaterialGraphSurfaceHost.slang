#pragma once
#include "PrincipledIblLookup.slang"
#include "MaterialGraphSurfacePoint.slang"

// Spatial evaluation ABI. Coordinates are already transformed/skinned into
// world space by the host. lod is explicit, not an invented compute derivative.
cbuffer LXSurfaceConstants : register(b0)
{
    uint lx_surfaceCount;
    uint lx_surfaceTier;
    uint2 lx_surfaceReserved;
    float4 lx_surfaceEye;
};
StructuredBuffer<LXSurfacePoint> lx_surfaceInputs : register(t0);
RWStructuredBuffer<PrincipledIblBakePoint> lx_surfaceOutput : register(u0);

LXMaterialContext LXSurfaceContext(LXSurfacePoint point)
{
    LXMaterialContext context;
    context.uv = point.uvLod.xyz;
    context.lod = point.uvLod.w;
    context.normal = normalize(point.normal.xyz);
    context.tangent = normalize(point.tangent.xyz);
    context.bitangent = normalize(point.bitangent.xyz);
    return context;
}

PrincipledIblBakePoint LXPackSurface(MaterialInputs inputs, LXMaterialContext context, float3 view)
{
    // Preserve authored IOR and Specular Level, and the unrotated tangent.
    // EvaluateIblBakePoint applies these operations exactly once in the baker.
    const float3 normal = PrincipledUnitNormal(inputs.normal, context.normal);
    PrincipledIblBakePoint point;
    point.baseAlpha = float4(inputs.baseColor, inputs.alpha);
    point.normalRoughness = float4(normal, inputs.roughness);
    point.metalIorLevelAo = float4(inputs.metallic, inputs.ior, inputs.specularIorLevel, inputs.occlusion);
    point.tintAnisotropy = float4(inputs.specularTint, inputs.anisotropy);
    point.emissionColorStrength = float4(inputs.emissionColor, inputs.emissionStrength);
    point.coatWeightRoughIorFilmThickness = float4(inputs.coatWeight, inputs.coatRoughness,
        inputs.coatIor, inputs.thinFilmThickness);
    point.coatTintFilmIor = float4(inputs.coatTint, inputs.thinFilmIor);
    point.coatNormalSheenWeight = float4(PrincipledUnitNormal(inputs.coatNormal, context.normal), inputs.sheenWeight);
    point.sheenTintRoughness = float4(inputs.sheenTint, inputs.sheenRoughness);
    const float3 fallbackTangent = PrincipledProjectedTangent(context.tangent, normal);
    const float3 tangent = PrincipledUnitNormal(inputs.tangent - normal * dot(inputs.tangent, normal), fallbackTangent);
    point.tangentRotation = float4(tangent, inputs.anisotropyRotation);
    point.viewTier = float4(normalize(view), float(lx_surfaceTier));
    return point;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void LXEvaluateSurface(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_surfaceCount)
    {
        return;
    }
    const LXSurfacePoint spatial = lx_surfaceInputs[id.x];
    if (!LXValidSurfacePoint(spatial, lx_surfaceEye.xyz))
    {
        PrincipledIblBakePoint rejected = (PrincipledIblBakePoint)0;
        rejected.viewTier.w = -1.0;
        lx_surfaceOutput[id.x] = rejected;
        return;
    }
    const LXMaterialContext context = LXSurfaceContext(spatial);
    const LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    PrincipledIblBakePoint point = LXPackSurface(generated.surfaceInputs, context,
        lx_surfaceEye.xyz - spatial.position.xyz);
    const MaterialInputs raw = generated.surfaceInputs;
    if (!PrincipledValidBakeVector(float4(raw.normal, 0))
        || !PrincipledValidBakeVector(float4(raw.coatNormal, 0))
        || !PrincipledValidBakeVector(float4(raw.tangent, 0)) || !IsValidPrincipledIblBakePoint(point))
    {
        point = (PrincipledIblBakePoint)0;
        point.viewTier.w = -1.0;
    }
    lx_surfaceOutput[id.x] = point;
}

// Bounded reference graphics consumer for validation/preview. One pixel per
// sample; no spatial interpolation. This is not the Scene mesh/instance ABI.
StructuredBuffer<PrincipledIblBakeSample> lx_surfaceIbl : register(t1);
StructuredBuffer<PrincipledIblBakePoint> lx_surfacePoints : register(t2);
[shader("vertex")]
float4 LXSurfaceVS(uint id : SV_VertexID) : SV_Position
{
    const float2 vertices[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(vertices[id], 0, 1);
}
[shader("fragment")]
float4 LXSurfacePS(float4 position : SV_Position) : SV_Target
{
    const uint index = uint(position.x);
    if (index >= lx_surfaceCount || !IsValidPrincipledIblBakePoint(lx_surfacePoints[index]))
    {
        return float4(0);
    }
    const LXSurfacePoint spatial = lx_surfaceInputs[index];
    if (!LXValidSurfacePoint(spatial, lx_surfaceEye.xyz))
    {
        return float4(0);
    }
    const LXMaterialContext context = LXSurfaceContext(spatial);
    const LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    const PrincipledSurface material = LXEvaluateGeneratedSurface(generated, context);
    const float3 view = normalize(lx_surfaceEye.xyz - spatial.position.xyz);
    return float4(EvaluateBakedPrincipledIbl(material, view, lx_surfaceTier == 1u, lx_surfaceIbl[index])
        + material.emission, material.alpha);
}
