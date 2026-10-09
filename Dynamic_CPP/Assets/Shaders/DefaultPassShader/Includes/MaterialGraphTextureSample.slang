#pragma once

// Scene fragment hosts opt in before including the generated material. Compute
// and evaluated-point hosts keep their explicitly supplied LOD contract.
#ifndef LX_MATERIAL_PIXEL_FOOTPRINT
#define LX_MATERIAL_PIXEL_FOOTPRINT 0
#endif

float4 LXSampleMaterialImage(Texture2D<float4> image, SamplerState sampler, float2 uv, float explicitLod, uint encoding)
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
    float4 sampled = image.SampleLevel(sampler, uv, lod);
    // Runtime storage metadata selects the decode; a zero blue channel is not
    // evidence of BC5. Re-encode positive Z for the graph's RGB Normal Map node.
    // Keep RG and alpha exactly as sampled, including filtered/mipmapped values.
    if (encoding == 1u)
    {
        const float2 xy = sampled.xy * 2.0 - 1.0;
        sampled.z = sqrt(saturate(1.0 - dot(xy, xy))) * 0.5 + 0.5;
    }
    return sampled;
}
