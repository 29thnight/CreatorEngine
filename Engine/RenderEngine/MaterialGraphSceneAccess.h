#pragma once

#include "Render/Passes/Geometry/EnhancedGBufferPass.h"

#include <algorithm>
#include <stdexcept>

namespace material_graph
{
    inline RGHandle AdvanceSceneColor(EnhancedRenderGraph& graph, RGHandle lighting)
    {
        return graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned ? graph.Modify(lighting) : lighting;
    }

    inline EnhancedGBufferPass::Outputs AdvanceSceneSurface(EnhancedRenderGraph& graph,
                                                            EnhancedGBufferPass::Outputs inputs,
                                                            RGAccessMode colorAccess)
    {
        if (colorAccess != RGAccessMode::Write && colorAccess != RGAccessMode::ReadWrite)
        {
            throw std::runtime_error("Scene surface requires Write or ReadWrite color access.");
        }
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            for (auto* color : {&inputs.diffuse, &inputs.metalRough, &inputs.normal, &inputs.emissive, &inputs.bitmask})
            {
                *color = colorAccess == RGAccessMode::Write ? graph.Write(*color) : graph.Modify(*color);
            }
            inputs.depth = graph.Modify(inputs.depth);
        }
        return inputs;
    }

    // Draws can share geometry and material textures. Only compatible reads of
    // the exact same version may be combined; writes remain unambiguous.
    inline void NormalizeSceneReads(const EnhancedRenderGraph& graph,
                                    std::vector<EnhancedRenderGraph::RGPassUsage>& uses)
    {
        if (graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder)
        {
            return;
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> unique;
        for (const auto& use : uses)
        {
            const auto found = std::ranges::find_if(unique, [&](const auto& prior) {
                return prior.handle.index == use.handle.index && prior.handle.version == use.handle.version &&
                       prior.handle.kind == use.handle.kind && prior.handle.epoch == use.handle.epoch;
            });
            if (found == unique.end())
            {
                unique.push_back(use);
                continue;
            }
            if (found->access != RGAccessMode::Read || use.access != RGAccessMode::Read)
            {
                throw std::runtime_error("Scene pass duplicates a writable resource version.");
            }
            if (found->state == use.state)
            {
                continue;
            }
            const auto shaderRead = [](RHIResourceState state) {
                return state == RHIResourceState::ShaderResource || state == RHIResourceState::PixelShaderResource;
            };
            if (shaderRead(found->state) && shaderRead(use.state))
            {
                found->state = RHIResourceState::ShaderResource;
            }
            else
            {
                throw std::runtime_error("Scene pass duplicates incompatible read states.");
            }
        }
        uses = std::move(unique);
    }
} // namespace material_graph
