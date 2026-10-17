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
        // 호출자의 vector를 수정하기 전에 원본 사용 목록을 전부 검증한다.
        // 해시 슬롯은 최초 사용 위치를 가리킨다. 입력마다 압축할 목적지도 기록하므로
        // 압축 중에 덮어쓴 원본 핸들을 다시 참조하지 않는다. 호출이 끝나면 모든 임시
        // 인덱스를 버린다. 테이블은 절반까지만 채우며 충돌 시 핸들 전체를 비교한다.
        constexpr size_t empty = (std::numeric_limits<size_t>::max)();
        std::array<size_t, 128> localSlots;
        std::array<size_t, 64> localDestinations;
        std::vector<size_t> overflowScratch;
        std::span<size_t> slots;
        std::span<size_t> destinations;
        if (uses.size() <= localDestinations.size())
        {
            slots = localSlots;
            destinations = std::span{localDestinations}.first(uses.size());
            std::ranges::fill(slots, empty);
        }
        else
        {
            if (uses.size() > overflowScratch.max_size() / 3)
            {
                throw std::length_error("Scene pass has too many resource uses.");
            }
            overflowScratch.assign(uses.size() * 3, empty);
            slots = std::span{overflowScratch}.first(uses.size() * 2);
            destinations = std::span{overflowScratch}.subspan(slots.size());
        }
        const auto original = std::span{uses};
        size_t uniqueCount = 0;
        for (size_t index = 0; index < original.size(); ++index)
        {
            const auto& use = original[index];
            size_t slot = HandleHash{}(use.handle) % slots.size();
            while (slots[slot] != empty && !SameHandle{}(original[slots[slot]].handle, use.handle))
            {
                slot = slot + 1 == slots.size() ? 0 : slot + 1;
            }
            if (slots[slot] == empty)
            {
                slots[slot] = index;
                destinations[index] = uniqueCount++;
                continue;
            }
            const auto& first = original[slots[slot]];
            destinations[index] = destinations[slots[slot]];
            if (first.access != RGAccessMode::Read || use.access != RGAccessMode::Read)
            {
                throw std::runtime_error("Scene pass duplicates a writable resource version.");
            }
            if (first.state == use.state)
            {
                continue;
            }
            const auto shaderRead = [](RHIResourceState state) {
                return state == RHIResourceState::ShaderResource || state == RHIResourceState::PixelShaderResource;
            };
            if (!shaderRead(first.state) || !shaderRead(use.state))
            {
                throw std::runtime_error("Scene pass duplicates incompatible read states.");
            }
        }
        if (uniqueCount == uses.size())
        {
            return;
        }
        // 목적지는 해당 자원보다 먼저 등장한 고유 자원의 수이므로 원본 인덱스를 넘지 않는다.
        // 따라서 압축해도 아직 읽지 않은 뒤쪽 입력을 덮지 않는다. 이 첫 수정 이후에는
        // 해시 테이블을 다시 참조하지 않는다.
        size_t copied = 0;
        for (size_t index = 0; index < original.size(); ++index)
        {
            const auto destination = destinations[index];
            if (destination == copied)
            {
                original[destination] = original[index];
                ++copied;
            }
            else if (original[destination].state != original[index].state)
            {
                original[destination].state = RHIResourceState::ShaderResource;
            }
        }
        uses.resize(uniqueCount);
    }
} // namespace material_graph
