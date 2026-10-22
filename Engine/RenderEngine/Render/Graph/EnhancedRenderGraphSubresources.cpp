#include "EnhancedRenderGraph.h"
#include <bit>

namespace
{
    using Usage = EnhancedRenderGraph::RGPassUsage;
    constexpr size_t kMaximumSubresourceCells = 65536;
    bool WritesRange(const Usage& usage)
    {
        return usage.access == RGAccessMode::Write || usage.access == RGAccessMode::ReadWrite ||
               usage.access == RGAccessMode::AtomicMin64;
    }
    bool ReadsRange(const Usage& usage)
    {
        return usage.access == RGAccessMode::Read || usage.access == RGAccessMode::ReadWrite ||
               usage.access == RGAccessMode::AtomicMin64;
    }
    bool WriteState(RHIResourceState state)
    {
        return state == RHIResourceState::RenderTarget || state == RHIResourceState::DepthWrite ||
               state == RHIResourceState::UnorderedAccess || state == RHIResourceState::CopyDest;
    }
    bool NarrowRange(const Usage& usage)
    {
        return usage.textureRange != RHITextureSubresourceRange{} || usage.bufferRange != RHIBufferSubresourceRange{};
    }
    template<class T>
    void Unique(std::vector<T>& values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }
} // namespace

