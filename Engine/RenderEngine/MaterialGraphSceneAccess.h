#pragma once

#include "Render/Passes/Geometry/SceneSurfaceTargets.h"

#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
    inline RGHandle AdvanceSceneColor(EnhancedRenderGraph& graph, RGHandle lighting)
    {
        return graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned ? graph.Modify(lighting) : lighting;
    }

    inline SceneSurfaceTargets::Outputs AdvanceSceneSurface(EnhancedRenderGraph& graph,
                                                            SceneSurfaceTargets::Outputs inputs,
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
        if (graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder || uses.size() < 2)
        {
            return;
        }
        struct HandleHash
        {
            size_t operator()(const RGHandle& handle) const noexcept
            {
                const uint64_t slot = uint64_t(handle.index) | (uint64_t(handle.version) << 16) |
                    (uint64_t(handle.kind) << 32);
                return std::hash<uint64_t>{}(slot) ^ (std::hash<uint64_t>{}(handle.epoch) +
                    size_t{0x9e3779b9u} + (size_t(slot) << 6) + (size_t(slot) >> 2));
            }
        };
        struct SameHandle
        {
            bool operator()(const RGHandle& left, const RGHandle& right) const noexcept
            {
                return left.index == right.index && left.version == right.version &&
                    left.kind == right.kind && left.epoch == right.epoch;
            }
        };
        // Keep first-use order while looking up the complete logical handle.
        // A hash collision still compares index/version/kind/epoch exactly.
        std::unordered_map<RGHandle, size_t, HandleHash, SameHandle> indices;
        indices.reserve(uses.size());
        std::vector<EnhancedRenderGraph::RGPassUsage> unique;
        unique.reserve(uses.size());
        for (const auto& use : uses)
        {
            const auto [entry, inserted] = indices.try_emplace(use.handle, unique.size());
            if (inserted)
            {
                unique.push_back(use);
                continue;
            }
            auto* found = &unique[entry->second];
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
