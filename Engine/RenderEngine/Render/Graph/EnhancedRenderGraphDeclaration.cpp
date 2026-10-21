#include "EnhancedRenderGraph.h"
#include "RGSetupTaskGraph.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

RGHandle EnhancedRenderGraph::LocalDeclarationHandle(RGHandle handle) const
{
    if (!handle.IsValid() || handle.epoch == m_resourceEpoch || m_scheduling != RGSchedulingMode::ExplicitVersioned)
    {
        return handle;
    }
    if (m_branch && m_branch->parent)
    {
        return const_cast<EnhancedRenderGraph*>(this)->SnapshotDeclarationResource(handle);
    }
    const auto canonical = CanonicalDeclarationHandle(handle);
    // Preserve an unknown epoch so the ordinary validator diagnoses a stale
    // caller handle. Only handles from a published branch are remapped.
    return canonical.IsValid() ? canonical : handle;
}

RGHandle EnhancedRenderGraph::SnapshotDeclarationResource(RGHandle handle)
{
    auto& state = *m_branch;
    auto& parent = *state.parent;
    std::lock_guard lock{parent.m_branchMutex};
    const auto canonical = parent.CanonicalDeclarationHandle(handle);
    if (!canonical.IsValid() || !parent.ValidVersionHandle(canonical))
    {
        return {};
    }
    for (size_t index = 0; index < state.parentResources.size(); ++index)
    {
        if (state.parentResources[index] == canonical.index)
        {
            return {static_cast<uint16_t>(index), canonical.version, canonical.kind, m_resourceEpoch};
        }
    }
    if (state.merged)
    {
        return {};
    }
    const auto& source = parent.m_resources[canonical.index];
    const auto local = source.imported ? source.IsBuffer()
                                             ? ImportBuffer(source.buffer, source.state, source.name, source.writeback)
                                             : ImportTexture(source.handle, source.state, source.name, source.writeback)
                       : source.IsBuffer() ? CreateBuffer(source.bufferDesc)
                                           : CreateTexture(source.desc);
    if (!local.IsValid())
    {
        throw std::runtime_error("Declaration resource snapshot exceeded capacity.");
    }
    auto& destination = m_resources[local.index];
    destination.desc = source.desc;
    destination.bufferDesc = source.bufferDesc;
    destination.name = source.name;
    destination.initialState = source.initialState;
    destination.firstImport = source.firstImport;
    destination.hasAtomicMin64 = source.hasAtomicMin64;
    destination.versionStorage.assign(source.versions.begin(), source.versions.end());
    destination.versions = destination.versionStorage;
    state.parentResources.resize(local.index + 1, RGHandle::kInvalid);
    state.baseVersions.resize(local.index + 1);
    state.parentResources[local.index] = canonical.index;
    state.baseVersions[local.index] = static_cast<uint16_t>(source.versions.size());
    for (const auto& pass : parent.m_passes)
    {
        for (const auto& usage : pass.usages)
        {
            if (usage.handle.index == canonical.index &&
                (usage.access == RGAccessMode::Write || usage.access == RGAccessMode::ReadWrite ||
                 usage.access == RGAccessMode::AtomicMin64))
            {
                state.knownWriters.push_back({local.index, CopyPacketName(pass.name)});
                break;
            }
        }
    }
    return {local.index, canonical.version, canonical.kind, m_resourceEpoch};
}

RGHandle EnhancedRenderGraph::MapBranchHandle(RGHandle handle) const
{
    handle = LocalDeclarationHandle(handle);
    if (!m_branch || !m_branch->merged || !handle.IsValid() || handle.index >= m_branch->mapping.size())
    {
        return {};
    }
    const auto& mapping = m_branch->mapping[handle.index];
    const uint16_t version = handle.version == RGHandle::kInvalid ? RGHandle::kInvalid
                             : handle.version < mapping.baseVersions
                                 ? handle.version
                                 : static_cast<uint16_t>(mapping.appendedStart + handle.version - mapping.baseVersions);
    return {mapping.resource, version, handle.kind, m_branch->parent->m_resourceEpoch};
}

RGHandle EnhancedRenderGraph::CanonicalDeclarationHandle(RGHandle handle) const
{
    if (m_branch)
    {
        return m_branch->merged ? m_branch->parent->CanonicalDeclarationHandle(handle)
                                : LocalDeclarationHandle(handle);
    }
    if (!handle.IsValid() || handle.epoch == m_resourceEpoch || m_scheduling != RGSchedulingMode::ExplicitVersioned)
    {
        return handle;
    }
    std::lock_guard lock{m_branchMutex};
    for (const auto& branch : m_declarationBranches)
    {
        if (branch->m_resourceEpoch == handle.epoch)
        {
            return branch->MapBranchHandle(handle);
        }
    }
    return {};
}