bool EnhancedRenderGraph::ValidateSubresources(std::string& outError)
{
    m_hasSubresourceUsages = false;
    for (const auto& pass : m_passes)
    {
        m_hasSubresourceUsages |= std::ranges::any_of(pass.usages, NarrowRange);
        for (const auto& phase : pass.phases)
        {
            m_hasSubresourceUsages |= std::ranges::any_of(phase.usages, NarrowRange);
        }
    }
    if (!m_hasSubresourceUsages)
    {
        return true;
    }
    if (m_scheduling != RGSchedulingMode::ExplicitVersioned)
    {
        outError = "Subresource declarations require explicit versioned scheduling";
        return false;
    }
    m_rangeSchemas.resize(m_resources.size());
    for (size_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& resource = m_resources[index];
        auto& schema = m_rangeSchemas[index];
        schema = {};
        if (resource.IsBuffer())
        {
            schema.buffer = resource.imported ? m_deviceServices->DescribeBuffer(resource.buffer)
                                              : RHIBufferInfo{resource.bufferDesc.bytes};
            if (!schema.buffer.IsValid())
            {
                outError = "Subresource graph needs valid buffer storage: " + resource.name;
                return false;
            }
        }
        else
        {
            schema.texture = resource.imported ? m_deviceServices->DescribeTexture(resource.handle)
                                               : RHITextureInfo{resource.desc.width,     resource.desc.height,
                                                                resource.desc.arraySize, 1,
                                                                resource.desc.format,    false};
            if (!schema.texture.IsValid())
            {
                outError = "Subresource graph needs valid texture storage: " + resource.name;
                return false;
            }
        }
    }
    std::vector<size_t> cellCounts(m_resources.size());
    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        // The Cartesian mip/layer partition is explicit compiler storage. Reject
        // pathological declarations before dependency or barrier allocation.
        cellCounts[index] = BuildSubresourceCells(index, false).size();
        if (cellCounts[index] == 0)
        {
            outError = "Subresource partition exceeds 65536 cells: " + m_resources[index].name;
            return false;
        }
    }
    for (const auto& pass : m_passes)
    {
        const auto validate = [&](std::span<const RGPassUsage> usages)
        {
            for (const auto& usage : usages)
            {
                if (!ValidVersionHandle(usage.handle))
                {
                    outError = "Subresource usage has a stale/foreign handle: " + pass.name;
                    return false;
                }
                const auto& resource = m_resources[usage.handle.index];
                const auto& schema = m_rangeSchemas[usage.handle.index];
                RHITextureSubresourceRange texture;
                RHIBufferSubresourceRange buffer;
                const bool valid =
                    resource.IsBuffer()
                        ? usage.textureRange == RHITextureSubresourceRange{} &&
                              ResolveRHIBufferSubresourceRange(usage.bufferRange, schema.buffer.bytes, buffer)
                        : usage.bufferRange == RHIBufferSubresourceRange{} &&
                              ResolveRHITextureSubresourceRange(
                                  usage.textureRange, schema.texture.mipLevels,
                                  (schema.texture.is3D ? 1u : schema.texture.depthOrArraySize),
                                  RHITextureAspects(schema.texture.format), texture);
                if (!valid)
                {
                    outError = "Invalid subresource range: " + pass.name + " / " + resource.name;
                    return false;
                }
            }
            // A backend whose state granularity is wider than the access range
            // cannot represent two different simultaneous states in one callback.
            for (size_t left = 0; left < usages.size(); ++left)
            {
                for (size_t right = left + 1; right < usages.size(); ++right)
                {
                    if (usages[left].handle.index != usages[right].handle.index ||
                        usages[left].state == usages[right].state)
                    {
                        continue;
                    }
                    const auto index = usages[left].handle.index;
                    for (const auto& cell : BuildSubresourceCells(index, true))
                    {
                        if (CoversSubresource(index, usages[left], cell, true) &&
                            CoversSubresource(index, usages[right], cell, true))
                        {
                            outError = "Backend state granularity cannot represent pass ranges: " + pass.name;
                            return false;
                        }
                    }
                }
            }
            return true;
        };
        if (!validate(pass.usages))
        {
            return false;
        }
        for (const auto& phase : pass.phases)
        {
            if (!validate(phase.usages))
            {
                return false;
            }
            for (const auto& usage : phase.usages)
            {
                const auto outer = std::ranges::find_if(pass.usages, [&](const auto& value)
                                                        { return value.handle.index == usage.handle.index; });
                if (outer == pass.usages.end() || outer->textureRange != usage.textureRange ||
                    outer->bufferRange != usage.bufferRange)
                {
                    outError = "Repeated phase ranges must match their outer resource contract: " + pass.name;
                    return false;
                }
            }
        }
    }
    // A usage can transition multiple cells. The old whole-resource bound of
    // one barrier per usage is insufficient for repeated range passes. Include
    // boundary convergence and prologue/epilogue without narrowing uint64_t sums.
    uint64_t maximumBarriers = 0;
    for (const auto count : cellCounts)
    {
        maximumBarriers += 2 * count;
    }
    const auto countUsages = [&](std::span<const RGPassUsage> usages, uint32_t repetitions)
    {
        for (const auto& usage : usages)
        {
            maximumBarriers += (2 * uint64_t(cellCounts[usage.handle.index]) + 1) * repetitions;
            if (maximumBarriers > UINT32_MAX)
            {
                return false;
            }
        }
        return maximumBarriers <= UINT32_MAX;
    };
    for (const auto& pass : m_passes)
    {
        if (pass.repeated)
        {
            for (const auto& phase : pass.phases)
            {
                if (!countUsages(phase.usages, pass.repeatCount))
                {
                    outError = "Subresource barriers exceed diagnostic counter capacity";
                    return false;
                }
            }
        }
        else if (!countUsages(pass.usages, 1))
        {
            outError = "Subresource barriers exceed diagnostic counter capacity";
            return false;
        }
    }
    return true;
}

