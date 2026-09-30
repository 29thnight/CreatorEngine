#pragma once

// Scene fragment hosts opt in before including the generated material. Compute
// and evaluated-point hosts keep their explicitly supplied LOD contract.
#ifndef LX_MATERIAL_PIXEL_FOOTPRINT
#define LX_MATERIAL_PIXEL_FOOTPRINT 0
#endif

float4 LXSampleMaterialImage(Texture2D<float4> image, SamplerState sampler, float2 uv, float explicitLod)
{
#if LX_MATERIAL_PIXEL_FOOTPRINT
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    const float2 dimensions = float2(width, height);
    // Different Vector expressions and image sizes have independent footprints.
    // Fine derivatives must run before the host's divergent coverage/owner test.
    const float footprint = max(length(ddx_fine(uv) * dimensions), length(ddy_fine(uv) * dimensions));
    const float lod = clamp(log2(max(footprint, 1e-20)), 0.0, float(levels - 1u));
#else
    const float lod = explicitLod;
#endif
    return image.SampleLevel(sampler, uv, lod);
}
