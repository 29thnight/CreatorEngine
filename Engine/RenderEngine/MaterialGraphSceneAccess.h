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
        // Keep first-use order without allocating a hash node per resource on
        // every declaration. Slots own only indices into the local result;
        // no borrowed handles or frame state survive this call. At most half
        // the table is occupied, and collisions compare the complete handle.
        constexpr size_t empty = (std::numeric_limits<size_t>::max)();
        std::array<size_t, 128> localSlots;
        std::vector<size_t> overflowSlots;
        std::span<size_t> slots;
        if (uses.size() <= localSlots.size() / 2)
        {
            slots = localSlots;
            std::ranges::fill(slots, empty);
        }
        else
        {
            if (uses.size() > overflowSlots.max_size() / 2)
            {
                throw std::length_error("Scene pass has too many resource uses.");
            }
            overflowSlots.assign(uses.size() * 2, empty);
            slots = overflowSlots;
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> unique;
        unique.reserve(uses.size());
        for (const auto& use : std::span{uses})
        {
            size_t slot = HandleHash{}(use.handle) % slots.size();
            while (slots[slot] != empty && !SameHandle{}(unique.data()[slots[slot]].handle, use.handle))
            {
                slot = slot + 1 == slots.size() ? 0 : slot + 1;
            }
            if (slots[slot] == empty)
            {
                slots[slot] = unique.size();
                unique.push_back(use);
                continue;
            }
            auto* found = &unique.data()[slots[slot]];
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