std::vector<EnhancedRenderGraph::SubresourceCell> EnhancedRenderGraph::BuildSubresourceCells(uint16_t index,
                                                                                             bool states) const
{
    const auto& resource = m_resources[index];
    const auto& schema = m_rangeSchemas[index];
    const auto capabilities = m_deviceServices->GetSubresourceCapabilities();
    const auto aspects = RHITextureAspects(schema.texture.format);
    std::vector<SubresourceCell> cells;
    if (resource.IsBuffer() && states && !capabilities.bufferRanges)
    {
        cells.push_back({{}, {0, schema.buffer.bytes}});
        return cells;
    }
    if (!resource.IsBuffer() && states && !capabilities.textureRanges)
    {
        cells.push_back(
            {{0, schema.texture.mipLevels, 0, (schema.texture.is3D ? 1u : schema.texture.depthOrArraySize), aspects},
             {}});
        return cells;
    }
    std::vector<uint64_t> offsets{0, schema.buffer.bytes};
    std::vector<uint32_t> mips{0, schema.texture.mipLevels},
        layers{0, (schema.texture.is3D ? 1u : schema.texture.depthOrArraySize)};
    const auto add = [&](std::span<const RGPassUsage> usages)
    {
        for (const auto& usage : usages)
        {
            if (usage.handle.index != index)
            {
                continue;
            }
            if (resource.IsBuffer())
            {
                RHIBufferSubresourceRange range;
                if (ResolveRHIBufferSubresourceRange(usage.bufferRange, schema.buffer.bytes, range))
                {
                    offsets.push_back(range.offset);
                    offsets.push_back(range.offset + range.size);
                }
            }
            else
            {
                RHITextureSubresourceRange range;
                if (ResolveRHITextureSubresourceRange(usage.textureRange, schema.texture.mipLevels,
                                                      (schema.texture.is3D ? 1u : schema.texture.depthOrArraySize),
                                                      aspects, range))
                {
                    mips.push_back(range.firstMip);
                    mips.push_back(range.firstMip + range.mipCount);
                    layers.push_back(range.firstLayer);
                    layers.push_back(range.firstLayer + range.layerCount);
                }
            }
        }
    };
    for (const auto& pass : m_passes)
    {
        add(pass.usages);
        for (const auto& phase : pass.phases)
        {
            add(phase.usages);
        }
    }
    if (resource.IsBuffer())
    {
        Unique(offsets);
        if (offsets.size() - 1 > kMaximumSubresourceCells)
        {
            return {};
        }
        cells.reserve(offsets.size() - 1);
        for (size_t part = 1; part < offsets.size(); ++part)
        {
            cells.push_back({{}, {offsets[part - 1], offsets[part] - offsets[part - 1]}});
        }
    }
    else
    {
        Unique(mips);
        Unique(layers);
        const size_t aspectCount =
            states && !capabilities.separateDepthStencil ? 1 : std::popcount(static_cast<uint8_t>(aspects));
        const auto mipCount = mips.size() - 1, layerCount = layers.size() - 1;
        if (!aspectCount || !mipCount || !layerCount || mipCount > kMaximumSubresourceCells / aspectCount ||
            layerCount > kMaximumSubresourceCells / aspectCount / mipCount)
        {
            return {};
        }
        cells.reserve(mipCount * layerCount * aspectCount);
        for (size_t mip = 1; mip < mips.size(); ++mip)
        {
            for (size_t layer = 1; layer < layers.size(); ++layer)
            {
                const auto append = [&](RHITextureAspect aspect)
                {
                    cells.push_back({{mips[mip - 1], mips[mip] - mips[mip - 1], layers[layer - 1],
                                      layers[layer] - layers[layer - 1], aspect},
                                     {}});
                };
                if (states && !capabilities.separateDepthStencil)
                {
                    append(aspects);
                }
                else
                {
                    for (auto aspect : {RHITextureAspect::Color, RHITextureAspect::Depth, RHITextureAspect::Stencil})
                    {
                        if ((static_cast<uint8_t>(aspects) & static_cast<uint8_t>(aspect)) != 0)
                        {
                            append(aspect);
                        }
                    }
                }
            }
        }
    }
    return cells;
}

bool EnhancedRenderGraph::CoversSubresource(uint16_t index, const RGPassUsage& usage, const SubresourceCell& cell,
                                            bool states) const
{
    const auto& schema = m_rangeSchemas[index];
    const auto capabilities = m_deviceServices->GetSubresourceCapabilities();
    if (m_resources[index].IsBuffer())
    {
        if (states && !capabilities.bufferRanges)
        {
            return true;
        }
        RHIBufferSubresourceRange range;
        return ResolveRHIBufferSubresourceRange(usage.bufferRange, schema.buffer.bytes, range) &&
               range.offset < cell.buffer.offset + cell.buffer.size && cell.buffer.offset < range.offset + range.size;
    }
    if (states && !capabilities.textureRanges)
    {
        return true;
    }
    RHITextureSubresourceRange range;
    if (!ResolveRHITextureSubresourceRange(usage.textureRange, schema.texture.mipLevels,
                                           (schema.texture.is3D ? 1u : schema.texture.depthOrArraySize),
                                           RHITextureAspects(schema.texture.format), range))
    {
        return false;
    }
    if (states && !capabilities.separateDepthStencil)
    {
        range.aspect = RHITextureAspects(schema.texture.format);
    }
    return (static_cast<uint8_t>(range.aspect) & static_cast<uint8_t>(cell.texture.aspect)) != 0 &&
           range.firstMip < cell.texture.firstMip + cell.texture.mipCount &&
           cell.texture.firstMip < range.firstMip + range.mipCount &&
           range.firstLayer < cell.texture.firstLayer + cell.texture.layerCount &&
           cell.texture.firstLayer < range.firstLayer + range.layerCount;
}