RGSetupTaskGraph& EnhancedRenderGraph::SetupTasks()
{
    RequireQueueIdle();
    if (m_branch)
    {
        throw std::logic_error("A node branch cannot admit graph-owner setup work");
    }
    if (!m_setupTasks)
    {
        m_setupTasks = std::make_unique<RGSetupTaskGraph>(ce::get_job_scheduler());
        if (m_compiled)
        {
            m_setupTasks->Join(RGSetupWaitPoint::Compile);
        }
    }
    return *m_setupTasks;
}

std::shared_ptr<EnhancedRenderGraph> EnhancedRenderGraph::CreateDeclarationBranch(std::span<const RGHandle> inputs)
{
    ce::profile_scope profile{ce::marker<"RenderGraphDeclareNodeSnapshot">()};
    RequireQueueIdle();
    std::lock_guard lock{m_branchMutex};
    if (m_scheduling != RGSchedulingMode::ExplicitVersioned)
    {
        throw std::logic_error("Declaration branches require versioned resources");
    }
    if (m_collectingDeclarationPackets)
    {
        NextDeclarationPacket();
    }
    JoinDeclarationPackets();
    std::shared_ptr<EnhancedRenderGraph> branch;
    if (m_nextDeclarationBranch < m_declarationBranchPool.size())
    {
        branch = m_declarationBranchPool[m_nextDeclarationBranch++];
    }
    else
    {
        branch = std::shared_ptr<EnhancedRenderGraph>{
            new EnhancedRenderGraph{*m_deviceServices, m_scheduling, m_orderPolicy, true}};
        m_declarationBranchPool.push_back(branch);
        ++m_nextDeclarationBranch;
    }
    auto& state = *branch->m_branch;
    state.merged = false;
    state.baseResources = 0;
    state.mapping.clear();
    state.baseVersions.clear();
    state.parentResources.clear();
    state.knownWriters.clear();
    state.parent = this;
    state.baseVersions.reserve(inputs.size());
    state.parentResources.reserve(inputs.size());
    for (const auto input : inputs)
    {
        const auto canonical = CanonicalDeclarationHandle(input);
        if (!canonical.IsValid())
        {
            continue;
        }
        if (!ValidVersionHandle(canonical))
        {
            throw std::invalid_argument("Declaration snapshot received a stale resource");
        }
        if (std::find(state.parentResources.begin(), state.parentResources.end(), canonical.index) !=
            state.parentResources.end())
        {
            continue;
        }
        const auto index = state.parentResources.size();
        const auto& source = m_resources[canonical.index];
        const auto handle =
            source.imported ? source.IsBuffer()
                                  ? branch->ImportBuffer(source.buffer, source.state, source.name, source.writeback)
                                  : branch->ImportTexture(source.handle, source.state, source.name, source.writeback)
            : source.IsBuffer() ? branch->CreateBuffer(source.bufferDesc)
                                : branch->CreateTexture(source.desc);
        if (handle.index != index)
        {
            throw std::runtime_error("Declaration snapshot changed a resource identity");
        }
        auto& destination = branch->m_resources[index];
        destination.desc = source.desc;
        destination.bufferDesc = source.bufferDesc;
        destination.name = source.name;
        destination.initialState = source.initialState;
        destination.firstImport = source.firstImport;
        destination.hasAtomicMin64 = source.hasAtomicMin64;
        destination.versionStorage.assign(source.versions.begin(), source.versions.end());
        destination.versions = destination.versionStorage;
        state.baseVersions.push_back(static_cast<uint16_t>(source.versions.size()));
        state.parentResources.push_back(canonical.index);
    }
    state.baseResources = state.parentResources.size();
    for (size_t index = 0; index < m_passes.size(); ++index)
    {
        const auto& pass = m_passes[index];
        for (const auto& usage : pass.usages)
        {
            if (usage.access == RGAccessMode::Write || usage.access == RGAccessMode::ReadWrite ||
                usage.access == RGAccessMode::AtomicMin64)
            {
                for (size_t resource = 0; resource < state.parentResources.size(); ++resource)
                {
                    if (state.parentResources.data()[resource] == usage.handle.index)
                    {
                        state.knownWriters.push_back({static_cast<uint16_t>(resource), CopyPacketName(pass.name)});
                        break;
                    }
                }
            }
        }
    }
    return branch;
}

