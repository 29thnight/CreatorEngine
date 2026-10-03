#pragma once
#include "EnhancedRenderGraph.h"

// One Forward+ culling result is consumed by both Code and Graph materials.
struct EnhancedForwardLighting
{
    static constexpr std::uint32_t TileSize = 16, MaxLightsPerTile = 32;
    RHIBufferSlice lights;
    RHIBufferHandle counts, indices;
    RGHandle graphCounts, graphIndices;
    RHIBufferSlice volumeConstants, volumeTriangles, volumeCoefficients;
    RGHandle graphVolumeCoefficients;
    RHITextureHandle volumeEnvironment;
    RHIBindingTable volumeTable;
    bool reference{};
};