bool EnhancedRenderGraph::BuildSubresourceDependencies(std::string& outError)
{
    using Edge = DiagnosticSnapshot::VersionEdge;
    using Reason = Edge::Reason;
    struct Access
    {
        uint16_t pass;
        const RGPassUsage* usage;
    };
    std::vector<std::vector<std::vector<Access>>> accesses(m_resources.size());
    std::vector<std::vector<SubresourceCell>> cells(m_resources.size());
    std::vector<Edge> dependencies;
    ce::dynamic_bitset alive(m_passes.size(), false);
    std::vector<uint16_t> roots;
    const auto retain = [&](uint16_t pass)
    {
        if (!alive.test(pass))
        {
            alive.set(pass, true);
            roots.push_back(pass);
        }
    };
    const auto edge = [&](uint16_t from, uint16_t to, uint16_t resource, uint16_t version, Reason reason)
    {
        if (from != to)
        {
            dependencies.push_back({from, to, resource, version, reason});
        }
    };
    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& resource = m_resources[index];
        accesses[index].resize(resource.versions.size());
        cells[index] = BuildSubresourceCells(index, false);
        if (resource.imported && !resource.firstImport)
        {
            outError = "RG2 duplicate physical import; reuse its handle: " + resource.name;
            return false;
        }
        for (size_t version = resource.imported ? 1 : 0; version < resource.versions.size(); ++version)
        {
            const auto& declaration = resource.versions[version];
            if (declaration.parent != (version ? version - 1 : RGHandle::kInvalid) ||
                (declaration.modify && declaration.parent == RGHandle::kInvalid))
            {
                outError = "RG2 forked/stale write parent or uninitialized Modify: " + resource.name;
                return false;
            }
        }
    }
    for (uint16_t pass = 0; pass < m_passes.size(); ++pass)
    {
        if (m_passes[pass].hasSideEffect)
        {
            retain(pass);
        }
        for (const auto& usage : m_passes[pass].usages)
        {
            const auto index = usage.handle.index, version = usage.handle.version;
            const auto& resource = m_resources[index];
            const auto& declaration = resource.versions[version];
            if (usage.access < RGAccessMode::Read || usage.access > RGAccessMode::AtomicMin64 ||
                usage.state < RHIResourceState::Common || usage.state > RHIResourceState::VertexAndShaderResource ||
                (WritesRange(usage) && !WriteState(usage.state)) ||
                (usage.access == RGAccessMode::Read && WriteState(usage.state) &&
                 usage.state != RHIResourceState::UnorderedAccess) ||
                (!resource.IsBuffer() &&
                 (usage.state == RHIResourceState::IndexBuffer || usage.state == RHIResourceState::IndirectArgument ||
                  usage.state == RHIResourceState::VertexAndShaderResource)) ||
                (resource.IsBuffer() &&
                 (usage.state == RHIResourceState::RenderTarget || usage.state == RHIResourceState::DepthWrite ||
                  usage.state == RHIResourceState::DepthRead ||
                  usage.state == RHIResourceState::DepthReadShaderResource)))
            {
                outError = "RG2 access/state mismatch: " + m_passes[pass].name;
                return false;
            }
            if (WritesRange(usage) && ((resource.imported && version == 0) ||
                                       declaration.atomicMin64 != (usage.access == RGAccessMode::AtomicMin64) ||
                                       declaration.modify != (usage.access == RGAccessMode::ReadWrite ||
                                                              usage.access == RGAccessMode::AtomicMin64) ||
                                       (declaration.atomicMin64 &&
                                        (!resource.IsBuffer() || usage.state != RHIResourceState::UnorderedAccess))))
            {
                outError = "RG2 Write/Modify/AtomicMin64 access mismatch: " + resource.name;
                return false;
            }
            for (const auto& prior : m_passes[pass].usages)
            {
                if (&prior == &usage)
                {
                    break;
                }
                if (prior.handle.index == index &&
                    std::ranges::any_of(cells[index],
                                        [&](const auto& cell)
                                        {
                                            return CoversSubresource(index, prior, cell, false) &&
                                                   CoversSubresource(index, usage, cell, false);
                                        }))
                {
                    outError = "RG2 overlapping accesses in one pass; use Modify: " + m_passes[pass].name;
                    return false;
                }
            }
            accesses[index][version].push_back({pass, &usage});
        }
    }
    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& resource = m_resources[index];
        for (uint16_t version = 0; version < resource.versions.size(); ++version)
        {
            std::vector<uint16_t> producers;
            for (const auto& access : accesses[index][version])
            {
                if (WritesRange(*access.usage))
                {
                    producers.push_back(access.pass);
                }
            }
            Unique(producers);
            if ((producers.empty() && !(resource.imported && version == 0)) ||
                (producers.size() > 1 && !resource.versions[version].atomicMin64))
            {
                outError = "RG2 missing or multiple version producers: " + resource.name;
                return false;
            }
        }
        for (const auto& cell : cells[index])
        {
            bool initialized = resource.imported && resource.state != RHIResourceState::ExternalShared;
            std::vector<uint16_t> writers;
            for (uint16_t version = 0; version < resource.versions.size(); ++version)
            {
                std::vector<uint16_t> next;
                for (const auto& access : accesses[index][version])
                {
                    if (!WritesRange(*access.usage) || !CoversSubresource(index, *access.usage, cell, false))
                    {
                        continue;
                    }
                    if (ReadsRange(*access.usage))
                    {
                        if (!initialized)
                        {
                            outError = "RG2 Modify reads an uninitialized subresource: " + resource.name;
                            return false;
                        }
                        for (const auto writer : writers)
                        {
                            edge(writer, access.pass, index, version, Reason::RAW);
                        }
                    }
                    next.push_back(access.pass);
                }
                if (!next.empty())
                {
                    initialized = true;
                    writers = std::move(next);
                }
                for (const auto& access : accesses[index][version])
                {
                    if (access.usage->access != RGAccessMode::Read ||
                        !CoversSubresource(index, *access.usage, cell, false))
                    {
                        continue;
                    }
                    if (!initialized)
                    {
                        outError = "RG2 Read reaches an uninitialized subresource: " + resource.name;
                        return false;
                    }
                    for (const auto writer : writers)
                    {
                        edge(writer, access.pass, index, version, Reason::RAW);
                    }
                }
            }
            if (resource.imported)
            {
                for (const auto writer : writers)
                {
                    retain(writer);
                }
            }
        }
    }
    std::vector<std::vector<uint16_t>> producers(m_passes.size());
    for (const auto& dependency : dependencies)
    {
        producers[dependency.consumer].push_back(static_cast<uint16_t>(dependency.producer));
    }
    while (!roots.empty())
    {
        const auto pass = roots.back();
        roots.pop_back();
        for (const auto producer : producers[pass])
        {
            retain(producer);
        }
    }
    std::erase_if(dependencies, [&](const Edge& dependency)
                  { return !alive.test(dependency.producer) || !alive.test(dependency.consumer); });
    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        for (const auto& cell : cells[index])
        {
            std::vector<uint16_t> writers, readers;
            for (uint16_t version = 0; version < accesses[index].size(); ++version)
            {
                std::vector<uint16_t> next;
                for (const auto& access : accesses[index][version])
                {
                    if (alive.test(access.pass) && WritesRange(*access.usage) &&
                        CoversSubresource(index, *access.usage, cell, false))
                    {
                        next.push_back(access.pass);
                    }
                }
                for (const auto writer : next)
                {
                    for (const auto prior : writers)
                    {
                        edge(prior, writer, index, version, Reason::WAW);
                    }
                    for (const auto reader : readers)
                    {
                        edge(reader, writer, index, version, Reason::WAR);
                    }
                }
                if (!next.empty())
                {
                    writers = std::move(next);
                    readers.clear();
                }
                for (const auto& access : accesses[index][version])
                {
                    if (alive.test(access.pass) && access.usage->access == RGAccessMode::Read &&
                        CoversSubresource(index, *access.usage, cell, false))
                    {
                        readers.push_back(access.pass);
                    }
                }
            }
        }
    }
    const auto key = [](const Edge& value)
    { return std::tuple{value.producer, value.consumer, value.resource, value.version, value.reason}; };
    std::sort(dependencies.begin(), dependencies.end(), [&](const Edge& a, const Edge& b) { return key(a) < key(b); });
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end(),
                                   [&](const Edge& a, const Edge& b) { return key(a) == key(b); }),
                       dependencies.end());
    m_versionEdges = std::move(dependencies);
    for (size_t pass = 0; pass < m_passes.size(); ++pass)
    {
        m_passes[pass].culled = !alive.test(pass);
    }
    return true;
}

