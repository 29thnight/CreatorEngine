#pragma once

#include "MaterialGraphSceneVolumeTypes.slang"

cbuffer LXSceneVolumeCoefficientConstants : register(b1)
{
    uint lx_volumeObject;
    uint3 lx_volumeReserved;
};
RWStructuredBuffer<LXSceneVolumeCoefficient> lx_volumeCoefficientsOutput : register(u0);

[shader("compute")]
[numthreads(1, 1, 1)]
void LXSceneVolumeCoefficientCS()
{
    LXMaterialContext context;
    context.uv = float3(0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    const LXGeneratedMaterial inputs = LXGenerateMaterial(context, LXBoundMaterialParameters());
    const PrincipledVolume medium = LXEvaluateGeneratedVolume(inputs);
    LXSceneVolumeCoefficient result;
    result.scatteringAnisotropy = float4(medium.scattering, medium.anisotropy);
    result.absorption = float4(medium.absorption, 0);
    result.emission = float4(medium.emission, 0);
    lx_volumeCoefficientsOutput[lx_volumeObject] = result;
}