void EnhancedRenderGraph::MergeDeclarationBranch(const std::shared_ptr<EnhancedRenderGraph>& branch)
{
    ce::profile_scope profile{ce::marker<"RenderGraphDeclareNodePublish">()};
    RequireQueueIdle();
    std::lock_guard lock{m_branchMutex};
    if (!branch || !branch->m_branch || branch->m_branch->parent != this || branch->m_branch->merged ||
        branch->m_collectingDeclarationPackets || !branch->m_declarationError.empty())
    {
        throw std::logic_error("Declaration branch is failed, unsealed, foreign or already published");
    }
    auto& state = *branch->m_branch;
    state.mapping.resize(branch->m_resources.size());
    for (size_t index = 0; index < branch->m_resources.size(); ++index)
    {
        const auto& source = branch->m_resources[index];
        RGHandle destination;
        uint16_t baseVersions{};
        if (index < state.parentResources.size() && state.parentResources[index] != RGHandle::kInvalid)
        {
            destination = VersionHandle(state.parentResources[index], 0);
            baseVersions = state.baseVersions[index];
        }
        else if (source.imported)
        {
            destination = source.IsBuffer() ? ImportBuffer(source.buffer, source.state, source.name, source.writeback)
                                            : ImportTexture(source.handle, source.state, source.name, source.writeback);
            baseVersions = m_scheduling == RGSchedulingMode::ExplicitVersioned ? 1 : 0;
        }
        else
        {
            destination = source.IsBuffer() ? CreateBuffer(source.bufferDesc) : CreateTexture(source.desc);
        }
        if (!destination.IsValid())
        {
            throw std::runtime_error("Declaration publication exceeded graph capacity");
        }
        auto& resource = m_resources[destination.index];
        auto& mapping = state.mapping[index];
        mapping = {destination.index, baseVersions, static_cast<uint16_t>(resource.versions.size())};
        for (size_t version = baseVersions; version < source.versions.size(); ++version)
        {
            auto value = source.versions[version];
            if (value.parent != RGHandle::kInvalid && value.parent >= baseVersions)
            {
                value.parent = static_cast<uint16_t>(mapping.appendedStart + value.parent - baseVersions);
            }
            AppendDeclarationVersion(destination.index, value);
        }
        if (source.hasAtomicMin64 && !resource.hasAtomicMin64)
        {
            resource.hasAtomicMin64 = true;
            m_atomicResources.push_back(destination.index);
        }
    }
    // The mapping is immutable before callbacks are retained. No worker writes
    // this graph; its owner publishes nodes in the original node order.
    state.merged = true;
    m_declarationBranches.push_back(branch);
    const auto mapPhaseUsages = [&](std::span<const RGPassUsage> input)
    {
        auto output = m_declarationData.copy(input);
        auto* writable = const_cast<RGPassUsage*>(output.data());
        for (size_t index = 0; index < output.size(); ++index)
        {
            writable[index].handle = branch->MapBranchHandle(writable[index].handle);
        }
        return output;
    };
    for (uint32_t index = 0; index < branch->m_passes.size(); ++index)
    {
        const auto& source = branch->m_passes[index];
        const auto* owner = branch.get();
        RGPassId id;
        if (source.repeated)
        {
            // These views and mapped usages share the graph's reusable arenas.
            // No per-publication phase/name/usage vectors or heap blocks remain.
            auto* phases = source.phases.empty()
                               ? nullptr
                               : static_cast<RepeatedPhaseView*>(m_packetArena.allocate(
                                     source.phases.size() * sizeof(RepeatedPhaseView), alignof(RepeatedPhaseView)));
            for (size_t phase = 0; phase < source.phases.size(); ++phase)
            {
                std::construct_at(phases + phase, RepeatedPhaseView{source.phases[phase].name,
                                                                    mapPhaseUsages(source.phases[phase].usages)});
            }
            id = AddRepeatedPassView(
                source.name, source.usages, std::span<const RepeatedPhaseView>{phases, source.phases.size()},
                source.repeatCount,
                [owner, index](const ExecuteContext& context, uint32_t iteration, uint32_t phase)
                {
                    const ExecuteContext local{owner, context.encoder};
                    owner->m_passes[index].repeatedExecute(local, iteration, phase);
                },
                source.hasSideEffect, source.recordCost);
        }
        else if (source.splitExecute)
        {
            id = AddPass(source.name, source.usages, {}, source.hasSideEffect);
            if (id.IsValid())
            {
                auto& pass = m_passes[id.index];
                pass.maxSlices = source.maxSlices;
                pass.splitExecute = [owner, index](const ExecuteContext& context, uint32_t slice, uint32_t count)
                {
                    const ExecuteContext local{owner, context.encoder};
                    owner->m_passes[index].splitExecute(local, slice, count);
                };
            }
        }
        else
        {
            // SetDeclarationUsages copies once into the root declaration arena
            // and canonicalizes the published branch handles in that owned copy.
            id = AddPass(
                source.name, source.usages,
                [owner, index](const ExecuteContext& context)
                {
                    const ExecuteContext local{owner, context.encoder};
                    if (owner->m_passes[index].execute)
                    {
                        owner->m_passes[index].execute(local);
                    }
                },
                source.hasSideEffect);
        }
        if (!id.IsValid())
        {
            throw std::runtime_error("Declaration publication exceeded pass capacity");
        }
        auto& pass = m_passes[id.index];
        pass.computeCompatible = source.computeCompatible;
        pass.recordingSideEffect = source.recordingSideEffect;
        pass.recordingIndependent = source.recordingIndependent;
        pass.recordCost = source.recordCost;
    }
    for (const auto& requirement : branch->m_finalStateRequirements)
    {
        RequireImportedFinalState(branch->MapBranchHandle(requirement.handle), requirement.state);
    }
}