void EnhancedRenderGraph::PlanSubresourceBarriers(const ce::dynamic_bitset* batchEnds, PhaseBarrierPlan* prologue,
                                                  PhaseBarrierPlan* epilogue)
{
    const bool queuePlan = batchEnds != nullptr;
    const auto& order = GetExecuteOrder();
    std::vector<std::vector<SubresourceCell>> cells(m_resources.size());
    std::vector<RHIResourceState> finalStates(m_resources.size());
    ce::dynamic_bitset externalResources(m_resources.size(), false);
    m_stats.barriersEmitted = m_stats.barrierBatches = 0;
    const auto countPlan = [&](const auto& plan, uint32_t repetitions = 1)
    {
        const auto count = plan.transitions.size() + plan.bufferTransitions.size() + plan.uavBarriers.size() +
                           plan.uavBufferBarriers.size();
        m_stats.barriersEmitted += static_cast<uint32_t>(count * repetitions);
        m_stats.barrierBatches += count ? repetitions : 0;
    };
    const auto transition = [&](PhaseBarrierPlan& plan, uint16_t index, SubresourceCell& cell, RHIResourceState after)
    {
        const auto& resource = m_resources[index];
        if (cell.state != after)
        {
            if (resource.IsBuffer())
            {
                plan.bufferTransitions.push_back({resource.buffer, cell.state, after, cell.buffer});
            }
            else
            {
                plan.transitions.push_back({resource.handle, cell.state, after, cell.texture});
            }
        }
        cell.state = after;
    };
    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        auto& resource = m_resources[index];
        if (!resource.used)
        {
            continue;
        }
        finalStates[index] = resource.state;
        if (!queuePlan)
        {
            resource.initialState = resource.state;
        }
        externalResources.set(index, resource.initialState == RHIResourceState::ExternalShared ||
                                         finalStates[index] == RHIResourceState::ExternalShared);
        cells[index] = BuildSubresourceCells(index, true);
        for (auto& cell : cells[index])
        {
            cell.state = resource.initialState;
            if (queuePlan && !externalResources.test(index))
            {
                transition(*prologue, index, cell, RHIResourceState::Common);
            }
        }
    }
    for (auto& pass : m_passes)
    {
        pass.transitions.clear();
        pass.bufferTransitions.clear();
        pass.uavBarriers.clear();
        pass.uavBufferBarriers.clear();
        pass.finalTransitions.clear();
        pass.finalBufferTransitions.clear();
        pass.firstPhaseBarriers.clear();
        pass.repeatPhaseBarriers.clear();
        pass.aliasTextures.clear();
        pass.aliasBuffers.clear();
        pass.poisonBuffers.clear();
        pass.aliasClears.clear();
    }
    const auto planUsages = [&](std::span<const RGPassUsage> usages, PhaseBarrierPlan& plan)
    {
        for (const auto& usage : usages)
        {
            const auto index = usage.handle.index;
            bool uav = false;
            for (auto& cell : cells[index])
            {
                if (!CoversSubresource(index, usage, cell, true))
                {
                    continue;
                }
                const bool siblingAtomic =
                    usage.access == RGAccessMode::AtomicMin64 && cell.atomicVersion == usage.handle.version;
                uav |= cell.state == usage.state && usage.state == RHIResourceState::UnorderedAccess &&
                       !siblingAtomic && (cell.previousWrite || WritesRange(usage));
                transition(plan, index, cell, usage.state);
                cell.previousWrite = WritesRange(usage);
                cell.atomicVersion =
                    usage.access == RGAccessMode::AtomicMin64 ? usage.handle.version : RGHandle::kInvalid;
            }
            if (uav)
            {
                const auto& resource = m_resources[index];
                if (resource.IsBuffer())
                {
                    if (std::ranges::find(plan.uavBufferBarriers, resource.buffer) == plan.uavBufferBarriers.end())
                    {
                        plan.uavBufferBarriers.push_back(resource.buffer);
                    }
                }
                else if (std::ranges::find(plan.uavBarriers, resource.handle) == plan.uavBarriers.end())
                {
                    plan.uavBarriers.push_back(resource.handle);
                }
            }
        }
    };
    for (size_t position = 0; position < order.size(); ++position)
    {
        const auto passIndex = order[position];
        auto& pass = m_passes[passIndex];
        if (pass.repeated)
        {
            for (uint32_t iteration = 0; iteration < (pass.repeatCount > 1 ? 2u : 1u); ++iteration)
            {
                auto& plans = iteration == 0 ? pass.firstPhaseBarriers : pass.repeatPhaseBarriers;
                plans.resize(pass.phases.size());
                for (size_t phase = 0; phase < pass.phases.size(); ++phase)
                {
                    planUsages(pass.phases[phase].usages, plans[phase]);
                    countPlan(plans[phase], iteration == 0 ? 1 : pass.repeatCount - 1);
                }
            }
        }
        else
        {
            PhaseBarrierPlan plan;
            planUsages(pass.usages, plan);
            countPlan(plan);
            pass.transitions = std::move(plan.transitions);
            pass.bufferTransitions = std::move(plan.bufferTransitions);
            pass.uavBarriers = std::move(plan.uavBarriers);
            pass.uavBufferBarriers = std::move(plan.uavBufferBarriers);
        }
        PhaseBarrierPlan final;
        for (uint16_t index = 0; index < m_resources.size(); ++index)
        {
            auto& resource = m_resources[index];
            if (!resource.used || (queuePlan ? !batchEnds->test(passIndex) : resource.lastUse != position))
            {
                continue;
            }
            if (queuePlan && externalResources.test(index))
            {
                continue; // Graphics retains this state until the explicit external release.
            }
            auto target = externalResources.test(index) ? RHIResourceState::CopyDest : RHIResourceState::Common;
            if (!queuePlan)
            {
                for (const auto& requirement : m_finalStateRequirements)
                {
                    if (requirement.handle.index == index)
                    {
                        target = requirement.state;
                    }
                }
            }
            // External import/writeback and transient pool contracts currently
            // carry one state. Converge all cells before returning that state.
            for (auto& cell : cells[index])
            {
                transition(final, index, cell, target);
                cell.previousWrite = true;
                cell.atomicVersion = RGHandle::kInvalid;
            }
            resource.state = target;
        }
        countPlan(final);
        pass.finalTransitions = std::move(final.transitions);
        pass.finalBufferTransitions = std::move(final.bufferTransitions);
    }
    if (queuePlan)
    {
        for (uint16_t index = 0; index < m_resources.size(); ++index)
        {
            if (!m_resources[index].used)
            {
                continue;
            }
            for (auto& cell : cells[index])
            {
                transition(*epilogue, index, cell, finalStates[index]);
            }
            m_resources[index].state = finalStates[index];
        }
        countPlan(*prologue);
        countPlan(*epilogue);
    }
}
