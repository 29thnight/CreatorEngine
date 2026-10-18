#include "../../../Utility_Framework/DynamicBitset.h"
#include "EnhancedRenderGraph.h"
#include "../../RHI/IRenderDeviceServices.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

#include <stdint.h>
#include "../../../Utility_Framework/StandardLibrary.h"

namespace
{
    void AuditStructuralMiss(const std::string& current, const std::shared_ptr<const std::string>& previous,
        const std::shared_ptr<const std::string>& alternate)
    {
        static const std::string prefix = []
        {
            char* value{};
            size_t length{};
            std::string result;
            if (_dupenv_s(&value, &length, "CREATOR_RENDERGRAPH_CACHE_AUDIT") == 0 && value)
            {
                result = value;
            }
            std::free(value);
            return result;
        }();
        if (prefix.empty())
        {
            return;
        }
        ce::profile_scope profile{ce::marker<"RenderGraphCacheAudit">()};
        static thread_local uint64_t serial{};
        const auto path = prefix + "." + std::to_string(++serial);
        const auto write = [&](const char* suffix, const std::string& data)
        {
            std::ofstream file(path + suffix, std::ios::binary);
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
        };
        write(".current.bin", current);
        if (previous)
        {
            write(".previous.bin", *previous);
        }
        if (alternate)
        {
            write(".alternate.bin", *alternate);
        }
    }

    template<class Values>
    void ClearNonempty(Values& values)
    {
        if (!values.empty())
        {
            values.clear();
        }
    }

    class TransientPreparationTimer
    {
    public:
        explicit TransientPreparationTimer(double& milliseconds)
            : m_milliseconds(milliseconds), m_start(std::chrono::steady_clock::now())
        {
        }
        ~TransientPreparationTimer()
        {
            m_milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_start).count();
        }
        TransientPreparationTimer(const TransientPreparationTimer&) = delete;
        TransientPreparationTimer& operator=(const TransientPreparationTimer&) = delete;
    private:
        double& m_milliseconds;
        std::chrono::steady_clock::time_point m_start;
    };

    RGTransientPool::AllocationKey TransientAllocationKey(const RHITransientResourceDesc& desc)
    {
        // Graph descriptions normalize dimension, mip count and creation state.
        // Clear values affect placed-resource identity, but not allocation size.
        const auto& texture = desc.textureDesc;
        const auto& buffer = desc.bufferDesc;
        return {desc.buffer ? 1ull : 0ull, desc.buffer ? buffer.bytes : texture.width,
            desc.buffer ? 0ull : texture.height, desc.buffer ? 0ull : texture.depthOrArraySize,
            desc.buffer ? 0ull : static_cast<uint64_t>(texture.format),
            desc.buffer ? uint64_t(buffer.allowUnorderedAccess) | (uint64_t(buffer.allowIndirectArguments) << 1) :
                uint64_t(texture.allowRenderTarget) | (uint64_t(texture.allowDepthStencil) << 1) |
                (uint64_t(texture.allowUnorderedAccess) << 2)};
    }

    bool SamePlacedDescription(const RHITransientResourceDesc& left, const RHITransientResourceDesc& right)
    {
        return TransientAllocationKey(left) == TransientAllocationKey(right) &&
            (left.buffer || (left.textureDesc.clearDepth == right.textureDesc.clearDepth &&
                std::equal(std::begin(left.textureDesc.clearColor), std::end(left.textureDesc.clearColor),
                    std::begin(right.textureDesc.clearColor))));
    }
    // An address may be reused while a packet still retains an old graph output.
    // Epochs must distinguish both Reset and reconstruction at the same address.
    uint64_t NextResourceEpoch()
    {
        static std::atomic<uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t NextRenderGraphCompileGeneration()
    {
        static std::atomic<uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }

    // 쓰기로 보는 상태. 의존성 유도와 컬링이 이 구분을 쓴다.
    bool IsWriteState(RHIResourceState state)
    {
        switch (state)
        {
        case RHIResourceState::RenderTarget:
        case RHIResourceState::DepthWrite:
        case RHIResourceState::UnorderedAccess:
        case RHIResourceState::CopyDest:
            return true;
        default:
            return false;
        }
    }
    bool Writes(const EnhancedRenderGraph::RGPassUsage& usage)
    {
        return usage.access == RGAccessMode::LegacyState ? IsWriteState(usage.state)
            : usage.access == RGAccessMode::Write || usage.access == RGAccessMode::ReadWrite || usage.access == RGAccessMode::AtomicMin64;
    }
    bool Reads(const EnhancedRenderGraph::RGPassUsage& usage)
    {
        return usage.access == RGAccessMode::LegacyState ? !IsWriteState(usage.state)
            : usage.access == RGAccessMode::Read || usage.access == RGAccessMode::ReadWrite || usage.access == RGAccessMode::AtomicMin64;
    }
}

RHITextureHandle EnhancedRenderGraph::ExecuteContext::ResolveHandle(RGHandle handle) const
{
    if (nullptr == graph || !handle.IsValid()) return {};
    if (handle.index >= graph->m_resources.size()) return {};
    if (graph->m_scheduling == RGSchedulingMode::ExplicitVersioned && !graph->ValidVersionHandle(handle)) return {};

    const Resource& resource = graph->m_resources[handle.index];
    return resource.IsBuffer() ? RHITextureHandle{} : resource.handle;
}

void EnhancedRenderGraph::Reset()
{
    ce::profile_scope profile{ce::marker<"RenderGraphReset">()};
    RequireQueueIdle();
    ReleaseResources();
    m_recordedRecording = 0;
    m_statesCommitted = false;
    m_queueExecutionAttempted = false;
    m_queueDiagnostics = {};
    m_measurementDomain = RGMeasurementDomain::Normal;
    m_queueExecutionMode = 0;
    // 현재 native 자원을 해제한 뒤 모든 필드를 비운다. Resource와 버전 vector의 수명을 유지해
    // 다음 선언에서 checked-iterator proxy까지 다시 생성하는 비용을 피한다.
    const Resource emptyResource{};
    for (auto& resource : m_resources)
    {
        resource = emptyResource;
    }
    m_resources = {};
    m_importedTextures.clear();
    m_importedBuffers.clear();
    m_atomicResources.clear();
    m_activeStructuralSchedule.reset();
    m_executeOrderOverridden = false;
    for (auto& pass : m_passes)
    {
        ClearPassMetadata(pass);
    }
    m_passes = {};
    m_finalStateRequirements.clear();
    m_executeOrder.clear();
    m_timingSignature.reset();
    m_versionEdges.clear();
    m_dependencyWaves.clear();
    m_criticalPath.clear();
    m_compiled = false;
    m_declarationError.clear();
    m_stats = Stats{};
    m_resourceEpoch = NextResourceEpoch();
    m_preparedPool = nullptr;
    m_preparedRecording = m_preparedDescriptors = 0;
    m_preparedRecordingConsumed = false;
}

RHIBufferHandle EnhancedRenderGraph::ResolveBufferHandle(RGHandle handle) const
{
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned && !ValidVersionHandle(handle)) return {};
    return handle.IsValid() && handle.index < m_resources.size() ? m_resources[handle.index].buffer : RHIBufferHandle{};
}

RGHandle EnhancedRenderGraph::FindImportedBuffer(RHIBufferHandle buffer) const
{
    const auto found = m_importedBuffers.find(buffer.id);
    if (buffer.IsValid() && found != m_importedBuffers.end())
    {
        return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(found->second, 0) : RGHandle{found->second};
    }
    return {};
}

RGHandle EnhancedRenderGraph::FindImportedTexture(RHITextureHandle texture) const
{
    const auto found = m_importedTextures.find(texture.id);
    if (texture.IsValid() && found != m_importedTextures.end())
    {
        return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(found->second, 0) : RGHandle{found->second};
    }
    return {};
}

bool EnhancedRenderGraph::PrepareParallel(IRHIParallelCommandPool& pool, std::string& outError)
{
    if (m_preparedPool || !m_resources.empty() || !m_passes.empty() || m_compiled || !pool.IsInitialized() ||
        m_deviceServices->GetCurrentUploadRecordingId() == 0 || thread_pool::is_worker_thread())
    {
        outError = "Parallel preparation needs an empty graph, initialized pool and an active owner recording.";
        return false;
    }
    if (!pool.Prepare(outError))
    {
        return false;
    }
    m_preparedPool = &pool;
    m_preparedRecording = m_deviceServices->GetCurrentUploadRecordingId();
    m_preparedDescriptors = m_deviceServices->GetDescriptorVersionToken();
    outError.clear();
    return true;
}

bool EnhancedRenderGraph::CheckDeclarationCapacity(std::size_t count, uint16_t limit,
    const char* kind, const std::string& name)
{
    if (!m_declarationError.empty())
    {
        return false;
    }
    if (count >= limit)
    {
        // 무효 ID로 감싼 뒤 검사하면 핸들이 이전 자원을 가리킬 수 있다.
        // 반환값을 무시해도 잘린 그래프를 Compile/Execute할 수 없도록 잠근다.
        m_declarationError = "RenderGraph " + std::string(kind) + " declaration limit (" +
            std::to_string(limit) + ") exceeded before '" + name + "'";
        m_compiled = false;
        return false;
    }
    return true;
}

EnhancedRenderGraph::Resource& EnhancedRenderGraph::AppendResource()
{
    const auto index = m_resources.size();
    if (index == m_resourceStorage.size())
    {
        m_resourceStorage.emplace_back();
    }
    m_resources = std::span{m_resourceStorage}.first(index + 1);
    return m_resources[index];
}

RGHandle EnhancedRenderGraph::ImportBuffer(RHIBufferHandle resource,
    RHIResourceState currentState, const std::string& name,
    RHIResourceState* stateWriteback)
{
    RequireQueueIdle();
    RGHandle handle{};
    if (!resource.IsValid()) return handle;
    if (!CheckDeclarationCapacity(m_resources.size(), RGHandle::kInvalid, "resource", name))
    {
        return {};
    }

    auto& entry = AppendResource();
    entry.buffer = resource;
    entry.bufferKind = true;
    entry.state = currentState;
    entry.writeback = stateWriteback;
    entry.imported = true;
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned) entry.versions.push_back({RGHandle::kInvalid, false});
    entry.name = name;

    handle.index = static_cast<uint16_t>(m_resources.size() - 1);
    entry.firstImport = m_importedBuffers.try_emplace(resource.id, handle.index).second;
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(handle.index,
        m_resources[handle.index].imported ? 0 : RGHandle::kInvalid) : handle;
}

RGHandle EnhancedRenderGraph::ImportTexture(RHITextureHandle resource,
    RHIResourceState currentState, const std::string& name,
    RHIResourceState* stateWriteback)
{
    RequireQueueIdle();
    RGHandle handle{};
    if (!resource.IsValid()) return handle;
    if (!CheckDeclarationCapacity(m_resources.size(), RGHandle::kInvalid, "resource", name))
    {
        return {};
    }

    auto& entry = AppendResource();
    entry.handle = resource;
    entry.state = currentState;
    entry.writeback = stateWriteback;
    entry.imported = true;
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned) entry.versions.push_back({RGHandle::kInvalid, false});
    entry.name = name;

    handle.index = static_cast<uint16_t>(m_resources.size() - 1);
    entry.firstImport = m_importedTextures.try_emplace(resource.id, handle.index).second;
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(handle.index,
        m_resources[handle.index].imported ? 0 : RGHandle::kInvalid) : handle;
}

RGHandle EnhancedRenderGraph::CreateTexture(const RGTextureDesc& desc)
{
    RequireQueueIdle();
    if (!CheckDeclarationCapacity(m_resources.size(), RGHandle::kInvalid, "resource", desc.name))
    {
        return {};
    }
    auto& entry = AppendResource();
    entry.desc = desc;
    entry.state = RHIResourceState::Common;
    entry.imported = false;
    entry.name = desc.name;

    RGHandle handle{};
    handle.index = static_cast<uint16_t>(m_resources.size() - 1);
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(handle.index,
        m_resources[handle.index].imported ? 0 : RGHandle::kInvalid) : handle;
}

RGHandle EnhancedRenderGraph::CreateBuffer(const RGBufferDesc& desc)
{
    RequireQueueIdle();
    if (!CheckDeclarationCapacity(m_resources.size(), RGHandle::kInvalid, "resource", desc.name))
    {
        return {};
    }
    auto& entry = AppendResource();
    entry.bufferDesc = desc;
    entry.bufferKind = true;
    entry.name = desc.name;
    const auto index = static_cast<uint16_t>(m_resources.size() - 1);
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned
        ? VersionHandle(index, RGHandle::kInvalid) : RGHandle{index};
}

struct RGTransientPool::AliasHeapAccounting
{
    AliasHeapAccounting()
    {
        static std::atomic<uint64_t> next{1};
        memory.domainId = next.fetch_add(1, std::memory_order_relaxed);
    }
    mutable std::mutex mutex;
    AliasHeapMemory memory;
};

RGTransientPool::AliasHeapMemory RGTransientPool::GetAliasHeapMemory() const
{
    if (!aliasHeapAccounting)
    {
        return {};
    }
    std::lock_guard lock{aliasHeapAccounting->mutex};
    auto result = aliasHeapAccounting->memory;
    result.leasedBytes = result.retainedBytes - result.cachedBytes;
    result.leasedHeaps = result.retainedHeaps - result.cachedHeaps;
    return result;
}

RGTransientPool::AliasHeapMemory EnhancedRenderGraph::GetAliasHeapMemory() const
{
    if (m_transientPool)
    {
        return m_transientPool->GetAliasHeapMemory();
    }
    if (!m_aliasHeapAccounting)
    {
        return {};
    }
    std::lock_guard lock{m_aliasHeapAccounting->mutex};
    auto result = m_aliasHeapAccounting->memory;
    result.leasedBytes = result.retainedBytes;
    result.leasedHeaps = result.retainedHeaps;
    return result;
}

void RGTransientPool::AliasedGroup::TrackHeap(const std::shared_ptr<AliasHeapAccounting>& accounting)
{
    if (!heap || !accounting || m_accounting)
    {
        throw std::logic_error("Alias heap accounting requires one successful native heap registration");
    }
    m_accounting = accounting;
    std::lock_guard lock{m_accounting->mutex};
    auto& memory = m_accounting->memory;
    memory.retainedBytes += allocation.bytes;
    ++memory.retainedHeaps;
    memory.peakRetainedBytes = (std::max)(memory.peakRetainedBytes, memory.retainedBytes);
}

void RGTransientPool::AliasedGroup::SetCached(bool cached)
{
    std::lock_guard lock{m_accounting->mutex};
    if (m_cached == cached)
    {
        return;
    }
    auto& memory = m_accounting->memory;
    if (cached)
    {
        memory.cachedBytes += allocation.bytes;
        ++memory.cachedHeaps;
    }
    else
    {
        memory.cachedBytes -= allocation.bytes;
        --memory.cachedHeaps;
    }
    m_cached = cached;
}

RGTransientPool::AliasedGroup::~AliasedGroup()
{
    for (const auto& entry : entries)
    {
        if (entry.texture.IsValid())
        {
            owner->ReleaseTexture(entry.texture);
        }
        if (entry.buffer.IsValid())
        {
            owner->ReleaseBuffer(entry.buffer);
        }
    }
    heap.reset();
    if (m_accounting)
    {
        std::lock_guard lock{m_accounting->mutex};
        auto& memory = m_accounting->memory;
        if (m_cached)
        {
            memory.cachedBytes -= allocation.bytes;
            --memory.cachedHeaps;
        }
        memory.retainedBytes -= allocation.bytes;
        --memory.retainedHeaps;
    }
}

void RGTransientPool::ClearAliasingCache()
{
    for (const auto& group : freeAliasedGroups)
    {
        group->SetCached(false);
    }
    freeAliasedGroups.clear();
    allocationCache.clear();
    freeAliasBytes = 0;
    aliasOwner = nullptr;
}

RGHandle EnhancedRenderGraph::VersionHandle(uint16_t index, uint16_t version) const
{
    if (index >= m_resources.size()) return {};
    return {index, version, m_resources[index].IsBuffer() ? RGResourceKind::Buffer : RGResourceKind::Texture, m_resourceEpoch};
}

bool EnhancedRenderGraph::ValidVersionHandle(RGHandle h, bool allowUnwritten) const
{
    if (!h.IsValid() || h.index >= m_resources.size() || h.epoch != m_resourceEpoch) return false;
    const auto& r = m_resources[h.index];
    if (h.kind != (r.IsBuffer() ? RGResourceKind::Buffer : RGResourceKind::Texture)) return false;
    return h.version < r.versions.size() || (allowUnwritten && !r.imported && h.version == RGHandle::kInvalid);
}

RGHandle EnhancedRenderGraph::AdvanceVersion(RGHandle previous, bool modify)
{
    RequireQueueIdle();
    m_compiled = false;
    if (m_scheduling != RGSchedulingMode::ExplicitVersioned || !ValidVersionHandle(previous, !modify)) return {};
    auto& r = m_resources[previous.index];
    if (r.versions.size() >= RGHandle::kInvalid) return {};
    const auto version = static_cast<uint16_t>(r.versions.size());
    // Keep fork declarations for Compile to reject, rather than silently
    // choosing whichever writer happened to call this API first.
    r.versions.push_back({previous.version, modify});
    return VersionHandle(previous.index, version);
}

bool EnhancedRenderGraph::HasDeclaredWriter(RGHandle resource, const std::string& producer) const
{
    if (!ValidVersionHandle(resource)) return false;
    for (const auto& pass : m_passes) if (pass.name == producer)
        for (const auto& use : std::span{pass.usages})
            if (use.handle.index == resource.index && use.handle.kind == resource.kind &&
                use.handle.epoch == resource.epoch &&
                (use.access == RGAccessMode::Write || use.access == RGAccessMode::ReadWrite))
                return true;
    return false;
}

RGHandle EnhancedRenderGraph::Write(RGHandle previous) { return AdvanceVersion(previous, false); }
RGHandle EnhancedRenderGraph::Modify(RGHandle previous) { return AdvanceVersion(previous, true); }
RGHandle EnhancedRenderGraph::AtomicMin64(RGHandle previous)
{
    if (!ValidVersionHandle(previous) || previous.kind != RGResourceKind::Buffer) return {};
    const auto result = AdvanceVersion(previous, true);
    if (result.IsValid())
    {
        auto& resource = m_resources[result.index];
        resource.versions[result.version].atomicMin64 = true;
        if (!resource.hasAtomicMin64)
        {
            m_atomicResources.push_back(result.index);
            resource.hasAtomicMin64 = true;
        }
    }
    return result;
}

void EnhancedRenderGraph::RequireImportedFinalState(RGHandle handle, RHIResourceState state)
{
    RequireQueueIdle();
    m_finalStateRequirements.push_back({handle, state});
    m_compiled = false;
}

EnhancedRenderGraph::Pass& EnhancedRenderGraph::AppendPass()
{
    const auto index = m_passes.size();
    if (index == m_passStorage.size())
    {
        m_passStorage.emplace_back();
    }
    m_passes = std::span{m_passStorage}.first(index + 1);
    return m_passes[index];
}

void EnhancedRenderGraph::ClearPassMetadata(Pass& pass)
{
    // Checked vector iterators use the Debug STL global lock. Empty containers
    // have nothing to orphan; retain their proxies and allocation across frames.
    pass.execute = {};
    pass.splitExecute = {};
    pass.repeatedExecute = {};
    ClearNonempty(pass.name);
    ClearNonempty(pass.usages);
    ClearNonempty(pass.transitions);
    ClearNonempty(pass.bufferTransitions);
    ClearNonempty(pass.uavBarriers);
    ClearNonempty(pass.uavBufferBarriers);
    ClearNonempty(pass.finalTransitions);
    ClearNonempty(pass.finalBufferTransitions);
    ClearNonempty(pass.aliasTextures);
    ClearNonempty(pass.aliasBuffers);
    ClearNonempty(pass.poisonBuffers);
    ClearNonempty(pass.aliasClears);
    ClearNonempty(pass.phases);
    ClearNonempty(pass.firstPhaseBarriers);
    ClearNonempty(pass.repeatPhaseBarriers);
    pass.maxSlices = 1;
    pass.recordCost = 0;
    pass.hasSideEffect = false;
    pass.culled = false;
    pass.computeCompatible = false;
    pass.recordingSideEffect = false;
    pass.recordingIndependent = false;
    pass.repeatCount = 1;
    pass.repeated = false;
}

RGPassId EnhancedRenderGraph::InitializePass(const std::string& name, bool hasSideEffect)
{
    RequireQueueIdle();
    if (!CheckDeclarationCapacity(m_passes.size(), RGPassId::kInvalid, "pass", name))
    {
        return {};
    }
    // Construct vector-owning metadata in place. Moving a temporary Pass also
    // constructs every checked-iterator proxy again in a Debug build.
    auto& pass = AppendPass();
    pass.name = name;
    pass.hasSideEffect = hasSideEffect;

    RGPassId id{};
    id.index = static_cast<uint16_t>(m_passes.size() - 1);
    m_compiled = false;
    return id;
}

RGPassId EnhancedRenderGraph::AddPass(const std::string& name,
    const std::vector<RGPassUsage>& usages, ExecuteCallback execute, bool hasSideEffect)
{
    const auto id = InitializePass(name, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.usages = usages;
    pass.execute = std::move(execute);
    return id;
}

RGPassId EnhancedRenderGraph::AddPass(const std::string& name,
    std::vector<RGPassUsage>&& usages, ExecuteCallback execute, bool hasSideEffect)
{
    const auto id = InitializePass(name, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.usages = std::move(usages);
    pass.execute = std::move(execute);
    return id;
}

RGPassId EnhancedRenderGraph::AddPass(const std::string& name,
    std::initializer_list<RGPassUsage> usages, ExecuteCallback execute, bool hasSideEffect)
{
    const auto id = InitializePass(name, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.usages.assign(usages.begin(), usages.end());
    pass.execute = std::move(execute);
    return id;
}

RGPassId EnhancedRenderGraph::AddRepeatedPass(const std::string& name,
    std::vector<RGPassUsage> usages, std::vector<RepeatedPhase> phases,
    uint32_t repeatCount, RepeatedExecuteCallback execute, bool hasSideEffect, uint32_t recordCost)
{
    RequireQueueIdle();
    const auto id = AddPass(name, std::move(usages), {}, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.repeated = true;
    pass.phases = std::move(phases);
    pass.repeatCount = repeatCount;
    pass.repeatedExecute = std::move(execute);
    pass.recordCost = recordCost;
    return id;
}

RGPassId EnhancedRenderGraph::AddSplitPass(const std::string& name,
    const std::vector<RGPassUsage>& usages, SplitExecuteCallback execute,
    uint32_t maxSlices, bool hasSideEffect, uint32_t recordCost)
{
    const auto id = InitializePass(name, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.usages = usages;
    pass.splitExecute = std::move(execute);
    pass.maxSlices = (std::max)(1u, maxSlices);
    pass.recordCost = recordCost;
    return id;
}

RGPassId EnhancedRenderGraph::AddSplitPass(const std::string& name,
    std::vector<RGPassUsage>&& usages, SplitExecuteCallback execute,
    uint32_t maxSlices, bool hasSideEffect, uint32_t recordCost)
{
    const auto id = InitializePass(name, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.usages = std::move(usages);
    pass.splitExecute = std::move(execute);
    pass.maxSlices = (std::max)(1u, maxSlices);
    pass.recordCost = recordCost;
    return id;
}

RGPassId EnhancedRenderGraph::AddSplitPass(const std::string& name,
    std::initializer_list<RGPassUsage> usages, SplitExecuteCallback execute,
    uint32_t maxSlices, bool hasSideEffect, uint32_t recordCost)
{
    const auto id = InitializePass(name, hasSideEffect);
    if (!id.IsValid())
    {
        return {};
    }
    auto& pass = m_passes[id.index];
    pass.usages.assign(usages.begin(), usages.end());
    pass.splitExecute = std::move(execute);
    pass.maxSlices = (std::max)(1u, maxSlices);
    pass.recordCost = recordCost;
    return id;
}

bool EnhancedRenderGraph::BuildExplicitOrder(std::string& outError)
{
    ce::profile_scope orderProfile{ce::marker<"RenderGraphDependencyOrder">()};
    m_executeOrder.clear();
    const size_t count = m_passes.size();
    if (count > RGPassId::kInvalid || m_resources.size() > RGHandle::kInvalid)
    { outError = "RG1 declaration exceeds handle capacity"; return false; }
    struct Edge { uint16_t to, resource; uint16_t version{0}; };
    std::vector<std::vector<Edge>> edges(count);
    std::vector<uint32_t> incoming(count, 0);
    ce::dynamic_bitset alive(count, true);
    m_versionEdges.clear();
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned)
    {
        using Reason = DiagnosticSnapshot::VersionEdge::Reason;
        // Flatten cold-compile access lists instead of allocating a Debug STL
        // container/proxy for every resource version and its producer/consumer.
        std::vector<size_t> versionOffsets(m_resources.size() + 1, 0);
        for (size_t resource = 0; resource < m_resources.size(); ++resource)
        {
            versionOffsets[resource + 1] = versionOffsets[resource] + m_resources[resource].versions.size();
        }
        const size_t versionCount = versionOffsets.back();
        std::vector<size_t> producerCounts(versionCount, 0), consumerCounts(versionCount, 0);
        const auto fail = [&](const std::string& why, size_t r, uint16_t v) {
            outError = "RG2 " + why + ": " + m_resources[r].name + " v" + std::to_string(v); return false;
        };
        for (size_t r=0; r<m_resources.size(); ++r)
        {
            const auto& resource=m_resources[r];
            // One physical storage must have one logical version chain.
            if (resource.imported && !resource.firstImport)
            {
                return fail("duplicate physical import; reuse its handle",r,0);
            }
            for (size_t v=resource.imported ? 1 : 0; v<resource.versions.size(); ++v)
            {
                const auto parent=resource.versions[v].parent;
                if (parent != (v==0 ? RGHandle::kInvalid : v-1))
                    return fail("forked/stale write parent",r,static_cast<uint16_t>(v));
                if (resource.versions[v].modify && parent==RGHandle::kInvalid)
                    return fail("Modify has no initialized input",r,static_cast<uint16_t>(v));
            }
        }
        for (size_t p=0; p<count; ++p)
        {
            std::vector<uint16_t> seen;
            for (const auto& u:std::span{m_passes[p].usages})
            {
                const auto bad = [&](const std::string& why) {
                    outError="RG2 pass '"+m_passes[p].name+"': "+why;
                    if(u.handle.index<m_resources.size()) outError += ": "+m_resources[u.handle.index].name;
                    outError += " [resource="+std::to_string(u.handle.index)+", v"+std::to_string(u.handle.version)+"]";
                    return false;
                };
                if (!ValidVersionHandle(u.handle)) return bad("stale/foreign/invalid resource version or kind");
                const auto r=u.handle.index, v=u.handle.version;
                if (std::ranges::find(std::span{seen}, r)!=std::span{seen}.end()) return bad("duplicate resource access; use Modify: "+m_resources[r].name);
                seen.push_back(r);
                if (u.access!=RGAccessMode::Read && u.access!=RGAccessMode::Write && u.access!=RGAccessMode::ReadWrite && u.access!=RGAccessMode::AtomicMin64)
                    return bad("explicit access required");
                if (u.state<RHIResourceState::Common || u.state>RHIResourceState::VertexAndShaderResource ||
                    ((u.state==RHIResourceState::IndirectArgument || u.state==RHIResourceState::VertexAndShaderResource) &&
                        (!m_resources[r].IsBuffer() || u.access!=RGAccessMode::Read)) ||
                    (Writes(u) && !IsWriteState(u.state)) ||
                    (u.access==RGAccessMode::Read && IsWriteState(u.state) && u.state!=RHIResourceState::UnorderedAccess))
                {
                    return bad("access/state mismatch: "+m_resources[r].name);
                }
                if (Writes(u))
                {
                    if (m_resources[r].imported && v==0) return fail("cannot overwrite imported v0; call Write/Modify",r,v);
                    const auto& declaration=m_resources[r].versions[v];
                    if (declaration.atomicMin64 != (u.access == RGAccessMode::AtomicMin64) ||
                        declaration.modify != (u.access == RGAccessMode::ReadWrite || u.access == RGAccessMode::AtomicMin64))
                        return fail("Write/Modify/AtomicMin64 access mismatch",r,v);
                    if (declaration.atomicMin64 && (!m_resources[r].IsBuffer() ||
                        u.state != RHIResourceState::UnorderedAccess))
                        return fail("AtomicMin64 requires a buffer UAV",r,v);
                    if (!declaration.atomicMin64 && producerCounts[versionOffsets[r] + v] != 0) return fail("multiple version producers",r,v);
                    ++producerCounts[versionOffsets[r] + v];
                    if (declaration.modify) ++consumerCounts[versionOffsets[r] + declaration.parent];
                }
                else ++consumerCounts[versionOffsets[r] + v];
            }
        }
        for (size_t r = 0; r < m_resources.size(); ++r)
        {
            for (size_t v = 0; v < m_resources[r].versions.size(); ++v)
            {
                if (producerCounts[versionOffsets[r] + v] == 0 && !(m_resources[r].imported && v == 0))
                {
                    return fail("missing version producer", r, static_cast<uint16_t>(v));
                }
            }
        }
        std::vector<size_t> producerOffsets(versionCount + 1, 0), consumerOffsets(versionCount + 1, 0);
        for (size_t version = 0; version < versionCount; ++version)
        {
            producerOffsets[version + 1] = producerOffsets[version] + producerCounts[version];
            consumerOffsets[version + 1] = consumerOffsets[version] + consumerCounts[version];
        }
        std::vector<uint16_t> producerStorage(producerOffsets.back()), consumerStorage(consumerOffsets.back());
        std::ranges::fill(std::span{producerCounts}, 0);
        std::ranges::fill(std::span{consumerCounts}, 0);
        for (size_t pass = 0; pass < count; ++pass)
        {
            for (const auto& usage : std::span{m_passes[pass].usages})
            {
                const auto resource = usage.handle.index;
                const auto version = usage.handle.version;
                const auto slot = versionOffsets[resource] + version;
                if (Writes(usage))
                {
                    producerStorage[producerOffsets[slot] + producerCounts[slot]++] = static_cast<uint16_t>(pass);
                    if (m_resources[resource].versions[version].modify)
                    {
                        const auto parent = versionOffsets[resource] + m_resources[resource].versions[version].parent;
                        consumerStorage[consumerOffsets[parent] + consumerCounts[parent]++] = static_cast<uint16_t>(pass);
                    }
                }
                else
                {
                    consumerStorage[consumerOffsets[slot] + consumerCounts[slot]++] = static_cast<uint16_t>(pass);
                }
            }
        }
        const auto producers = [&](size_t resource, size_t version)
        {
            const auto slot = versionOffsets[resource] + version;
            return std::span<const uint16_t>{producerStorage}.subspan(producerOffsets[slot], producerCounts[slot]);
        };
        const auto consumers = [&](size_t resource, size_t version)
        {
            const auto slot = versionOffsets[resource] + version;
            return std::span<const uint16_t>{consumerStorage}.subspan(consumerOffsets[slot], consumerCounts[slot]);
        };
        // Only data dependencies retain work. WAR/WAW constrain surviving
        // accesses; they must not keep discarded versions or dead readers alive.
        alive.assign(count, false);
        std::vector<uint16_t> roots;
        const auto retain = [&](uint16_t p)
        {
            if (!alive.test(p))
            {
                alive.set(p, true);
                roots.push_back(p);
            }
        };
        for(size_t p=0; p<count; ++p) if(m_passes[p].hasSideEffect) retain(static_cast<uint16_t>(p));
        for(size_t r=0; r<m_resources.size(); ++r)
            if(m_resources[r].imported && m_resources[r].versions.size()>1)
                for (const auto producer : producers(r, m_resources[r].versions.size() - 1)) retain(producer);
        while(!roots.empty())
        {
            const auto p=roots.back(); roots.pop_back();
            for(const auto& u:std::span{m_passes[p].usages})
            {
                if(!Reads(u)) continue;
                const auto r=u.handle.index;
                const auto v=(u.access==RGAccessMode::ReadWrite || u.access==RGAccessMode::AtomicMin64) ? m_resources[r].versions[u.handle.version].parent : u.handle.version;
                for (const auto producer : producers(r, v)) retain(producer);
            }
        }
        for (size_t p = 0; p < count; ++p)
        {
            m_passes[p].culled = !alive.test(p);
        }
        const auto edge = [&](int32_t from, uint16_t to, uint16_t r, uint16_t v, Reason reason) {
            if (from<0 || from==to) return;
            m_versionEdges.push_back({static_cast<uint32_t>(from),to,r,v,reason});
            auto& outgoing=edges[from];
            if (std::ranges::none_of(std::span{outgoing},[&](const Edge& e){return e.to==to;}))
            { outgoing.push_back({to,r,v}); ++incoming[to]; }
        };
        std::vector<uint16_t> previousWriters, current;
        std::vector<std::pair<uint16_t, uint16_t>> pendingReaders;
        for (size_t r=0; r<m_resources.size(); ++r)
        {
            previousWriters.clear();
            pendingReaders.clear();
            uint16_t previousVersion=0;
            for (size_t v=0; v<m_resources[r].versions.size(); ++v)
            {
                current.clear();
                for (const auto producer : producers(r, v))
                {
                    if (alive.test(producer))
                    {
                        current.push_back(producer);
                    }
                }
                if (!current.empty())
                {
                    for (const auto producer : std::span{current})
                    {
                        for (const auto previous : std::span{previousWriters})
                            edge(previous,producer,static_cast<uint16_t>(r),previousVersion,Reason::WAW);
                        for (const auto& [reader,readVersion] : std::span{pendingReaders})
                        {
                            // Atomic writers consume initialization, not sibling results.
                            if (std::ranges::find(std::span{current}, reader)==std::span{current}.end())
                                edge(reader,producer,static_cast<uint16_t>(r),readVersion,Reason::WAR);
                        }
                    }
                    pendingReaders.clear(); previousWriters=current; previousVersion=static_cast<uint16_t>(v);
                }
                for (const auto reader : consumers(r, v))
                {
                    if (!alive.test(reader))
                    {
                        continue;
                    }
                    for (const auto producer : std::span{current})
                        edge(producer,reader,static_cast<uint16_t>(r),static_cast<uint16_t>(v),Reason::RAW);
                    pendingReaders.push_back({reader,static_cast<uint16_t>(v)});
                }
            }
        }
    }
    else
    {
    std::vector<int32_t> writer(m_resources.size(), -1);
    std::vector<std::vector<uint16_t>> readers(m_resources.size());
    ce::dynamic_bitset modifies(m_resources.size(), false);
    for (size_t p = 0; p < count; ++p)
    {
        std::vector<uint16_t> seen;
        for (const auto& u : m_passes[p].usages)
        {
            const auto fail = [&](const std::string& why) {
                outError = "RG1 pass '" + m_passes[p].name + "': " + why; return false;
            };
            if (!u.handle.IsValid() || u.handle.index >= m_resources.size()) return fail("invalid resource handle");
            const auto r = u.handle.index;
            if (std::find(seen.begin(), seen.end(), r) != seen.end()) return fail("duplicate access: " + m_resources[r].name);
            seen.push_back(r);
            if (u.access != RGAccessMode::Read && u.access != RGAccessMode::Write && u.access != RGAccessMode::ReadWrite)
                return fail("explicit Read/Write/ReadWrite required: " + m_resources[r].name);
            if (u.state < RHIResourceState::Common || u.state > RHIResourceState::VertexAndShaderResource ||
                ((u.state == RHIResourceState::IndirectArgument || u.state == RHIResourceState::VertexAndShaderResource) &&
                    (!m_resources[r].IsBuffer() || u.access != RGAccessMode::Read)))
            {
                return fail("invalid resource state: " + m_resources[r].name);
            }
            if ((Writes(u) && !IsWriteState(u.state)) ||
                (u.access == RGAccessMode::Read && IsWriteState(u.state) && u.state != RHIResourceState::UnorderedAccess))
                return fail("access/state mismatch: " + m_resources[r].name);
            if (Writes(u))
            {
                if (writer[r] >= 0) return fail("multiple writers require RG2 versions: " + m_resources[r].name
                    + " (first: " + m_passes[writer[r]].name + ")");
                writer[r] = static_cast<int32_t>(p);
                modifies.set(r, u.access == RGAccessMode::ReadWrite);
                if (modifies.test(r) && !m_resources[r].imported)
                {
                    return fail("ReadWrite needs initialized imported input: " + m_resources[r].name);
                }
            }
            if (Reads(u)) readers[r].push_back(static_cast<uint16_t>(p));
        }
    }
    for (size_t r = 0; r < m_resources.size(); ++r)
        for (const auto consumer : readers[r])
        {
            if (writer[r] < 0)
            {
                if (!m_resources[r].imported)
                { outError = "RG1 missing writer: " + m_resources[r].name + " read by " + m_passes[consumer].name; return false; }
                continue;
            }
            const auto producer = static_cast<uint16_t>(writer[r]);
            if (producer == consumer) continue; // sole imported ReadWrite
            if (modifies.test(r))
            { outError = "RG1 ambiguous ReadWrite chain requires RG2 versions: " + m_resources[r].name; return false; }
            auto& outgoing = edges[producer];
            if (std::none_of(outgoing.begin(), outgoing.end(), [&](const Edge& e) { return e.to == consumer; }))
            { outgoing.push_back({consumer, static_cast<uint16_t>(r)}); ++incoming[consumer]; }
        }
    }
    // CPU callbacks with undeclared external effects are explicit owner-thread
    // fences, unlike hasSideEffect (which only retains a pass during culling).
    for (uint16_t fence = 0; fence < count; ++fence)
    {
        if (!alive.test(fence) || !m_passes[fence].recordingSideEffect)
        {
            continue;
        }
        for (uint16_t pass = 0; pass < count; ++pass)
        {
            if (!alive.test(pass) || pass == fence)
            {
                continue;
            }
            const uint16_t from = pass < fence ? pass : fence;
            const uint16_t to = pass < fence ? fence : pass;
            auto& outgoing = edges[from];
            if (std::none_of(outgoing.begin(), outgoing.end(), [&](const Edge& edge) { return edge.to == to; }))
            {
                outgoing.push_back({to, RGHandle::kInvalid});
                ++incoming[to];
            }
            m_versionEdges.push_back({from, to, RGHandle::kInvalid, 0,
                DiagnosticSnapshot::VersionEdge::Reason::RecordingSideEffect});
        }
    }
    std::priority_queue<uint16_t, std::vector<uint16_t>, std::greater<uint16_t>> ready;
    for (size_t p = 0; p < count; ++p)
    {
        if (alive.test(p) && !incoming[p])
        {
            ready.push(static_cast<uint16_t>(p));
        }
    }
    std::vector<uint16_t> order;
    while (!ready.empty())
    {
        const auto p = ready.top(); ready.pop(); order.push_back(p);
        for (const auto& e : edges[p]) if (--incoming[e.to] == 0) ready.push(e.to);
    }
    if (order.size() != alive.count())
    {
        // Iterative DFS: report a real cycle with resource labels, without
        // recursion depth depending on the number of authored passes.
        std::vector<uint8_t> color(count, 0);
        struct Visit { uint16_t pass; size_t next; };
        std::vector<Visit> stack;
        for (size_t root = 0; root < count; ++root)
        {
            if (!alive.test(root) || color[root])
            {
                continue;
            }
            stack.push_back({static_cast<uint16_t>(root), 0}); color[root] = 1;
            while (!stack.empty())
            {
                auto& top = stack.back();
                if (top.next == edges[top.pass].size()) { color[top.pass] = 2; stack.pop_back(); continue; }
                const auto edge = edges[top.pass][top.next++];
                if (color[edge.to] == 2) continue;
                if (color[edge.to] == 1)
                {
                    outError = m_scheduling==RGSchedulingMode::ExplicitVersioned ? "RG2 cycle: " : "RG1 cycle: ";
                    const auto start = std::find_if(stack.begin(), stack.end(), [&](const Visit& v) { return v.pass == edge.to; });
                    for (auto it = start; it != stack.end(); ++it)
                    {
                        const auto resource = (it + 1 == stack.end()) ? edge.resource : edges[it->pass][it->next - 1].resource;
                        const auto v=(it+1==stack.end()) ? edge.version : edges[it->pass][it->next-1].version;
                        outError += m_passes[it->pass].name + " --" +
                            (resource == RGHandle::kInvalid ? std::string("recording side effect") : m_resources[resource].name)
                            + (m_scheduling==RGSchedulingMode::ExplicitVersioned ? " v"+std::to_string(v) : "") + "--> ";
                    }
                    outError += m_passes[edge.to].name; return false;
                }
                color[edge.to] = 1; stack.push_back({edge.to, 0});
            }
        }
        outError = "RG1 cycle"; return false;
    }
    if (m_orderPolicy == RGOrderPolicy::PreserveDeclarationOrder)
    {
        // Validate the same live version DAG, then preserve authored order only
        // if every dependency can be satisfied without reordering.
        for (size_t from = 0; from < count; ++from)
        {
            for (const auto& edge : edges[from])
            {
                if (alive.test(from) && alive.test(edge.to) && from >= edge.to)
                {
                    outError = "Declaration order violates dependency: " + m_passes[from].name + " -> " +
                               m_passes[edge.to].name + " resource=" +
                               (edge.resource == RGHandle::kInvalid ? std::string("recording side effect") : m_resources[edge.resource].name);
                    return false;
                }
            }
        }
        std::sort(order.begin(), order.end());
    }
    m_executeOrder.swap(order);
    outError.clear();
    return true;
}

bool EnhancedRenderGraph::BuildOrder(std::string& outError)
{
    if (m_orderPolicy != RGOrderPolicy::DependencyOrder && m_orderPolicy != RGOrderPolicy::PreserveDeclarationOrder)
    {
        m_executeOrder.clear();
        outError = "Invalid RenderGraph order policy";
        return false;
    }
    if (m_scheduling != RGSchedulingMode::DeclarationOrder && m_scheduling != RGSchedulingMode::ExplicitSingleWriter &&
        m_scheduling != RGSchedulingMode::ExplicitVersioned)
    {
        m_executeOrder.clear();
        outError = "Invalid RenderGraph scheduling mode";
        return false;
    }
    if (m_scheduling != RGSchedulingMode::DeclarationOrder)
    {
        return BuildExplicitOrder(outError);
    }
    ce::profile_scope profile{ce::marker<"RenderGraphOrder">()};
    // LegacyState compatibility branch only: retain authored order and reject
    // uninitialized transient reads. Explicit modes above compile a dependency
    // DAG; RGOrderPolicy chooses dependency order or validates authored order.
    const size_t passCount = m_passes.size();
    m_executeOrder.clear();
    if (0 == passCount) return true;

    ce::dynamic_bitset written(m_resources.size(), false);

    for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        const Pass& pass = m_passes[passIndex];

        for (const auto& usage : std::span{pass.usages})
        {
            if (usage.access != RGAccessMode::LegacyState)
            { outError = "Explicit access requires ExplicitSingleWriter scheduling: " + pass.name; return false; }
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;
            const size_t resourceIndex = usage.handle.index;
            const Resource& resource = m_resources[resourceIndex];

            if (IsWriteState(usage.state))
            {
                written.set(resourceIndex, true);
                continue;
            }

            if (!resource.imported && !written.test(resourceIndex))
            {
                outError = "선언 순서가 데이터 흐름과 어긋난다: 패스 '" + pass.name
                    + "'가 아직 아무도 쓰지 않은 '" + resource.name + "'을 읽는다";
                return false;
            }
        }
    }

    m_executeOrder.reserve(passCount);
    for (size_t i = 0; i < passCount; ++i)
    {
        m_executeOrder.push_back(static_cast<uint16_t>(i));
    }

    return true;
}

void EnhancedRenderGraph::CullPasses()
{
    ce::profile_scope profile{ce::marker<"RenderGraphCull">()};
    const size_t passCount = m_passes.size();
    if(m_scheduling==RGSchedulingMode::ExplicitVersioned)
    {
        m_stats.passesCulled=static_cast<uint32_t>(passCount-m_executeOrder.size());
        return; // Version-producer reachability preceded the stable sort.
    }
    for (auto& pass : m_passes) pass.culled = true;

    // Index writers once rather than scanning every pass and usage for each read.
    // Keep all writers, including later declarations, to preserve reachability semantics.
    std::vector<std::vector<size_t>> writers(m_resources.size());
    for (size_t i = 0; i < passCount; ++i)
        for (const auto& usage : m_passes[i].usages)
            if (usage.handle.IsValid() && usage.handle.index < writers.size() && Writes(usage))
                writers[usage.handle.index].push_back(i);

    // 뿌리: 부작용이 있는 패스(결과가 그래프 밖으로 나간다)와 외부 리소스에 쓰는 패스.
    // 후자를 넣는 이유는, 임포트한 리소스는 그래프가 수명을 모르므로 그 쓰기가
    // 밖에서 쓰일 수 있다고 봐야 하기 때문이다.
    std::vector<size_t> stack;
    for (size_t i = 0; i < passCount; ++i)
    {
        bool isRoot = m_passes[i].hasSideEffect;
        if (!isRoot)
        {
            for (const auto& usage : m_passes[i].usages)
            {
                if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;
                if (m_resources[usage.handle.index].imported && Writes(usage))
                {
                    isRoot = true;
                    break;
                }
            }
        }

        if (isRoot)
        {
            m_passes[i].culled = false;
            stack.push_back(i);
        }
    }

    // 살아남은 패스가 읽는 리소스를 쓴 패스도 살린다(역방향 도달).
    while (!stack.empty())
    {
        const size_t current = stack.back();
        stack.pop_back();

        for (const auto& usage : m_passes[current].usages)
        {
            if (!Reads(usage)) continue;
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;

            for (const size_t producer : writers[usage.handle.index])
            {
                if (!m_passes[producer].culled) continue;
                m_passes[producer].culled = false;
                stack.push_back(producer);
            }
        }
    }

    // 실행 순서에서 걷어낸 패스를 뺀다.
    std::vector<uint16_t> survivors;
    survivors.reserve(m_executeOrder.size());
    for (uint16_t index : std::span{m_executeOrder})
    {
        if (!m_passes[index].culled) survivors.push_back(index);
    }

    m_stats.passesCulled = static_cast<uint32_t>(m_executeOrder.size() - survivors.size());
    m_executeOrder.swap(survivors);
}

RHITransientResourceDesc EnhancedRenderGraph::TransientDescription(const Resource& resource) const
{
    RHITransientResourceDesc result{};
    result.buffer = resource.IsBuffer();
    result.bufferDesc.bytes = resource.bufferDesc.bytes;
    result.bufferDesc.allowUnorderedAccess = resource.bufferDesc.allowUnorderedAccess;
    result.bufferDesc.allowIndirectArguments = resource.bufferDesc.allowIndirectArguments;
    result.textureDesc.width = resource.desc.width;
    result.textureDesc.height = resource.desc.height;
    result.textureDesc.depthOrArraySize = (std::max)(1u, resource.desc.arraySize);
    result.textureDesc.mipLevels = 1;
    result.textureDesc.format = resource.desc.format;
    result.textureDesc.allowRenderTarget = resource.desc.allowRenderTarget;
    result.textureDesc.allowDepthStencil = resource.desc.allowDepthStencil;
    result.textureDesc.allowUnorderedAccess = resource.desc.allowUnorderedAccess;
    std::copy_n(resource.desc.clearColor, 4, result.textureDesc.clearColor);
    if (m_poison)
    {
        const float color[]{1.0f, 0.0f, 1.0f, 1.0f};
        std::copy_n(color, 4, result.textureDesc.clearColor);
        result.textureDesc.clearDepth = 0.125f;
    }
    return result;
}

bool EnhancedRenderGraph::CreateAliasedTransients(std::string& outError)
{
    const auto& executeOrder = GetExecuteOrder();
    if (!m_aliasing || !m_deviceServices->SupportsTransientAliasing())
    {
        return true; // Unsupported backends retain the committed resource path.
    }
    if (m_scheduling != RGSchedulingMode::ExplicitVersioned)
    {
        outError = "Transient aliasing requires explicit versioned write declarations";
        return false;
    }
    if (m_deviceServices->GetCurrentUploadRecordingId() == 0)
    {
        outError = "Transient aliasing compilation requires an active owner recording";
        return false;
    }
    if (m_transientPool)
    {
        if (m_transientPool->aliasOwner && m_transientPool->aliasOwner != m_deviceServices)
        {
            outError = "Transient alias cache belongs to another device service";
            return false;
        }
        m_transientPool->aliasOwner = m_deviceServices;
    }
    struct Group
    {
        RHITransientAllocationInfo allocation;
        uint32_t lastUse;
        std::vector<uint16_t> members;
    };
    std::vector<Group> groups;
    std::vector<uint16_t> candidates;
    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& resource = m_resources[index];
        if (!resource.imported && resource.used)
        {
            candidates.push_back(index);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [this](auto left, auto right)
    {
        return m_resources[left].firstUse < m_resources[right].firstUse;
    });
    for (auto index : candidates)
    {
        auto& resource = m_resources[index];
        RHITransientAllocationInfo allocation{};
        const auto description = TransientDescription(resource);
        const auto key = TransientAllocationKey(description);
        const RHITransientAllocationInfo* cached = nullptr;
        if (m_transientPool)
        {
            const auto found = m_transientPool->allocationCache.find(key);
            if (found != m_transientPool->allocationCache.end())
            {
                cached = &found->second;
            }
        }
        if (cached)
        {
            allocation = *cached;
        }
        else
        {
            ++m_stats.transientAllocationQueries;
            if (!m_deviceServices->DescribeTransientAllocation(description, allocation, outError))
            {
                return false;
            }
            if (m_transientPool)
            {
                if (m_transientPool->allocationCache.size() >= 256)
                {
                    m_transientPool->allocationCache.clear();
                }
                m_transientPool->allocationCache.emplace(key, allocation);
            }
        }
        resource.allocationBytes = allocation.bytes;
        m_stats.transientUnaliasedBytes += allocation.bytes;
        m_stats.transientCommittedBytes += allocation.bytes;
        const auto& firstPass = m_passes[executeOrder[resource.firstUse]];
        auto firstUsage = std::ranges::find_if(firstPass.usages, [index](const auto& usage)
        {
            return usage.handle.index == index;
        });
        // Repeated phases have their own state plan. A first read cannot
        // initialize aliased storage. RT/DS compression requires a full clear.
        if (!m_aliasing || resource.IsValid() || firstPass.repeated || firstUsage == firstPass.usages.end() ||
            !Writes(*firstUsage) || Reads(*firstUsage) ||
            (!resource.IsBuffer() && resource.desc.allowRenderTarget && firstUsage->state != RHIResourceState::RenderTarget) ||
            (!resource.IsBuffer() && resource.desc.allowDepthStencil && firstUsage->state != RHIResourceState::DepthWrite))
        {
            continue;
        }
        const auto allocationLastUse = m_extendLifetimes ?
            static_cast<uint32_t>(executeOrder.size() - 1) : resource.lastUse;
        auto group = std::ranges::find_if(groups, [&](const Group& existing)
        {
            return existing.allocation.heapClass == allocation.heapClass && existing.lastUse < resource.firstUse;
        });
        if (group == groups.end())
        {
            groups.push_back({allocation, allocationLastUse, {index}});
        }
        else
        {
            group->allocation.bytes = (std::max)(group->allocation.bytes, allocation.bytes);
            group->allocation.alignment = (std::max)(group->allocation.alignment, allocation.alignment);
            group->lastUse = allocationLastUse;
            group->members.push_back(index);
        }
    }
    for (const auto& group : groups)
    {
        if (group.members.size() < 2)
        {
            continue;
        }
        std::vector<RHITransientResourceDesc> descriptions;
        descriptions.reserve(group.members.size());
        for (auto index : group.members)
        {
            const auto& resource = m_resources[index];
            const auto& usages = m_passes[executeOrder[resource.firstUse]].usages;
            const auto first = std::ranges::find_if(usages, [index](const auto& usage)
            {
                return usage.handle.index == index;
            });
            if (m_poison && ((resource.IsBuffer() &&
                (!resource.bufferDesc.allowUnorderedAccess || resource.bufferDesc.bytes % 4 != 0 ||
                    resource.bufferDesc.bytes / 4 > UINT32_MAX || first == usages.end() ||
                    first->state != RHIResourceState::UnorderedAccess)) ||
                (!resource.IsBuffer() && !resource.desc.allowRenderTarget && !resource.desc.allowDepthStencil)))
            {
                outError = "Placed poison supports RT/DS targets and float-view UAV buffers only";
                return false;
            }
            descriptions.push_back(TransientDescription(m_resources[index]));
        }
        std::shared_ptr<RGTransientPool::AliasedGroup> lease;
        if (m_transientPool)
        {
            auto& free = m_transientPool->freeAliasedGroups;
            const auto match = std::ranges::find_if(free, [&](const auto& candidate)
            {
                return candidate->owner == m_deviceServices && candidate->allocation.bytes == group.allocation.bytes &&
                    candidate->allocation.alignment == group.allocation.alignment &&
                    candidate->allocation.heapClass == group.allocation.heapClass &&
                    candidate->descriptions.size() == descriptions.size() &&
                    std::equal(descriptions.begin(), descriptions.end(), candidate->descriptions.begin(), SamePlacedDescription);
            });
            if (match != free.end())
            {
                lease = std::move(*match);
                lease->SetCached(false);
                m_transientPool->freeAliasBytes -= lease->allocation.bytes;
                free.erase(match);
                ++m_stats.aliasHeapReuses;
                m_stats.aliasResourceReuses += static_cast<uint32_t>(lease->entries.size());
            }
        }
        if (!lease)
        {
            lease = std::make_shared<RGTransientPool::AliasedGroup>();
            lease->owner = m_deviceServices;
            lease->allocation = group.allocation;
            lease->descriptions = std::move(descriptions);
            lease->entries.reserve(group.members.size());
            if (!m_deviceServices->CreateTransientHeap(group.allocation, lease->heap, outError))
            {
                return false;
            }
            ++m_stats.aliasHeapCreates;
            auto& accounting = m_transientPool ? m_transientPool->aliasHeapAccounting : m_aliasHeapAccounting;
            if (!accounting)
            {
                accounting = std::make_shared<RGTransientPool::AliasHeapAccounting>();
            }
            lease->TrackHeap(accounting);
            for (size_t member = 0; member < group.members.size(); ++member)
            {
                auto description = lease->descriptions[member];
                const auto& resource = m_resources[group.members[member]];
                const std::wstring name(resource.name.begin(), resource.name.end());
                description.bufferDesc.debugName = name.c_str();
                description.textureDesc.debugName = name.c_str();
                RGTransientPool::AliasedEntry entry;
                if (!m_deviceServices->CreatePlacedTransient(description, *lease->heap, entry.texture, entry.buffer, outError))
                {
                    return false;
                }
                lease->entries.push_back(entry);
                ++m_stats.transientCreated;
            }
        }
        const auto groupIndex = static_cast<uint32_t>(m_transientHeaps.size());
        m_transientHeaps.push_back(std::move(lease));
        for (size_t member = 0; member < group.members.size(); ++member)
        {
            auto& resource = m_resources[group.members[member]];
            const auto& entry = m_transientHeaps.back()->entries[member];
            resource.handle = entry.texture;
            resource.buffer = entry.buffer;
            resource.initialState = resource.state = entry.state;
            resource.aliasGroup = groupIndex;
            resource.aliasMember = static_cast<uint32_t>(member);
            m_stats.transientCommittedBytes -= resource.allocationBytes;
        }
        m_stats.transientCommittedBytes += group.allocation.bytes;
        m_stats.aliasReuseCount += static_cast<uint32_t>(group.members.size() - 1);
    }
    return true;
}

void EnhancedRenderGraph::UpdateResourceUses()
{
    if (m_activeStructuralSchedule && !m_executeOrderOverridden)
    {
        // The exact authored contracts and execution order were matched before
        // binding this template. Only index lifetimes survive; storage ownership
        // and native states are acquired again by CreateTransients/PlanBarriers.
        for (size_t index = 0; index < m_resources.size(); ++index)
        {
            auto& resource = m_resources[index];
            const auto& declaration = m_activeStructuralSchedule->resources[index];
            resource.used = declaration.used;
            resource.firstUse = declaration.firstUse;
            resource.lastUse = declaration.lastUse;
        }
        return;
    }
    for (auto& resource : std::span{m_resources})
    {
        resource.used = false;
        resource.firstUse = 0xFFFFFFFF;
        resource.lastUse = 0;
    }

    const auto& executeOrder = GetExecuteOrder();
    for (uint32_t order = 0; order < executeOrder.size(); ++order)
    {
        for (const auto& usage : m_passes[executeOrder[order]].usages)
        {
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;
            Resource& resource = m_resources[usage.handle.index];
            resource.used = true;
            resource.firstUse = (std::min)(resource.firstUse, order);
            resource.lastUse = (std::max)(resource.lastUse, order);
        }
    }
}

bool EnhancedRenderGraph::CreateTransients(std::string& outError)
{
    ce::profile_scope profile{ce::marker<"RenderGraphTransients">()};
    TransientPreparationTimer timer{m_stats.transientPrepareCpuMs};
    // 살아남은 패스가 실제로 쓰는 리소스만 만든다. 컬링된 패스만 쓰던 것을
    // 만드는 것은 낭비이고, 그 낭비는 프레임마다 반복된다.
    UpdateResourceUses();

    if (!CreateAliasedTransients(outError))
    {
        return false;
    }
    for (auto& resource : std::span{m_resources})
    {
        if (resource.imported || !resource.used || resource.IsValid())
        {
            continue;
        }
        if (resource.IsBuffer())
        {
            auto create = TransientDescription(resource);
            const std::wstring name(resource.name.begin(), resource.name.end());
            create.bufferDesc.debugName = name.c_str();
            if (!m_deviceServices->CreateBuffer(create.bufferDesc, resource.buffer, outError))
            {
                return false;
            }
            ++m_stats.transientCreated;
            continue;
        }

        // 풀 키 — 재사용 호환성을 정하는 것 전부를 섞는다(크기·포맷·플래그·
        // 클리어 값). 이름은 넣지 않는다: 같은 모양이면 다른 패스끼리도
        // 재사용해야 풀이 작게 유지된다.
        {
            uint64_t key = 1469598103934665603ull;
            const auto mix = [&key](uint64_t value)
            { key ^= value; key *= 1099511628211ull; };
            mix(resource.desc.width);
            mix(resource.desc.height);
            mix((std::max)(1u, resource.desc.arraySize));
            mix(static_cast<uint64_t>(resource.desc.format));
            mix((resource.desc.allowRenderTarget ? 1u : 0u)
                | (resource.desc.allowDepthStencil ? 2u : 0u)
                | (resource.desc.allowUnorderedAccess ? 4u : 0u));
            for (int i = 0; i < 4; ++i)
            {
                uint32_t bits = 0;
                memcpy(&bits, &resource.desc.clearColor[i], sizeof(bits));
                mix(bits);
            }
            resource.poolKey = key;
        }

        if (nullptr != m_transientPool)
        {
            auto found = m_transientPool->freeList.find(resource.poolKey);
            if (found != m_transientPool->freeList.end() && !found->second.empty())
            {
                const RGTransientPool::Entry entry = found->second.back();
                found->second.pop_back();
                resource.handle = entry.handle;
                // 지난 프레임의 마지막 상태에서 출발한다 — COMMON으로 두면
                // 배리어 계획이 실제 상태와 어긋나 검증 레이어가 운다.
                resource.state = entry.state;
                resource.initialState = entry.state;
                continue;
            }
        }

        // ★ 서비스가 만든다 (G-1). 예전에는 여기서 CreateCommittedResource 를
        //   직접 불렀고, 그것이 `ID3D12Device` 가 그래프 헤더에 남아 있던
        //   이유 둘 중 하나였다(다른 하나는 `Compile` 의 인자).
        //
        //   막고 있던 것은 desc 어휘였다 — `RHITextureDesc` 에 깊이 타깃과
        //   클리어 힌트가 없었다. 그 둘을 더하자 손코드 전체가 이 호출 하나가
        //   된다. 클리어 힌트의 분기(깊이면 1.0, 아니면 색 넷)도 서비스가
        //   들고, 3-3 이 실측한 "힌트 없음도 경고"라는 규칙이 한 곳에 남는다.
        RHITextureDesc create{};
        create.width = resource.desc.width;
        create.height = resource.desc.height;
        create.depthOrArraySize = (std::max)(1u, resource.desc.arraySize);
        create.mipLevels = 1;
        create.format = resource.desc.format;
        create.allowRenderTarget = resource.desc.allowRenderTarget;
        create.allowDepthStencil = resource.desc.allowDepthStencil;
        create.allowUnorderedAccess = resource.desc.allowUnorderedAccess;
        create.initialState = RHIResourceState::Common;
        for (int i = 0; i < 4; ++i) create.clearColor[i] = resource.desc.clearColor[i];

        const std::wstring wideName(resource.name.begin(), resource.name.end());
        if (!resource.name.empty()) create.debugName = wideName.c_str();

        std::string createError;
        if (!m_deviceServices->CreateTexture(create, resource.handle, createError))
        {
            outError = "transient 리소스 생성 실패(" + resource.name + ") " + createError;
            return false;
        }
        resource.state = RHIResourceState::Common;
        ++m_stats.transientCreated;
    }

    return true;
}

EnhancedRenderGraph::EnhancedRenderGraph(IRenderDeviceServices& services, RGSchedulingMode scheduling, RGOrderPolicy orderPolicy)
    : m_deviceServices(&services), m_scheduling(scheduling), m_orderPolicy(orderPolicy)
{
    // A live scene declares dozens of passes/resources per recording. Pass owns
    // several vectors; growing the outer array repeatedly also rebuilds their
    // checked-iterator proxies in Debug. These are initial capacities, not limits.
    // The METEOR scene graph alone records ~190 passes; 128 forced one full
    // relocation of every Pass per frame.
    m_resourceStorage.reserve(512);
    m_passStorage.reserve(512);
    m_executeOrder.reserve(512);
    m_resourceEpoch = NextResourceEpoch();
    services.RegisterUploadTransactionListener(this);
}

EnhancedRenderGraph::~EnhancedRenderGraph()
{
    m_deviceServices->UnregisterUploadTransactionListener(this);
    ReleaseResources();
}

void EnhancedRenderGraph::OnUploadAccepted(uint64_t recording, RHICompletionPoint)
{
    if (recording != m_recordedRecording || m_statesCommitted)
    {
        return;
    }
    for (auto& resource : std::span{m_resources})
    {
        if (resource.writeback)
        {
            *resource.writeback = resource.state;
        }
    }
    m_statesCommitted = true;
}

// transient를 풀에 반납하고, 그래프가 표에 넣은 것을 놓는다. 호출부가 이
// 시점에 GPU 완료를 보장하는 것이 계약이다(그래프 수명 규칙) — 그래서
// 여기서 펜스를 보지 않는다.
//
// ★ 소멸자와 Reset이 같은 것을 부른다. 예전에는 반납이 소멸자에만 있어서
//   Reset이 m_resources를 그냥 비웠다 — 지금은 호출자가 0이라 안 드러났지만,
//   V2-c2로 표 등록이 걸리면서 Reset 한 번에 표가 새는 API가 된다.
//   호출자가 없다는 이유로 반쪽만 고치면, 나중에 부른 사람이 그 사실을
//   알 길이 없다.
void EnhancedRenderGraph::ReleaseResources()
{
    for (auto& resource : std::span{m_resources})
    {
        if (!resource.IsValid()) continue;

        if (resource.aliasGroup != UINT32_MAX)
        {
            auto& entry = m_transientHeaps[resource.aliasGroup]->entries[resource.aliasMember];
            entry.state = m_statesCommitted ? resource.state : resource.initialState;
            resource.handle = {};
            resource.buffer = {};
            continue;
        }

        // ImportBuffer는 남의 지속 버퍼를 추적만 한다. 그래프가 등록하거나
        // 만든 것이 아니므로 반납할 일이 없다.
        if (resource.IsBuffer())
        {
            if (!resource.imported)
            {
                m_deviceServices->ReleaseBuffer(resource.buffer);
                resource.buffer = {};
            }
            continue;
        }

        // 임포트는 등록된 핸들을 빌려 추적만 한다. 등록 수명은 import 전에
        // backend에 올린 소유자가 GPU 완료 뒤 해제한다(R6-a).
        if (resource.imported) continue;

        // ── transient ──
        //
        // ★ 여기에 결함이 있었다(V2-c2, 2026-08-10). 반납 조건이 별도 소유
        //   플래그였는데 풀에서 '빌려 온' 경로가 그 플래그를
        //   세우지 않아, 빌린 것이 반납되지 않았다. 결과는 한 프레임 걸러
        //   전 transient를 CreateCommittedResource로 다시 만드는 것 —
        //   PHASE 3-9가 풀을 넣어 없앤 바로 그 비용(프레임당 수십 ms)이
        //   되살아났고, 에디터 카메라가 뚝뚝 끊겼다.
        //
        //   고치면서 조건 자체를 없앴다. transient는 '그래프가 만들었든
        //   빌렸든 그래프가 끝나면 내놓는다'가 예외 없는 규칙이므로, 그것을
        //   기억하는 플래그가 있으면 안 된다. 세우는 자리가 둘이면 언젠가
        //   한쪽을 빠뜨린다 — 실제로 빠뜨렸다.
        if (nullptr != m_transientPool && resource.aliasGroup == UINT32_MAX)
        {
            m_transientPool->freeList[resource.poolKey].push_back(
                { resource.handle, m_statesCommitted ? resource.state : resource.initialState });
        }
        else if (nullptr != m_deviceServices)
        {
            m_deviceServices->ReleaseTexture(resource.handle);
        }
        resource.handle = {};   // 두 번 내놓지 않는다
    }
    if (m_transientPool)
    {
        for (auto& group : m_transientHeaps)
        {
            if (m_transientPool->aliasOwner == m_deviceServices &&
                m_transientPool->freeAliasedGroups.size() < m_transientPool->maxFreeAliasGroups &&
                group->allocation.bytes <= m_transientPool->maxFreeAliasBytes &&
                m_transientPool->freeAliasBytes <= m_transientPool->maxFreeAliasBytes - group->allocation.bytes)
            {
                m_transientPool->freeAliasedGroups.push_back(std::move(group));
                m_transientPool->freeAliasBytes += m_transientPool->freeAliasedGroups.back()->allocation.bytes;
                m_transientPool->freeAliasedGroups.back()->SetCached(true);
            }
        }
    }
    m_transientHeaps.clear();
}

bool EnhancedRenderGraph::ValidateRepeatedPasses(std::string& outError) const
{
    ce::profile_scope profile{ce::marker<"RenderGraphValidateRepeated">()};
    std::vector<size_t> outerIndices(m_resources.size(), SIZE_MAX);
    std::vector<size_t> seenPhase(m_resources.size(), SIZE_MAX);
    for (const auto& pass : m_passes)
    {
        if (!pass.repeated)
        {
            continue;
        }
        const auto fail = [&](const std::string& reason)
        {
            outError = "RenderGraph repeated pass '" + pass.name + "': " + reason;
            return false;
        };
        if (m_scheduling != RGSchedulingMode::ExplicitVersioned || !pass.repeatedExecute ||
            pass.phases.empty() || pass.phases.size() > kMaxRepeatedPhases ||
            pass.repeatCount == 0 || pass.repeatCount > kMaxPassRepetitions)
        {
            return fail("requires versioned access, a callback, and bounded nonempty phases/repetitions");
        }
        std::ranges::fill(std::span{outerIndices}, SIZE_MAX);
        std::ranges::fill(std::span{seenPhase}, SIZE_MAX);
        // Word-backed bitsets avoid container-bound iterator proxy adoption.
        ce::dynamic_bitset initialized(pass.usages.size()), used(pass.usages.size()), written(pass.usages.size());
        for (size_t index = 0; index < pass.usages.size(); ++index)
        {
            const auto resourceIndex = pass.usages[index].handle.index;
            if (resourceIndex >= outerIndices.size())
            {
                return fail("invalid external resource");
            }
            // Preserve first-match semantics; the DAG validator rejects any
            // duplicate outer declarations, including on a cache miss.
            if (outerIndices[resourceIndex] == SIZE_MAX)
            {
                outerIndices[resourceIndex] = index;
            }
            initialized.set(index, pass.usages[index].access != RGAccessMode::Write);
        }
        for (size_t phaseIndex = 0; phaseIndex < pass.phases.size(); ++phaseIndex)
        {
            const auto& phase = pass.phases[phaseIndex];
            if (phase.name.empty() || phase.usages.empty())
            {
                return fail("phase names and resource contracts must be nonempty");
            }
            for (const auto& usage : std::span{phase.usages})
            {
                const auto resourceIndex = usage.handle.index;
                if (resourceIndex >= outerIndices.size() || outerIndices[resourceIndex] == SIZE_MAX)
                {
                    return fail("undeclared resource in phase '" + phase.name + "'");
                }
                const auto index = outerIndices[resourceIndex];
                const auto* found = &pass.usages[index];
                if (!ValidVersionHandle(usage.handle) ||
                    found->handle.version != usage.handle.version || found->handle.kind != usage.handle.kind ||
                    found->handle.epoch != usage.handle.epoch)
                {
                    return fail("undeclared or different resource version in phase '" + phase.name + "'");
                }
                if (seenPhase[resourceIndex] == phaseIndex)
                {
                    return fail("duplicate resource in phase '" + phase.name + "'");
                }
                seenPhase[resourceIndex] = phaseIndex;
                if (usage.access == RGAccessMode::LegacyState ||
                    usage.access < RGAccessMode::Read || usage.access > RGAccessMode::ReadWrite ||
                    usage.state < RHIResourceState::Common || usage.state > RHIResourceState::VertexAndShaderResource ||
                    (Writes(usage) && !IsWriteState(usage.state)) ||
                    (Reads(usage) && usage.access == RGAccessMode::Read && IsWriteState(usage.state) &&
                        usage.state != RHIResourceState::UnorderedAccess) ||
                    (!m_resources[usage.handle.index].IsBuffer() &&
                        (usage.state == RHIResourceState::IndexBuffer || usage.state == RHIResourceState::IndirectArgument ||
                            usage.state == RHIResourceState::VertexAndShaderResource)) ||
                    (m_resources[usage.handle.index].IsBuffer() &&
                        (usage.state == RHIResourceState::RenderTarget || usage.state == RHIResourceState::DepthWrite ||
                            usage.state == RHIResourceState::DepthRead || usage.state == RHIResourceState::DepthReadShaderResource)) ||
                    ((usage.state == RHIResourceState::IndirectArgument || usage.state == RHIResourceState::VertexAndShaderResource)
                        && usage.access != RGAccessMode::Read))
                {
                    return fail("access/state mismatch in phase '" + phase.name + "'");
                }
                if (!used.test(index) && found->state != usage.state)
                {
                    return fail("external state must match first phase use: " + m_resources[usage.handle.index].name);
                }
                if ((found->access == RGAccessMode::Read && Writes(usage)) ||
                    (Reads(usage) && !initialized.test(index)))
                {
                    return fail("phase writes an external Read or reads an uninitialized output: " +
                        m_resources[usage.handle.index].name);
                }
                used.set(index, true);
                if (Writes(usage))
                {
                    initialized.set(index, true);
                    written.set(index, true);
                }
            }
        }
        for (size_t index = 0; index < pass.usages.size(); ++index)
        {
            if (!used.test(index) || (Writes(pass.usages[index]) && !written.test(index)))
            {
                return fail("external resource contract has no corresponding phase access/write");
            }
        }
    }
    // 진단 카운터의 표현 한계도 선언 단계에서 닫는다. 실제 배리어 수는
    // 접근 수 이하이므로 이 상한 안에서는 반복 통계가 잘리거나 감기지 않는다.
    uint64_t maximumBarriers = m_finalStateRequirements.size();
    for (const auto& pass : m_passes)
    {
        if (pass.culled)
        {
            continue;
        }
        if (pass.repeated)
        {
            for (const auto& phase : std::span{pass.phases})
            {
                maximumBarriers += uint64_t(phase.usages.size()) * pass.repeatCount;
            }
        }
        else
        {
            maximumBarriers += pass.usages.size();
        }
        if (maximumBarriers > UINT32_MAX)
        {
            outError = "RenderGraph repeated resource accesses exceed barrier counter capacity";
            return false;
        }
    }
    return true;
}

bool EnhancedRenderGraph::ValidateFinalStates(std::string& outError) const
{
    for (size_t index = 0; index < m_finalStateRequirements.size(); ++index)
    {
        const auto& requirement = m_finalStateRequirements[index];
        const auto handle = requirement.handle;
        if (!handle.IsValid() || handle.index >= m_resources.size() ||
            (m_scheduling == RGSchedulingMode::ExplicitVersioned && !ValidVersionHandle(handle)))
        {
            outError = "RenderGraph final state requires a valid imported handle";
            return false;
        }
        const auto& resource = m_resources[handle.index];
        // Pooled imported buffers return to Common. Keep other buffer final
        // states outside this contract until their usage/capabilities are validated.
        const bool invalidKindState = resource.IsBuffer()
            ? requirement.state != RHIResourceState::Common
            : (requirement.state == RHIResourceState::IndexBuffer || requirement.state == RHIResourceState::IndirectArgument ||
               requirement.state == RHIResourceState::VertexAndShaderResource);
        if (!resource.imported || invalidKindState || requirement.state < RHIResourceState::Common ||
            requirement.state > RHIResourceState::VertexAndShaderResource)
        {
            outError = "Invalid imported final state: " + resource.name;
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous)
        {
            const auto& other = m_finalStateRequirements[previous];
            if (other.handle.index == handle.index && other.state != requirement.state)
            {
                outError = "Conflicting imported final states: " + resource.name;
                return false;
            }
        }
    }
    return true;
}

void EnhancedRenderGraph::PlanRepeatedBarriers(Pass& pass, ce::dynamic_bitset& previousWrite)
{
    // 첫 반복만 외부 상태에서 출발한다. 이후 반복은 고정된 마지막 단계에서
    // 돌아오므로 배리어 표 두 벌이면 타일 수와 무관하게 같은 계약을 표현한다.
    const uint32_t templates = pass.repeatCount > 1 ? 2u : 1u;
    for (uint32_t iteration = 0; iteration < templates; ++iteration)
    {
        auto& plans = iteration == 0 ? pass.firstPhaseBarriers : pass.repeatPhaseBarriers;
        plans.resize(pass.phases.size());
        for (size_t phaseIndex = 0; phaseIndex < pass.phases.size(); ++phaseIndex)
        {
            auto& plan = plans[phaseIndex];
            for (const auto& usage : std::span{pass.phases[phaseIndex].usages})
            {
                auto& resource = m_resources[usage.handle.index];
                if (resource.state == usage.state)
                {
                    if (usage.state == RHIResourceState::UnorderedAccess &&
                        (previousWrite.test(usage.handle.index) || Writes(usage)))
                    {
                        if (resource.IsBuffer())
                        {
                            plan.uavBufferBarriers.push_back(resource.buffer);
                        }
                        else
                        {
                            plan.uavBarriers.push_back(resource.handle);
                        }
                    }
                }
                else if (resource.IsBuffer())
                {
                    plan.bufferTransitions.push_back({resource.buffer, resource.state, usage.state});
                }
                else
                {
                    plan.transitions.push_back({resource.handle, resource.state, usage.state});
                }
                resource.state = usage.state;
                previousWrite.set(usage.handle.index, Writes(usage));
            }
            const auto count = plan.transitions.size() + plan.bufferTransitions.size() +
                plan.uavBarriers.size() + plan.uavBufferBarriers.size();
            const uint32_t repetitions = iteration == 0 ? 1 : pass.repeatCount - 1;
            m_stats.barriersEmitted += static_cast<uint32_t>(uint64_t(count) * repetitions);
            if (count != 0)
            {
                m_stats.barrierBatches += repetitions;
            }
        }
    }
}

void EnhancedRenderGraph::PlanBarriers(const ce::dynamic_bitset* batchEnds,
    PhaseBarrierPlan* prologue, PhaseBarrierPlan* epilogue,
    const ce::dynamic_bitset* retainedGraphicsTextures)
{
    const auto& executeOrder = GetExecuteOrder();
    ce::profile_scope profile{ce::marker<"RenderGraphPlanBarriers">()};
    const bool queuePlan = batchEnds != nullptr;
    m_stats.barriersEmitted = 0;
    m_stats.barrierBatches = 0;
    ce::dynamic_bitset previousWrite(m_resources.size(),true);
    std::vector<uint16_t> previousAtomicVersion(m_resources.size(), RGHandle::kInvalid);
    std::vector<RHIResourceState> finalStates;
    const auto retainsState = [&](size_t index)
    {
        return retainedGraphicsTextures && retainedGraphicsTextures->test(index);
    };
    const auto appendTransition = [](PhaseBarrierPlan& plan, const Resource& resource,
        RHIResourceState before, RHIResourceState after)
    {
        if (before == after)
        {
            return;
        }
        if (resource.IsBuffer())
        {
            plan.bufferTransitions.push_back({resource.buffer, before, after});
        }
        else
        {
            plan.transitions.push_back({resource.handle, before, after});
        }
    };
    for (auto& resource : std::span{m_resources})
    {
        if (queuePlan)
        {
            finalStates.push_back(resource.state);
            if (resource.used)
            {
                if (!retainsState(finalStates.size() - 1))
                {
                    appendTransition(*prologue, resource, resource.initialState, RHIResourceState::Common);
                    resource.state = RHIResourceState::Common;
                }
                else
                {
                    resource.state = resource.initialState;
                }
            }
        }
        else
        {
            resource.initialState = resource.state;
        }
    }
    for (auto& pass : std::span{m_passes})
    {
        ClearNonempty(pass.transitions);
        ClearNonempty(pass.bufferTransitions);
        ClearNonempty(pass.uavBarriers);
        ClearNonempty(pass.uavBufferBarriers);
        ClearNonempty(pass.finalTransitions);
        ClearNonempty(pass.finalBufferTransitions);
        ClearNonempty(pass.firstPhaseBarriers);
        ClearNonempty(pass.repeatPhaseBarriers);
        ClearNonempty(pass.aliasTextures);
        ClearNonempty(pass.aliasBuffers);
        ClearNonempty(pass.poisonBuffers);
        ClearNonempty(pass.aliasClears);
    }

    for (uint16_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& resource = m_resources[index];
        if (resource.aliasGroup == UINT32_MAX)
        {
            continue;
        }
        auto& pass = m_passes[executeOrder[resource.firstUse]];
        if (resource.IsBuffer())
        {
            pass.aliasBuffers.push_back(resource.buffer);
            if (m_poison)
            {
                pass.poisonBuffers.push_back(index);
                ++m_stats.poisonInitializations;
            }
        }
        else
        {
            pass.aliasTextures.push_back(resource.handle);
            if (m_poison)
            {
                ++m_stats.poisonInitializations;
            }
            if (resource.desc.allowDepthStencil)
            {
                RHIDepthTargetDesc depth{};
                depth.resource = resource.handle;
                depth.format = resource.desc.format;
                depth.sliceCount = resource.desc.arraySize > 1 ? resource.desc.arraySize : 0;
                auto binding = m_deviceServices->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                pass.aliasClears.push_back({index, binding});
            }
            else if (resource.desc.allowRenderTarget)
            {
                auto color = RHIColorTargetDesc::Texture(resource.handle);
                color.sliceCount = resource.desc.arraySize > 1 ? resource.desc.arraySize : 0;
                auto binding = m_deviceServices->CreateRenderTargets(std::span<const RHIColorTargetDesc>{&color, 1});
                pass.aliasClears.push_back({index, binding});
            }
        }
    }

    // 실행 순서를 따라가며 상태를 추적한다. 요구 상태와 다르면 그 패스 앞에
    // 전이를 붙인다. 한 패스의 전이는 전부 모아 두었다가 한 번에 넣는다 —
    // 배리어를 호출마다 흩뿌리면 GPU가 그때마다 파이프라인을 비운다.
    for (uint16_t passIndex : std::span{executeOrder})
    {
        Pass& pass = m_passes[passIndex];

        if (pass.repeated)
        {
            PlanRepeatedBarriers(pass, previousWrite);
            std::fill(previousAtomicVersion.begin(), previousAtomicVersion.end(), RGHandle::kInvalid);
        }
        else
        {
            for (const auto& usage : std::span{pass.usages})
            {
                if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size())
                {
                    continue;
                }
                Resource& resource = m_resources[usage.handle.index];
                const bool siblingAtomic = usage.access == RGAccessMode::AtomicMin64 &&
                    previousAtomicVersion[usage.handle.index] == usage.handle.version;
                previousAtomicVersion[usage.handle.index] = usage.access == RGAccessMode::AtomicMin64
                    ? usage.handle.version : RGHandle::kInvalid;

                // ★ 핸들이 유효한가만 본다 (G-2). 예전에는 여기서 `Resolve` 로
                //   포인터를 풀어 배리어 구조체에 박았는데, 그 한 줄 때문에
                //   계획 단계가 DX12 를 알아야 했다. 실물은 기록 시점에 백엔드가
                //   푼다 — `RHIEncoder::ResourceBarriers` 가 핸들을 받는다.
                if (!resource.IsValid())
                {
                    continue;
                }

                if (resource.state == usage.state)
                {
                    // 같은 상태로 연속해서 쓰는 경우, UAV만은 배리어가 필요하다 —
                    // 상태는 그대로지만 앞 패스의 쓰기가 끝났음을 알려야 한다.
                    if (RHIResourceState::UnorderedAccess == usage.state && !siblingAtomic &&
                        (m_scheduling!=RGSchedulingMode::ExplicitVersioned || previousWrite.test(usage.handle.index) || Writes(usage)))
                    {
                        if (resource.IsBuffer())
                        {
                            pass.uavBufferBarriers.push_back(resource.buffer);
                        }
                        else
                        {
                            pass.uavBarriers.push_back(resource.handle);
                        }
                    }
                    previousWrite.set(usage.handle.index, Writes(usage));
                    continue;
                }

                if (resource.IsBuffer())
                {
                    pass.bufferTransitions.push_back(RHIBufferTransition{
                        resource.buffer, resource.state, usage.state });
                }
                else
                {
                    pass.transitions.push_back(RHITransition{
                        resource.handle, resource.state, usage.state });
                }

                resource.state = usage.state;
                previousWrite.set(usage.handle.index, Writes(usage));
            }

            const size_t barrierCount = pass.transitions.size() +
                pass.bufferTransitions.size() + pass.uavBarriers.size() +
                pass.uavBufferBarriers.size() + pass.aliasTextures.size() + pass.aliasBuffers.size() + pass.poisonBuffers.size();
            m_stats.barriersEmitted += static_cast<uint32_t>(barrierCount);
            m_stats.barrierBatches += static_cast<uint32_t>(pass.poisonBuffers.size());
            if (0 != barrierCount)
            {
                ++m_stats.barrierBatches;
            }
        }

        if (queuePlan && batchEnds->test(passIndex))
        {
            // Graphics-only nondecaying textures retain explicit state on the
            // same FIFO queue. Buffers, unknown textures and cross-queue resources
            // keep the original COMMON handoff/decay contract.
            PhaseBarrierPlan boundary;
            for (size_t index = 0; index < m_resources.size(); ++index)
            {
                auto& resource = m_resources[index];
                if (resource.used && !retainsState(index))
                {
                    appendTransition(boundary, resource, resource.state, RHIResourceState::Common);
                    resource.state = RHIResourceState::Common;
                    previousWrite.set(index, true);
                }
            }
            pass.finalTransitions = std::move(boundary.transitions);
            pass.finalBufferTransitions = std::move(boundary.bufferTransitions);
            const auto count = pass.finalTransitions.size() + pass.finalBufferTransitions.size();
            m_stats.barriersEmitted += static_cast<uint32_t>(count);
            if (count != 0)
            {
                ++m_stats.barrierBatches;
            }
        }
    }

    if (queuePlan)
    {
        for (size_t index = 0; index < m_resources.size(); ++index)
        {
            auto& resource = m_resources[index];
            if (resource.used)
            {
                appendTransition(*epilogue, resource, resource.state, finalStates[index]);
            }
            resource.state = finalStates[index];
        }
        for (const auto* boundary : {prologue, epilogue})
        {
            const auto count = boundary->transitions.size() + boundary->bufferTransitions.size();
            m_stats.barriersEmitted += static_cast<uint32_t>(count);
            if (count != 0)
            {
                ++m_stats.barrierBatches;
            }
        }
        return;
    }

    // 다른 뷰·다음 프레임도 같은 캐시 상태로 시작하도록 마지막 소비 뒤 복구한다.
    // 분할 패스의 마지막 조각에 기록하므로 일부 draw 앞에서 복구하지 않는다.
    for (const auto& requirement : std::span{m_finalStateRequirements})
    {
        auto& resource = m_resources[requirement.handle.index];
        if (!resource.used || resource.state == requirement.state)
        {
            continue;
        }
        auto& pass = m_passes[executeOrder[resource.lastUse]];
        const bool firstFinalBarrier = pass.finalTransitions.empty() && pass.finalBufferTransitions.empty();
        if (resource.IsBuffer())
            pass.finalBufferTransitions.push_back({resource.buffer, resource.state, requirement.state});
        else
            pass.finalTransitions.push_back({resource.handle, resource.state, requirement.state});
        resource.state = requirement.state;
        ++m_stats.barriersEmitted;
        if (firstFinalBarrier)
        {
            ++m_stats.barrierBatches;
        }
    }

    // Final states remain planned until the recording is accepted by the queue.
}

bool EnhancedRenderGraph::CaptureDiagnosticSnapshot(DiagnosticSnapshot& output) const
{
    const auto& executeOrder = GetExecuteOrder();
    output = {};
    if (!m_compiled)
    {
        return false;
    }
    output.generation = m_compileGeneration;
    output.graphEpoch = m_resourceEpoch;
    output.aliasHeapMemory = GetAliasHeapMemory();
    output.scheduling = m_scheduling;
    output.orderPolicy = m_orderPolicy;
    output.executeOrder = executeOrder;
    output.queueExecution = m_queueDiagnostics;
    for (const auto& resource : std::span{m_resources})
    {
        output.resources.push_back({resource.name, resource.imported, resource.IsBuffer(),
            resource.used, resource.firstUse, resource.lastUse,
            static_cast<uint32_t>(resource.versions.size()), resource.initialState, resource.state,
            resource.aliasGroup, resource.allocationBytes});
    }
    const auto textureIndex = [this](RHITextureHandle handle) -> uint32_t
    {
        for (uint32_t i = 0; i < m_resources.size(); ++i)
        {
            if (!m_resources[i].IsBuffer() && m_resources[i].handle == handle)
            {
                return i;
            }
        }
        return RGHandle::kInvalid;
    };
    const auto bufferIndex = [this](RHIBufferHandle handle) -> uint32_t
    {
        for (uint32_t i = 0; i < m_resources.size(); ++i)
        {
            if (m_resources[i].IsBuffer() && m_resources[i].buffer == handle)
            {
                return i;
            }
        }
        return RGHandle::kInvalid;
    };
    for (uint32_t i = 0; i < m_passes.size(); ++i)
    {
        const auto& pass = m_passes[i];
        DiagnosticPass copy{pass.name, i, -1, pass.culled, pass.hasSideEffect,
            pass.recordCost, pass.maxSlices, {}, {}};
        const auto position = std::find(executeOrder.begin(), executeOrder.end(), i);
        if (position != executeOrder.end())
        {
            copy.compiledIndex = static_cast<int32_t>(position - executeOrder.begin());
        }
        for (const auto& usage : std::span{pass.usages})
        {
            copy.usages.push_back({usage.handle.index, usage.state, Writes(usage), usage.access, usage.handle.version, usage.handle.kind});
        }
        for (const auto handle : pass.aliasTextures)
        {
            copy.barriers.push_back({textureIndex(handle), RHIResourceState::Common,
                RHIResourceState::Common, false, false, true});
        }
        for (const auto handle : pass.aliasBuffers)
        {
            copy.barriers.push_back({bufferIndex(handle), RHIResourceState::Common,
                RHIResourceState::Common, false, false, true});
        }
        for (const auto index : pass.poisonBuffers)
        {
            copy.barriers.push_back({index, RHIResourceState::UnorderedAccess,
                RHIResourceState::UnorderedAccess, true});
        }
        for (const auto& barrier : pass.transitions)
        {
            copy.barriers.push_back({textureIndex(barrier.texture), barrier.before, barrier.after, false});
        }
        for (const auto& barrier : pass.bufferTransitions)
        {
            copy.barriers.push_back({bufferIndex(barrier.buffer), barrier.before, barrier.after, false});
        }
        for (const auto handle : pass.uavBarriers)
        {
            copy.barriers.push_back({textureIndex(handle), RHIResourceState::UnorderedAccess,
                RHIResourceState::UnorderedAccess, true});
        }
        for (const auto handle : pass.uavBufferBarriers)
        {
            copy.barriers.push_back({bufferIndex(handle), RHIResourceState::UnorderedAccess,
                RHIResourceState::UnorderedAccess, true});
        }
        for (const auto& barrier : pass.finalTransitions)
        {
            copy.barriers.push_back({textureIndex(barrier.texture), barrier.before, barrier.after, false, true});
        }
        for (const auto& barrier : pass.finalBufferTransitions)
        {
            copy.barriers.push_back({bufferIndex(barrier.buffer), barrier.before, barrier.after, false, true});
        }
        copy.repeatCount = pass.repeatCount;
        for (size_t phaseIndex = 0; phaseIndex < pass.phases.size(); ++phaseIndex)
        {
            DiagnosticPass::Phase phase;
            phase.name = pass.phases[phaseIndex].name;
            for (const auto& usage : std::span{pass.phases[phaseIndex].usages})
            {
                phase.usages.push_back({usage.handle.index, usage.state, Writes(usage), usage.access,
                    usage.handle.version, usage.handle.kind});
            }
            const auto copyPlan = [&](const PhaseBarrierPlan& plan, std::vector<DiagnosticBarrier>& barriers)
            {
                for (const auto& barrier : plan.transitions)
                {
                    barriers.push_back({textureIndex(barrier.texture), barrier.before, barrier.after, false});
                }
                for (const auto& barrier : plan.bufferTransitions)
                {
                    barriers.push_back({bufferIndex(barrier.buffer), barrier.before, barrier.after, false});
                }
                for (const auto handle : plan.uavBarriers)
                {
                    barriers.push_back({textureIndex(handle), RHIResourceState::UnorderedAccess,
                        RHIResourceState::UnorderedAccess, true});
                }
                for (const auto handle : plan.uavBufferBarriers)
                {
                    barriers.push_back({bufferIndex(handle), RHIResourceState::UnorderedAccess,
                        RHIResourceState::UnorderedAccess, true});
                }
            };
            if (phaseIndex < pass.firstPhaseBarriers.size())
            {
                copyPlan(pass.firstPhaseBarriers[phaseIndex], phase.firstBarriers);
            }
            if (phaseIndex < pass.repeatPhaseBarriers.size())
            {
                copyPlan(pass.repeatPhaseBarriers[phaseIndex], phase.repeatBarriers);
            }
            copy.phases.push_back(std::move(phase));
        }
        output.passes.push_back(std::move(copy));
    }
    output.versionEdges = VersionEdges();
    output.dependencyWaves = DependencyWaves();
    output.criticalPath = CriticalPath();
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned)
    {
        for (const auto& edge : VersionEdges())
        {
            if (edge.reason == DiagnosticSnapshot::VersionEdge::Reason::RAW)
            {
                output.reachabilityEdges.push_back({edge.producer,edge.consumer,edge.resource});
            }
        }
    }
    else
    {
        // 기존 진단의 도달성 정보는 실제 version edge와 구분해 보존한다.
        for (uint32_t consumer = 0; consumer < output.passes.size(); ++consumer)
        {
            for (const auto& read : output.passes[consumer].usages)
            {
                if (!read.inferredWrite)
                {
                    for (uint32_t producer = 0; producer < output.passes.size(); ++producer)
                    {
                        for (const auto& write : output.passes[producer].usages)
                        {
                            if (write.inferredWrite && write.resource == read.resource)
                            {
                                output.reachabilityEdges.push_back({producer, consumer, read.resource});
                            }
                        }
                    }
                }
            }
        }
    }

    // 물리 핸들·epoch·풀의 이전 state는 같은 논리 의존성의 해시를 바꾸지 않는다.
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](uint64_t value)
    {
        for (uint32_t byte = 0; byte < sizeof(value); ++byte)
        {
            hash ^= (value >> (byte * 8u)) & 0xFFu;
            hash *= 1099511628211ull;
        }
    };
    const auto mixName = [&mix](const std::string& name)
    {
        mix(name.size());
        for (const unsigned char character : name)
        {
            mix(character);
        }
    };
    mix(static_cast<uint64_t>(m_scheduling));
    mix(static_cast<uint64_t>(m_orderPolicy));
    mix(m_resources.size());
    for (const auto& resource : std::span{m_resources})
    {
        mixName(resource.name);
        mix(resource.imported);
        mix(resource.IsBuffer());
        mix(resource.used);
        mix(resource.firstUse);
        mix(resource.lastUse);
        mix(resource.versions.size());
        for (const auto& version : std::span{resource.versions})
        {
            mix(version.parent);
            mix(version.modify);
            mix(version.atomicMin64);
        }
    }
    mix(output.passes.size());
    for (const auto& pass : output.passes)
    {
        mixName(pass.name);
        mix(pass.culled);
        mix(pass.sideEffect);
        mix(pass.usages.size());
        for (const auto& usage : std::span{pass.usages})
        {
            mix(usage.resource);
            mix(usage.version);
            mix(static_cast<uint64_t>(usage.kind));
            mix(static_cast<uint64_t>(usage.access));
            mix(static_cast<uint64_t>(usage.state));
        }
        mix(pass.repeatCount);
        mix(pass.phases.size());
        for (const auto& phase : std::span{pass.phases})
        {
            mixName(phase.name);
            mix(phase.usages.size());
            for (const auto& usage : std::span{phase.usages})
            {
                mix(usage.resource);
                mix(usage.version);
                mix(static_cast<uint64_t>(usage.kind));
                mix(static_cast<uint64_t>(usage.access));
                mix(static_cast<uint64_t>(usage.state));
            }
        }
    }
    mix(output.executeOrder.size());
    for (const auto pass : output.executeOrder)
    {
        mix(pass);
    }
    mix(output.versionEdges.size());
    for (const auto& edge : output.versionEdges)
    {
        mix(edge.producer);
        mix(edge.consumer);
        mix(edge.resource);
        mix(edge.version);
        mix(static_cast<uint64_t>(edge.reason));
    }
    mix(m_finalStateRequirements.size());
    for (const auto& requirement : m_finalStateRequirements)
    {
        mix(requirement.handle.index);
        mix(static_cast<uint64_t>(requirement.state));
    }
    output.dependencyHash = hash;
    return true;
}

void EnhancedRenderGraph::BuildDependencyWaves()
{
    m_dependencyWaves.assign(m_passes.size(),-1);
    m_criticalPath.clear();
    if(m_scheduling!=RGSchedulingMode::ExplicitVersioned) return;
    std::vector<std::vector<uint16_t>> predecessors(m_passes.size());
    for(const auto& edge:m_versionEdges) predecessors[edge.consumer].push_back(static_cast<uint16_t>(edge.producer));
    std::vector<uint16_t> parent(m_passes.size(),RGPassId::kInvalid);
    uint16_t last=RGPassId::kInvalid;
    for(const auto p:std::span{m_executeOrder})
    {
        m_dependencyWaves[p]=0;
        for(const auto producer:predecessors[p])
        {
            const auto depth=m_dependencyWaves[producer]+1;
            if(depth>m_dependencyWaves[p] || (depth==m_dependencyWaves[p] && producer<parent[p]))
            { m_dependencyWaves[p]=depth; parent[p]=producer; }
        }
        if(last==RGPassId::kInvalid || m_dependencyWaves[p]>m_dependencyWaves[last] ||
            (m_dependencyWaves[p]==m_dependencyWaves[last] && p<last)) last=p;
    }
    if(last!=RGPassId::kInvalid)
    {
        m_stats.dependencyWaveCount=static_cast<uint32_t>(m_dependencyWaves[last]+1);
        for(auto p=last;p!=RGPassId::kInvalid;p=parent[p]) m_criticalPath.push_back(p);
        std::reverse(m_criticalPath.begin(),m_criticalPath.end());
    }
}

const std::vector<uint16_t>& EnhancedRenderGraph::GetExecuteOrder() const
{
    return m_activeStructuralSchedule && !m_executeOrderOverridden ?
        m_activeStructuralSchedule->order : m_executeOrder;
}

bool EnhancedRenderGraph::MatchesStructuralSchedule(const StructuralSchedule& schedule) const
{
    if (!schedule.key || !schedule.timingSignature || schedule.scheduling != m_scheduling ||
        schedule.orderPolicy != m_orderPolicy || schedule.measurementDomain != m_measurementDomain ||
        schedule.aliasing != m_aliasing || schedule.extendLifetimes != m_extendLifetimes ||
        schedule.poison != m_poison || schedule.resources.size() != m_resources.size() ||
        schedule.passes.size() != m_passes.size() ||
        schedule.finalStateRequirements.size() != m_finalStateRequirements.size())
    {
        return false;
    }
    // Compare the immutable declarations directly. No hash may authorize reuse,
    // and current epochs are checked even though the cached template has none.
    const auto sameHandle = [&](const RGHandle& current, const RGHandle& cached)
    {
        return current.index == cached.index && current.version == cached.version &&
            current.kind == cached.kind &&
            (m_scheduling != RGSchedulingMode::ExplicitVersioned || ValidVersionHandle(current));
    };
    const auto sameUsages = [&](const std::vector<RGPassUsage>& current, const std::vector<RGPassUsage>& cached)
    {
        if (current.size() != cached.size())
        {
            return false;
        }
        for (size_t index = 0; index < current.size(); ++index)
        {
            const auto& left = current[index];
            const auto& right = cached[index];
            if (!sameHandle(left.handle, right.handle) || left.state != right.state || left.access != right.access)
            {
                return false;
            }
        }
        return true;
    };
    for (size_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& current = m_resources[index];
        const auto& cached = schedule.resources[index];
        if (current.name != cached.name || current.imported != cached.imported ||
            current.bufferKind != cached.bufferKind || current.firstImport != cached.firstImport ||
            current.versions.size() != cached.versions.size())
        {
            return false;
        }
        if (current.IsBuffer())
        {
            if (current.bufferDesc.bytes != cached.buffer.bytes ||
                current.bufferDesc.allowUnorderedAccess != cached.buffer.allowUnorderedAccess ||
                current.bufferDesc.allowIndirectArguments != cached.buffer.allowIndirectArguments)
            {
                return false;
            }
        }
        else
        {
            const auto& left = current.desc;
            const auto& right = cached.texture;
            if (left.width != right.width || left.height != right.height || left.arraySize != right.arraySize ||
                left.format != right.format || left.allowRenderTarget != right.allowRenderTarget ||
                left.allowDepthStencil != right.allowDepthStencil || left.allowUnorderedAccess != right.allowUnorderedAccess)
            {
                return false;
            }
            for (size_t component = 0; component < 4; ++component)
            {
                if (std::bit_cast<uint32_t>(left.clearColor[component]) !=
                    std::bit_cast<uint32_t>(right.clearColor[component]))
                {
                    return false;
                }
            }
        }
        for (size_t version = 0; version < current.versions.size(); ++version)
        {
            const auto& left = current.versions[version];
            const auto& right = cached.versions[version];
            if (left.parent != right.parent || left.modify != right.modify || left.atomicMin64 != right.atomicMin64)
            {
                return false;
            }
        }
    }
    for (size_t index = 0; index < m_passes.size(); ++index)
    {
        const auto& current = m_passes[index];
        const auto& cached = schedule.passes[index];
        if (current.name != cached.name || current.hasSideEffect != cached.hasSideEffect ||
            current.computeCompatible != cached.computeCompatible || current.recordingSideEffect != cached.recordingSideEffect ||
            current.recordingIndependent != cached.recordingIndependent || current.maxSlices != cached.maxSlices ||
            current.recordCost != cached.recordCost || current.repeatCount != cached.repeatCount ||
            bool(current.execute) != cached.execute || bool(current.splitExecute) != cached.splitExecute ||
            current.repeated != cached.repeated || bool(current.repeatedExecute) != cached.repeatedExecute ||
            current.phases.size() != cached.phases.size() || !sameUsages(current.usages, cached.usages))
        {
            return false;
        }
        for (size_t phase = 0; phase < current.phases.size(); ++phase)
        {
            if (current.phases[phase].name != cached.phases[phase].name ||
                !sameUsages(current.phases[phase].usages, cached.phases[phase].usages))
            {
                return false;
            }
        }
    }
    for (size_t index = 0; index < m_finalStateRequirements.size(); ++index)
    {
        const auto& current = m_finalStateRequirements[index];
        const auto& cached = schedule.finalStateRequirements[index];
        if (current.state != cached.state || !sameHandle(current.handle, cached.handle))
        {
            return false;
        }
    }
    return true;
}

void EnhancedRenderGraph::CaptureStructuralDeclarations(StructuralSchedule& schedule) const
{
    schedule.scheduling = m_scheduling;
    schedule.orderPolicy = m_orderPolicy;
    schedule.measurementDomain = m_measurementDomain;
    schedule.aliasing = m_aliasing;
    schedule.extendLifetimes = m_extendLifetimes;
    schedule.poison = m_poison;
    schedule.dependencyWaveCount = m_stats.dependencyWaveCount;
    schedule.resources.reserve(m_resources.size());
    for (const auto& resource : std::span{m_resources})
    {
        auto& declaration = schedule.resources.emplace_back();
        declaration.texture = resource.desc;
        declaration.buffer = resource.bufferDesc;
        declaration.name = resource.name;
        declaration.imported = resource.imported;
        declaration.bufferKind = resource.bufferKind;
        declaration.firstImport = resource.firstImport;
        declaration.versions = resource.versions;
        declaration.used = resource.used;
        declaration.firstUse = resource.firstUse;
        declaration.lastUse = resource.lastUse;
    }
    const auto forgetEpochs = [](std::vector<RGPassUsage>& usages)
    {
        for (auto& usage : usages)
        {
            usage.handle.epoch = 0;
        }
    };
    schedule.passes.reserve(m_passes.size());
    for (const auto& pass : std::span{m_passes})
    {
        auto& declaration = schedule.passes.emplace_back();
        declaration.name = pass.name;
        declaration.usages = pass.usages;
        declaration.phases = pass.phases;
        forgetEpochs(declaration.usages);
        for (auto& phase : declaration.phases)
        {
            forgetEpochs(phase.usages);
        }
        declaration.maxSlices = pass.maxSlices;
        declaration.recordCost = pass.recordCost;
        declaration.repeatCount = pass.repeatCount;
        declaration.hasSideEffect = pass.hasSideEffect;
        declaration.computeCompatible = pass.computeCompatible;
        declaration.recordingSideEffect = pass.recordingSideEffect;
        declaration.recordingIndependent = pass.recordingIndependent;
        declaration.execute = bool(pass.execute);
        declaration.splitExecute = bool(pass.splitExecute);
        declaration.repeated = pass.repeated;
        declaration.repeatedExecute = bool(pass.repeatedExecute);
    }
    schedule.finalStateRequirements = m_finalStateRequirements;
    for (auto& requirement : schedule.finalStateRequirements)
    {
        requirement.handle.epoch = 0;
    }
}

void EnhancedRenderGraph::InheritStructuralSchedule(const EnhancedRenderGraph& previous)
{
    std::array<std::shared_ptr<const StructuralSchedule>, MaxStructuralSchedules> schedules;
    schedules[0] = previous.m_structuralSchedule;
    schedules[1] = previous.m_alternateSchedule;
    std::copy(previous.m_additionalSchedules.begin(), previous.m_additionalSchedules.end(), schedules.begin() + 2);
    InheritStructuralSchedule(schedules);
}

void EnhancedRenderGraph::InheritStructuralSchedule(const std::shared_ptr<const StructuralSchedule>& previous)
{
    InheritStructuralSchedule(previous, {});
}

void EnhancedRenderGraph::InheritStructuralSchedule(const std::shared_ptr<const StructuralSchedule>& previous,
    const std::shared_ptr<const StructuralSchedule>& alternate)
{
    RequireQueueIdle();
    if (previous)
    {
        m_structuralSchedule = previous;
    }
    m_alternateSchedule = alternate;
    m_additionalSchedules.fill({});
    m_compiled = false;
}

void EnhancedRenderGraph::InheritStructuralSchedule(
    std::span<const std::shared_ptr<const StructuralSchedule>> schedules)
{
    InheritStructuralSchedule(schedules.empty() ? nullptr : schedules[0],
        schedules.size() < 2 ? nullptr : schedules[1]);
    for (size_t index = 2; index < (std::min)(schedules.size(), MaxStructuralSchedules); ++index)
    {
        m_additionalSchedules[index - 2] = schedules[index];
    }
}

std::shared_ptr<const EnhancedRenderGraph::StructuralSchedule> EnhancedRenderGraph::SnapshotStructuralSchedule() const
{
    return m_structuralSchedule && m_structuralSchedule->key ? m_structuralSchedule : nullptr;
}

bool EnhancedRenderGraph::Compile(std::string& outError)
{
    if (!m_queueLease.expired() || m_queueExecutionAttempted)
    {
        outError = "Queue execution requires completion collection and Reset before reuse";
        return false;
    }
    ce::profile_scope profile{ce::marker<"RenderGraphCompile">()};
    // ★ 디바이스를 인자로 받지 않는다 (G-1). 생성자에서 받은 중립 service를
    //   그대로 쓰므로 호출부가 같은 값을 도로 넘기지 않는다.
    if (nullptr == m_deviceServices)
    {
        outError = "디바이스 서비스가 없다";
        return false;
    }

    m_compiled = false;
    m_stats = Stats{};
    m_timingSignature.reset();
    m_activeStructuralSchedule.reset();
    m_executeOrderOverridden = false;
    if (!m_transientHeaps.empty())
    {
        outError = "An aliased graph must be Reset after GPU completion before recompilation";
        return false;
    }
    m_stats.passesDeclared = static_cast<uint32_t>(m_passes.size());

    if (!m_declarationError.empty())
    {
        outError = m_declarationError;
        return false;
    }

    // Device/storage validity is not structural: validate even when reusing a DAG.
    // Declaration records the affected resources once; ordinary version chains
    // need no per-frame atomic-capability scan.
    for (const auto index : std::span{m_atomicResources})
    {
        const auto& resource = m_resources[index];
        if (!m_deviceServices->GetAtomicCapabilities().bufferInt64Min || !resource.IsBuffer() ||
            (resource.imported ? !m_deviceServices->IsAtomicMin64Buffer(resource.buffer) :
                (!resource.bufferDesc.allowUnorderedAccess || resource.bufferDesc.bytes == 0 || resource.bufferDesc.bytes % 8 != 0)))
        {
            outError = "AtomicMin64 requires supported, valid 8-byte aligned UAV buffer storage: " + resource.name;
            return false;
        }
    }
    {
        ce::profile_scope scheduleProfile{ce::marker<"RenderGraphSchedule">()};
        bool reuse = m_structuralSchedule && MatchesStructuralSchedule(*m_structuralSchedule);
        if (!reuse && m_alternateSchedule && MatchesStructuralSchedule(*m_alternateSchedule))
        {
            std::swap(m_structuralSchedule, m_alternateSchedule);
            reuse = true;
        }
        if (!reuse)
        {
            for (auto& schedule : m_additionalSchedules)
            {
                if (schedule && MatchesStructuralSchedule(*schedule))
                {
                    std::swap(m_structuralSchedule, schedule);
                    reuse = true;
                    break;
                }
            }
        }
        if (reuse)
        {
            m_activeStructuralSchedule = m_structuralSchedule;
            m_timingSignature = m_structuralSchedule->timingSignature;
            for (size_t i = 0; i < m_passes.size(); ++i)
            {
                m_passes[i].culled = m_structuralSchedule->culled.test(i);
            }
            m_stats.passesCulled = static_cast<uint32_t>(m_passes.size() - GetExecuteOrder().size());
            m_stats.structuralScheduleReused = true;
            m_stats.dependencyWaveCount = m_structuralSchedule->dependencyWaveCount;
            ce::profile_instant(ce::marker<"RenderGraphScheduleReused">());
        }
        else
        {
            // A cached contract passed these structural validators previously.
            // Misses (including stale epochs or missing callbacks) take the full
            // validation path; no validity decision is made from a hash alone.
            for (auto& pass : m_passes)
            {
                pass.culled = false;
            }
            if (!ValidateFinalStates(outError) || !ValidateRepeatedPasses(outError))
            {
                return false;
            }
            BuildTimingSignature();
            const auto structuralKey = m_timingSignature;
            AuditStructuralMiss(*structuralKey, m_structuralSchedule ? m_structuralSchedule->key : nullptr,
                m_alternateSchedule ? m_alternateSchedule->key : nullptr);
            if (!BuildOrder(outError))
            {
                return false;
            }
            CullPasses();
            BuildDependencyWaves();
            if (m_stats.passesCulled != 0)
            {
                PatchTimingSignatureReachability();
            }
            // Bound retained metadata. Large/diagnostic graphs still compile normally.
            if (structuralKey->size() <= 1024 * 1024 && m_passes.size() <= 4096)
            {
                UpdateResourceUses();
                auto schedule = std::make_shared<StructuralSchedule>();
                CaptureStructuralDeclarations(*schedule);
                schedule->key = structuralKey;
                schedule->timingSignature = m_timingSignature;
                schedule->order = std::move(m_executeOrder);
                schedule->waves = std::move(m_dependencyWaves);
                schedule->criticalPath = std::move(m_criticalPath);
                schedule->edges = std::move(m_versionEdges);
                schedule->culled.reserve(m_passes.size());
                for (const auto& pass : std::span{m_passes})
                {
                    schedule->culled.push_back(pass.culled);
                }
                for (size_t index = m_additionalSchedules.size() - 1; index > 0; --index)
                {
                    m_additionalSchedules[index] = std::move(m_additionalSchedules[index - 1]);
                }
                m_additionalSchedules[0] = std::move(m_alternateSchedule);
                m_alternateSchedule = std::move(m_structuralSchedule);
                m_structuralSchedule = std::move(schedule);
                m_activeStructuralSchedule = m_structuralSchedule;
            }
        }
    }

    if (!CreateTransients(outError)) return false;

    PlanBarriers();
    for (const auto& pass : m_passes)
    {
        for (const auto& clear : pass.aliasClears)
        {
            if (!clear.second.IsValid())
            {
                outError = "Cannot initialize placed render/depth target";
                return false;
            }
        }
    }

    m_stats.passesExecuted = static_cast<uint32_t>(GetExecuteOrder().size());
    m_compileGeneration = NextRenderGraphCompileGeneration();
    m_compiled = true;
    return true;
}

void EnhancedRenderGraph::DeclareComputeCompatible(RGPassId pass)
{
    RequireQueueIdle();
    if (!pass.IsValid() || pass.index >= m_passes.size() || m_compiled)
    {
        throw std::logic_error("Compute compatibility must be declared before compilation on a valid pass");
    }
    m_passes[pass.index].computeCompatible = true;
}

void EnhancedRenderGraph::DeclareRecordingSideEffect(RGPassId pass)
{
    RequireQueueIdle();
    if (!pass.IsValid() || pass.index >= m_passes.size() || m_compiled || m_passes[pass.index].recordingIndependent)
    {
        throw std::logic_error("Recording side effects must be declared before compilation on a valid pass");
    }
    m_passes[pass.index].recordingSideEffect = true;
    m_passes[pass.index].hasSideEffect = true;
}

void EnhancedRenderGraph::DeclareRecordingIndependent(RGPassId pass)
{
    RequireQueueIdle();
    if (!pass.IsValid() || pass.index >= m_passes.size() || m_compiled ||
        m_passes[pass.index].recordingSideEffect)
    {
        throw std::logic_error("Independent recording requires a pass without external CPU side effects before compilation");
    }
    m_passes[pass.index].recordingIndependent = true;
}

void EnhancedRenderGraph::BuildTimingSignature()
{
    ce::profile_scope profile{ce::marker<"RenderGraphSignature">()};
    // 프레임마다 달라지는 핸들/epoch/포인터는 빼되 선언 슬롯, 이름, 접근과
    // 반복 구조는 전부 넣는다. 문자열 길이를 먼저 적으므로 같은 이름, 빈 이름,
    // '(xN)'이 실제 이름인 경우도 서로 다른 선언을 합치지 않는다.
    m_signatureCulledOffsets.clear();
    m_signatureCulledOffsets.reserve(m_passes.size());
    std::string signature;
    signature.reserve(m_structuralSchedule && m_structuralSchedule->key ? m_structuralSchedule->key->size() :
        m_resources.size() * 256 + m_passes.size() * 256);
    // The object bytes are already the exact little-endian key; append them once.
    static_assert(std::endian::native == std::endian::little);
    const auto number = [&](uint64_t value)
    {
        signature.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    // Same bytes as one number() per value, in one append.
    const auto numbers = [&](std::initializer_list<uint64_t> values)
    {
        signature.append(reinterpret_cast<const char*>(values.begin()), values.size() * sizeof(uint64_t));
    };
    const auto name = [&](const std::string& value)
    {
        number(value.size());
        signature.append(value);
    };
    const auto usages = [&](const std::vector<RGPassUsage>& values)
    {
        number(values.size());
        for (const auto& usage : std::span{values})
        {
            numbers({usage.handle.index, usage.handle.version,
                uint64_t{m_scheduling != RGSchedulingMode::ExplicitVersioned || ValidVersionHandle(usage.handle)},
                static_cast<uint64_t>(usage.handle.kind), static_cast<uint64_t>(usage.state),
                static_cast<uint64_t>(usage.access)});
        }
    };
    number(static_cast<uint64_t>(m_measurementDomain));
    number(static_cast<uint64_t>(m_scheduling));
    number(static_cast<uint64_t>(m_orderPolicy));
    number(m_aliasing);
    number(m_extendLifetimes);
    number(m_poison);
    number(m_resources.size());
    for (uint32_t index = 0; index < m_resources.size(); ++index)
    {
        const auto& resource = m_resources[index];
        name(resource.name);
        number(resource.imported);
        number(resource.bufferKind);
        if (resource.imported)
        {
            number(resource.firstImport);
        }
        if (resource.bufferKind)
        {
            number(resource.bufferDesc.bytes);
            number(resource.bufferDesc.allowUnorderedAccess);
            number(resource.bufferDesc.allowIndirectArguments);
        }
        else
        {
            numbers({resource.desc.width, resource.desc.height, resource.desc.arraySize,
                static_cast<uint64_t>(resource.desc.format), uint64_t(resource.desc.allowRenderTarget),
                uint64_t(resource.desc.allowDepthStencil), uint64_t(resource.desc.allowUnorderedAccess)});
            for (const float component : resource.desc.clearColor)
            {
                number(std::bit_cast<uint32_t>(component));
            }
        }
        number(resource.versions.size());
        for (const auto& version : std::span{resource.versions})
        {
            numbers({version.parent, uint64_t(version.modify), uint64_t(version.atomicMin64)});
        }
    }
    number(m_passes.size());
    for (const auto& pass : std::span{m_passes})
    {
        name(pass.name);
        m_signatureCulledOffsets.push_back(signature.size());
        numbers({uint64_t(pass.culled), uint64_t(pass.hasSideEffect), uint64_t(pass.computeCompatible),
            uint64_t(pass.recordingSideEffect), pass.maxSlices, pass.recordCost,
            uint64_t(static_cast<bool>(pass.splitExecute)), uint64_t(pass.repeated), pass.repeatCount,
            uint64_t(pass.recordingIndependent), uint64_t(static_cast<bool>(pass.execute)),
            uint64_t(static_cast<bool>(pass.repeatedExecute))});
        usages(pass.usages);
        number(pass.phases.size());
        for (const auto& phase : std::span{pass.phases})
        {
            name(phase.name);
            usages(phase.usages);
        }
    }
    number(m_finalStateRequirements.size());
    for (const auto& requirement : std::span{m_finalStateRequirements})
    {
        numbers({requirement.handle.index, requirement.handle.version,
            static_cast<uint64_t>(requirement.handle.kind), static_cast<uint64_t>(requirement.state)});
    }
    m_timingSignature = std::make_shared<const std::string>(std::move(signature));
}

// Compilation changes only reachability, so rewrite those fields in a copy
// instead of serializing the whole graph again. The result is byte-identical
// to BuildTimingSignature() on the compiled graph.
void EnhancedRenderGraph::PatchTimingSignatureReachability()
{
    if (!m_timingSignature || m_signatureCulledOffsets.size() != m_passes.size())
    {
        BuildTimingSignature();
        return;
    }
    ce::profile_scope profile{ce::marker<"RenderGraphSignature">()};
    std::string signature = *m_timingSignature;
    for (size_t i = 0; i < m_passes.size(); ++i)
    {
        const uint64_t culled = m_passes[i].culled;
        std::memcpy(signature.data() + m_signatureCulledOffsets[i], &culled, sizeof(culled));
    }
    m_timingSignature = std::make_shared<const std::string>(std::move(signature));
}

std::vector<EnhancedRenderGraph::QueueHint> EnhancedRenderGraph::MeasuredQueueHints(
    const std::function<std::optional<uint64_t>(const GpuPassTimingIdentity&)>& measurement) const
{
    const auto& executeOrder = GetExecuteOrder();
    std::vector<QueueHint> hints;
    if (!m_compiled || !m_timingSignature)
    {
        return hints;
    }
    // 겹쳐 실행할 graphics 비용도 필요하다. 표시 이름이 아닌 선언 정체성으로
    // 조회해야 같은 이름의 다른 패스와 분할 조각이 서로 비용을 덮지 않는다.
    hints.reserve(executeOrder.size());
    for (const auto index : std::span{executeOrder})
    {
        const auto& pass = m_passes[index];
        const auto sample = measurement(TimingIdentity(index));
        hints.push_back({RGPassId{index}, pass.computeCompatible, sample.value_or(0), sample.has_value()});
    }
    return hints;
}

bool EnhancedRenderGraph::IsComputeStateCompatible(uint16_t passIndex) const
{
    const auto& pass = m_passes[passIndex];
    if (pass.recordingSideEffect)
    {
        return false;
    }
    const auto compatible = [this](const std::vector<RGPassUsage>& usages)
    {
        for (const auto& usage : usages)
        {
            // Reduction storage stays on graphics until cross-queue simultaneous UAV ownership is supported.
            if (usage.handle.index < m_resources.size() &&
                std::any_of(m_resources[usage.handle.index].versions.begin(), m_resources[usage.handle.index].versions.end(),
                    [](const Resource::Version& v) { return v.atomicMin64; })) return false;
            // This is the RHI compute encoder's supported legacy-state subset.
            // ShaderResource is narrowed to NON_PIXEL only after a COMMON handoff.
            switch (usage.state)
            {
            case RHIResourceState::Common:
            case RHIResourceState::ShaderResource:
            case RHIResourceState::UnorderedAccess:
            case RHIResourceState::CopySource:
            case RHIResourceState::CopyDest:
            case RHIResourceState::IndirectArgument:
                break;
            default:
                return false;
            }
        }
        return true;
    };
    if (!compatible(pass.usages))
    {
        return false;
    }
    for (const auto& phase : std::span{pass.phases})
    {
        if (!compatible(phase.usages))
        {
            return false;
        }
    }
    return true;
}

struct EnhancedRenderGraph::QueuePlanningScratch
{
    struct Timer
    {
        double& subtotal;
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        ~Timer() { subtotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
    };
    double dependencyMilliseconds{0}, simulationMilliseconds{0};
    struct ReadEpoch
    {
        std::vector<uint16_t> frontier, before;
        RHIResourceState state{RHIResourceState::Common};
        RHIQueueKind queue{RHIQueueKind::Graphics};
        bool immutableRead{false};
        uint16_t atomicVersion{RGHandle::kInvalid};
    };
    std::vector<ReadEpoch> resources;
    std::vector<uint32_t> position, remaining;
    std::vector<uint64_t> earliest, finish;
    std::vector<uint8_t> feedsCompute;
    std::array<std::vector<uint16_t>, 2> ready, future;
};

void EnhancedRenderGraph::BuildQueueDependencies(const std::vector<RHIQueueKind>& queues, QueuePlanningScratch& scratch,
    std::vector<std::vector<uint16_t>>& predecessors,
    std::vector<std::vector<uint16_t>>& successors) const
{
    const auto& executeOrder = GetExecuteOrder();
    QueuePlanningScratch::Timer timer{scratch.dependencyMilliseconds};
    // Trials change queue ownership, not the pass count. Preserve each adjacency
    // list's storage instead of allocating the entire DAG for every trial.
    predecessors.resize(m_passes.size());
    successors.resize(m_passes.size());
    for (auto& list : std::span{predecessors.data(), predecessors.size()}) list.clear();
    for (auto& list : std::span{successors.data(), successors.size()}) list.clear();
    const auto add = [&](uint16_t producer, uint16_t consumer)
    {
        if (producer == consumer || m_passes[producer].culled || m_passes[consumer].culled)
        {
            return;
        }
        predecessors[consumer].push_back(producer);
        successors[producer].push_back(consumer);
    };
    for (const auto& edge : std::span{VersionEdges().data(), VersionEdges().size()})
    {
        add(static_cast<uint16_t>(edge.producer), static_cast<uint16_t>(edge.consumer));
    }
    auto& resources = scratch.resources;
    resources.resize(m_resources.size());
    for (auto& epoch : std::span{resources.data(), resources.size()})
    {
        epoch.frontier.clear();
        epoch.before.clear();
        epoch.state = RHIResourceState::Common;
        epoch.queue = RHIQueueKind::Graphics;
        epoch.immutableRead = false;
        epoch.atomicVersion = RGHandle::kInvalid;
    }
    for (const auto pass : std::span{executeOrder.data(), executeOrder.size()})
    {
        const auto& usages = m_passes[pass].usages;
        for (const auto& usage : std::span{usages.data(), usages.size()})
        {
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size())
            {
                continue;
            }
            auto& epoch = resources[usage.handle.index];
            const bool immutableRead = usage.access == RGAccessMode::Read &&
                usage.state != RHIResourceState::Common && !IsWriteState(usage.state) &&
                !m_passes[pass].repeated;
            // Legacy DX12 maps graphics SRV to PIXEL|NON_PIXEL and compute SRV
            // to NON_PIXEL. Equal RHI enums do not prove cross-queue compatibility.
            // Same-queue readers share one immutable state epoch: that queue owns
            // preparation and batch-boundary COMMON transitions in submission order.
            const bool atomicSibling = usage.access == RGAccessMode::AtomicMin64 &&
                usage.handle.version == epoch.atomicVersion;
            const bool compatible = ((immutableRead && epoch.immutableRead) || atomicSibling) &&
                usage.state == epoch.state && queues[pass] == epoch.queue;
            if (!compatible)
            {
                epoch.before.swap(epoch.frontier);
                epoch.frontier.clear();
            }
            for (const auto predecessor : std::span{epoch.before.data(), epoch.before.size()})
            {
                add(predecessor, pass);
            }
            epoch.frontier.push_back(pass);
            epoch.state = usage.state;
            epoch.queue = queues[pass];
            epoch.immutableRead = immutableRead;
            epoch.atomicVersion = usage.access == RGAccessMode::AtomicMin64 ? usage.handle.version : RGHandle::kInvalid;
        }
    }
    for (auto* lists : {&predecessors, &successors})
    {
        for (auto& list : std::span{lists->data(), lists->size()})
        {
            if (list.size() > 1)
            {
                // These local lists cannot reallocate during sorting. Pointer
                // ranges avoid repeatedly registering short-lived MSVC Debug
                // vector iterators while retaining the same bounds and edges.
                auto* first = list.data();
                auto* last = first + list.size();
                std::sort(first, last);
                list.resize(static_cast<size_t>(std::unique(first, last) - first));
            }
        }
    }
}

uint64_t EnhancedRenderGraph::SimulateQueues(const std::vector<RHIQueueKind>& queues,
    const std::vector<uint64_t>& costs, const std::vector<std::vector<uint16_t>>& predecessors,
    const std::vector<std::vector<uint16_t>>& successors, bool reorder, std::vector<uint16_t>* order,
    QueuePlanningScratch& scratch) const
{
    QueuePlanningScratch::Timer timer{scratch.simulationMilliseconds};
    const auto baseline = SimulateQueueOrder(queues, costs, predecessors, successors, reorder, order, scratch, false);
    if (!reorder) return baseline;
    std::vector<uint16_t> candidateOrder;
    const auto candidate = SimulateQueueOrder(queues, costs, predecessors, successors, true,
        order ? &candidateOrder : nullptr, scratch, true);
    if (candidate < baseline)
    {
        if (order) *order = std::move(candidateOrder);
        return candidate;
    }
    return baseline;
}

uint64_t EnhancedRenderGraph::SimulateQueueOrder(const std::vector<RHIQueueKind>& queues,
    const std::vector<uint64_t>& costs, const std::vector<std::vector<uint16_t>>& predecessors,
    const std::vector<std::vector<uint16_t>>& successors, bool reorder, std::vector<uint16_t>* order,
    QueuePlanningScratch& scratch, bool prioritizeComputeInputs) const
{
    const auto& executeOrder = GetExecuteOrder();
    const uint64_t handoff = m_queueCostModel.handoffNanoseconds;
    const auto addTime = [](uint64_t left, uint64_t right)
    {
        return right > UINT64_MAX - left ? UINT64_MAX : left + right;
    };
    auto& position = scratch.position;
    const auto compiled = std::span{executeOrder.data(), executeOrder.size()};
    position.assign(m_passes.size(), UINT32_MAX);
    for (uint32_t index = 0; index < compiled.size(); ++index)
    {
        position[compiled[index]] = index;
    }
    // Ancestors of compute work go first among equally ready graphics passes so
    // compute starts early; everything else keeps compiled order on ties.
    auto& feedsCompute = scratch.feedsCompute;
    feedsCompute.assign(m_passes.size(), false);
    bool usesCompute = false;
    for (size_t index = compiled.size(); index-- > 0;)
    {
        const auto pass = compiled[index];
        usesCompute = usesCompute || queues[pass] == RHIQueueKind::Compute;
        const auto& nextPasses = successors[pass];
        for (const auto next : std::span{nextPasses.data(), nextPasses.size()})
        {
            if (queues[next] == RHIQueueKind::Compute || feedsCompute[next])
            {
                feedsCompute[pass] = true;
                break;
            }
        }
    }
    auto& remaining = scratch.remaining;
    auto& earliest = scratch.earliest;
    auto& finish = scratch.finish;
    auto& ready = scratch.ready;
    auto& future = scratch.future;
    remaining.assign(m_passes.size(), 0);
    earliest.assign(m_passes.size(), 0);
    finish.assign(m_passes.size(), 0);
    // The first compute batch always waits for the graphics prologue, even
    // without a resource edge. It cannot start before this modeled handoff.
    uint64_t queueFree[2]{0, usesCompute ? handoff : 0};
    const auto slot = [&](uint16_t pass) { return queues[pass] == RHIQueueKind::Compute ? 1 : 0; };
    const auto start = [&](uint16_t pass) { return (std::max)(earliest[pass], queueFree[slot(pass)]); };
    const auto rank = [&](uint16_t pass)
    {
        return position[pass] + (reorder && !feedsCompute[pass] ? static_cast<uint32_t>(executeOrder.size()) : 0u);
    };
    const auto readyLater = [&](uint16_t left, uint16_t right) { return rank(left) > rank(right); };
    const auto deferred = [&](uint16_t pass)
    {
        return prioritizeComputeInputs && !feedsCompute[pass] && queues[pass] != RHIQueueKind::Compute;
    };
    const auto better = [&](uint16_t left, uint16_t right)
    {
        return std::tuple{deferred(left), reorder ? start(left) : 0, rank(left)} <
            std::tuple{deferred(right), reorder ? start(right) : 0, rank(right)};
    };
    const auto futureLater = [&](uint16_t left, uint16_t right)
    {
        if (deferred(left) != deferred(right)) return deferred(left) > deferred(right);
        return earliest[left] > earliest[right] || (earliest[left] == earliest[right] && readyLater(left, right));
    };
    for (uint32_t queue = 0; queue < 2; ++queue)
    {
        ready[queue].clear();
        future[queue].clear();
        ready[queue].reserve(executeOrder.size());
        future[queue].reserve(executeOrder.size());
    }
    const auto enqueue = [&](uint16_t pass)
    {
        const auto queue = slot(pass);
        if (!reorder || earliest[pass] <= queueFree[queue])
        {
            ready[queue].push_back(pass);
            std::push_heap(ready[queue].data(), ready[queue].data() + ready[queue].size(), readyLater);
        }
        else
        {
            future[queue].push_back(pass);
            std::push_heap(future[queue].data(), future[queue].data() + future[queue].size(), futureLater);
        }
    };
    for (const auto pass : compiled)
    {
        remaining[pass] = static_cast<uint32_t>(predecessors[pass].size());
        if (remaining[pass] == 0) enqueue(pass);
    }
    if (order)
    {
        order->clear();
        order->reserve(executeOrder.size());
    }
    // Each queue has two heaps. Future work is ordered by immutable dependency
    // readiness. Once the queue clock catches up, all those starts are clamped
    // to queueFree and must instead be ordered by ancestor/compiled-position rank.
    // Compare only the two queue heads: same list policy, O(P log P + E) rather
    // than rescanning a potentially wide frontier for every dispatched pass.
    while (!ready[0].empty() || !ready[1].empty() || !future[0].empty() || !future[1].empty())
    {
        uint16_t heads[2]{RGPassId::kInvalid, RGPassId::kInvalid};
        for (uint32_t queue = 0; queue < 2; ++queue)
        {
            while (!future[queue].empty() && earliest[future[queue].front()] <= queueFree[queue])
            {
                const auto pass = future[queue].front();
                std::pop_heap(future[queue].data(), future[queue].data() + future[queue].size(), futureLater);
                future[queue].pop_back();
                enqueue(pass);
            }
            if (!ready[queue].empty()) heads[queue] = ready[queue].front();
            else if (!future[queue].empty()) heads[queue] = future[queue].front();
            if (!ready[queue].empty() && !future[queue].empty() &&
                better(future[queue].front(), heads[queue])) heads[queue] = future[queue].front();
        }
        auto pass = heads[0];
        if (pass == RGPassId::kInvalid) pass = heads[1];
        else if (heads[1] != RGPassId::kInvalid)
        {
            if (better(heads[1], pass)) pass = heads[1];
        }
        const auto queue = slot(pass);
        if (!ready[queue].empty() && ready[queue].front() == pass)
        {
            std::pop_heap(ready[queue].data(), ready[queue].data() + ready[queue].size(), readyLater);
            ready[queue].pop_back();
        }
        else
        {
            std::pop_heap(future[queue].data(), future[queue].data() + future[queue].size(), futureLater);
            future[queue].pop_back();
        }
        finish[pass] = addTime(start(pass), costs[pass]);
        queueFree[slot(pass)] = finish[pass];
        if (order)
        {
            order->push_back(pass);
        }
        const auto& nextPasses = successors[pass];
        for (const auto next : std::span{nextPasses.data(), nextPasses.size()})
        {
            const uint64_t available = addTime(finish[pass], queues[next] != queues[pass] ? handoff : 0);
            earliest[next] = (std::max)(earliest[next], available);
            if (--remaining[next] == 0)
            {
                enqueue(next);
            }
        }
    }
    // The graphics epilogue joins the last compute completion.
    return (std::max)(queueFree[0], usesCompute ? addTime(queueFree[1], handoff) : 0);
}

bool EnhancedRenderGraph::BuildQueueSchedule(const RHIQueueCapabilities& capabilities,
    const std::vector<QueueHint>& hints, uint64_t minimumGpuNanoseconds,
    QueueSchedule& output, std::string& outError) const
{
    const auto& executeOrder = GetExecuteOrder();
    output = {};
    outError.clear();
    if (!m_compiled || !capabilities.graphics)
    {
        outError = "Queue planning requires a compiled graph and graphics capability";
        return false;
    }
    std::vector<const QueueHint*> byPass(m_passes.size(), nullptr);
    for (const auto& hint : hints)
    {
        if (!hint.pass.IsValid() || hint.pass.index >= m_passes.size() || byPass[hint.pass.index])
        {
            outError = "Queue hint references an invalid or duplicate pass";
            return false;
        }
        byPass[hint.pass.index] = &hint;
    }
    QueueSchedule plan;
    plan.compileGeneration = m_compileGeneration;
    // RG7 allocation/reuse still requires its validated single-queue lifetime
    // contract. Never trade that guard for a schedule-only overlap estimate.
    const bool allowCompute = capabilities.compute && capabilities.crossQueueTimeline &&
        minimumGpuNanoseconds > 0 && !m_aliasing && m_scheduling == RGSchedulingMode::ExplicitVersioned;
    if (minimumGpuNanoseconds == 0)
    {
        plan.fallbackReason = RGQueueFallbackReason::Disabled;
    }
    else if (!allowCompute)
    {
        plan.fallbackReason = RGQueueFallbackReason::Unsupported;
    }
    const bool overlap = m_queueCostModel.placement == RGQueuePlacement::Overlap;
    std::vector<uint64_t> costs(m_passes.size(), 0);
    std::vector<uint16_t> candidates;
    plan.predictionComplete = true;
    for (const auto pass : std::span{executeOrder})
    {
        const auto* hint = byPass[pass];
        const bool measured = hint && hint->hasGpuMeasurement;
        costs[pass] = measured ? hint->measuredGpuNanoseconds : 0;
        plan.measuredPasses += measured ? 1u : 0u;
        plan.missingMeasurementPasses += measured ? 0u : 1u;
        plan.predictionComplete = plan.predictionComplete && measured;
        plan.predictedSerialNanoseconds = costs[pass] > UINT64_MAX - plan.predictedSerialNanoseconds
            ? UINT64_MAX : plan.predictedSerialNanoseconds + costs[pass];
        const bool declared = hint && hint->computeCompatible &&
            (!overlap || m_passes[pass].computeCompatible);
        const bool legal = !declared || IsComputeStateCompatible(pass);
        plan.rejectedStatePasses += declared && !legal ? 1u : 0u;
        if (allowCompute && declared && legal && measured &&
            hint->measuredGpuNanoseconds >= minimumGpuNanoseconds)
        {
            candidates.push_back(pass);
        }
    }
    plan.eligibleComputePasses = static_cast<uint32_t>(candidates.size());
    plan.predictionComplete = plan.predictionComplete && plan.predictedSerialNanoseconds != UINT64_MAX;
    // Partial sums are not a prediction. A measured empty scope is valid zero;
    // missing scopes never receive a synthetic positive cost to force placement.
    if (!plan.predictionComplete)
    {
        plan.predictedSerialNanoseconds = 0;
    }
    plan.predictedNanoseconds = plan.predictedSerialNanoseconds;
    if (allowCompute && overlap && !plan.predictionComplete)
    {
        plan.fallbackReason = RGQueueFallbackReason::MissingMeasurement;
    }
    else if (allowCompute && candidates.empty())
    {
        plan.fallbackReason = plan.rejectedStatePasses != 0 ? RGQueueFallbackReason::UnsupportedState :
            RGQueueFallbackReason::InsufficientGain;
    }
    // Leave the normal serial order and barriers alone on every early fallback.
    if (candidates.empty() || (overlap && !plan.predictionComplete))
    {
        for (const auto pass : std::span{executeOrder})
        {
            plan.entries.push_back({pass, RHIQueueKind::Graphics});
        }
        output = std::move(plan);
        return true;
    }
    std::vector<std::vector<uint16_t>> predecessors, successors;
    const std::vector<RHIQueueKind> serial(m_passes.size(), RHIQueueKind::Graphics);
    std::vector<RHIQueueKind> queues = serial;
    // Scratch is local to this planning invocation: reuse allocations between
    // trials without caching measurements, ownership epochs or another frame's DAG.
    QueuePlanningScratch scratch;
    BuildQueueDependencies(queues, scratch, predecessors, successors);
    std::vector<uint16_t> order = executeOrder;
    if (!overlap)
    {
        for (const auto pass : candidates)
        {
            queues[pass] = RHIQueueKind::Compute;
        }
        BuildQueueDependencies(queues, scratch, predecessors, successors);
        if (plan.predictionComplete)
        {
            plan.predictedNanoseconds = SimulateQueues(queues, costs, predecessors, successors, false, nullptr, scratch);
        }
    }
    // Missing samples and saturated arithmetic are not evidence of a benefit.
    // Retain the original graphics order until the entire executed graph is timed.
    else if (!candidates.empty() && plan.predictionComplete)
    {
        const bool reorder = m_orderPolicy == RGOrderPolicy::DependencyOrder;
        uint64_t best = plan.predictedSerialNanoseconds;
        std::vector<std::vector<uint16_t>> trialPredecessors, trialSuccessors;
        std::vector<RHIQueueKind> trial;
        const auto attempt = [&](std::span<const uint16_t> passes)
        {
            ++plan.placementTrials;
            trial = queues;
            for (const auto pass : passes)
            {
                trial[pass] = trial[pass] == RHIQueueKind::Compute ? RHIQueueKind::Graphics : RHIQueueKind::Compute;
            }
            BuildQueueDependencies(trial, scratch, trialPredecessors, trialSuccessors);
            const auto span = SimulateQueues(trial, costs, trialPredecessors, trialSuccessors, reorder, nullptr, scratch);
            if (span < best)
            {
                best = span;
                queues = trial;
            }
        };
        // A dependent chain of candidates (SSAO raw -> filter) only pays off as a
        // unit: moving one member alone adds two handoffs. Try chains first, then
        // single toggles to drop members that do not help.
        std::vector<uint16_t> component(m_passes.size(), RGPassId::kInvalid);
        ce::dynamic_bitset candidatePasses(m_passes.size(), false);
        for (const auto pass : candidates)
        {
            candidatePasses.set(pass, true);
        }
        for (const auto root : candidates)
        {
            if (component[root] != RGPassId::kInvalid)
            {
                continue;
            }
            std::vector<uint16_t> members{root}, stack{root};
            component[root] = root;
            while (!stack.empty())
            {
                const auto pass = stack.back();
                stack.pop_back();
                for (const auto* neighbours : {&predecessors[pass], &successors[pass]})
                {
                    for (const auto next : *neighbours)
                    {
                        if (candidatePasses.test(next) && component[next] == RGPassId::kInvalid)
                        {
                            component[next] = root;
                            members.push_back(next);
                            stack.push_back(next);
                        }
                    }
                }
            }
            attempt(members);
        }
        for (const auto pass : candidates)
        {
            attempt(std::span<const uint16_t>{&pass, 1});
        }
        if (plan.predictedSerialNanoseconds - best >= m_queueCostModel.minimumGainNanoseconds)
        {
            BuildQueueDependencies(queues, scratch, predecessors, successors);
            plan.predictedNanoseconds = SimulateQueues(queues, costs, predecessors, successors, reorder, &order, scratch);
        }
        else
        {
            queues = serial; // No predicted benefit: keep the single-queue plan.
            plan.fallbackReason = RGQueueFallbackReason::InsufficientGain;
        }
    }
    BuildQueueDependencies(queues, scratch, predecessors, successors);
    plan.dependencyMilliseconds = scratch.dependencyMilliseconds;
    plan.simulationMilliseconds = scratch.simulationMilliseconds;
    for (const auto pass : order)
    {
        plan.usesCompute = plan.usesCompute || queues[pass] == RHIQueueKind::Compute;
        plan.entries.push_back({pass, queues[pass]});
    }
    if (!plan.usesCompute)
    {
        output = std::move(plan);
        return true;
    }
    for (const auto pass : std::span{executeOrder})
    {
        for (const auto producer : predecessors[pass])
        {
            if (queues[producer] == queues[pass])
            {
                continue;
            }
            // Report resource hazards; non-resource recording fences still require
            // a GPU wait and use the explicit invalid-resource diagnostic sentinel.
            const auto waitBegin = plan.waits.size();
            for (const auto& usage : m_passes[pass].usages)
            {
                const auto resource = usage.handle.index;
                const bool shared = std::any_of(m_passes[producer].usages.begin(), m_passes[producer].usages.end(),
                    [&](const RGPassUsage& other) { return other.handle.index == resource; });
                if (shared && std::none_of(plan.waits.begin(), plan.waits.end(), [&](const auto& wait)
                    {
                        return wait.producer == producer && wait.consumer == pass && wait.resource == resource;
                    }))
                {
                    plan.waits.push_back({producer, pass, resource});
                }
            }
            if (plan.waits.size() == waitBegin)
            {
                plan.waits.push_back({producer, pass, RGHandle::kInvalid});
            }
        }
    }
    output = std::move(plan);
    return true;
}

bool EnhancedRenderGraph::Execute(std::string& outError)
{
    const auto& executeOrder = GetExecuteOrder();
    if (!m_queueLease.expired() || m_queueExecutionAttempted)
    {
        outError = "Queue execution requires completion collection and Reset before reuse";
        return false;
    }
    if (!m_compiled)
    {
        outError = "Compile을 먼저 불러야 한다";
        return false;
    }
    // 〃 (G-1). 호출부 30곳이 `resources.GetCommandList()` 를 넘기고 있었다.
    if (nullptr == m_deviceServices)
    {
        outError = "디바이스 서비스가 없다";
        return false;
    }

    // 프로파일러도 G-3에서 RHI 인코더 계약으로 내려갔다. 붙지 않은 그래프는
    // 계측만 생략하고 같은 실행 경로를 탄다.
    ExecuteContext context{};

    context.graph = this;

    for (uint16_t passIndex : std::span{executeOrder})
    {
        Pass& pass = m_passes[passIndex];

        // ★ 인코더를 서비스에서 받는다 (G-2a). 예전에는 `DX12Encoder` 를 여기서
        //   직접 만들었고, 그 한 줄이 그래프를 DX12 에 묶는 마지막 매듭이었다.
        //
        //   "패스마다 새 인코더"라는 계약은 그대로다 — `GetImmediateEncoder()`
        //   가 부를 때마다 상태 기억을 비운다(A-3 의 계약을 그렇게 좁혔다).
        //   기억이 패스 경계를 넘으면 안 되는 이유는 같다: 아직 안 옮긴
        //   코드가 같은 리스트에 원시로 루트를 걸면 기억이 낡는다.
        RHIEncoder& encoder = m_deviceServices->GetImmediateEncoder();
        context.encoder = &encoder;

        // 배리어를 측정 구간 안에 둔다. 배리어도 GPU 시간을 쓰고, 그 비용이
        // 어느 패스 때문에 생겼는지가 곧 그 패스의 비용이다.
        const uint32_t timerSlot = (nullptr != m_profiler)
            ? m_profiler->BeginPass(encoder, pass.name, TimingIdentity(passIndex))
            : IRHIGpuProfiler::kInvalidSlot;

        // 네 부류를 하나의 batch로 건다. 인코더가 감싼 command target에
        // 기록하므로 순차와 병렬 경로의 배리어 구현이 갈리지 않는다(G-2).
        // 분할 패스는 조각 하나로 부른다 — 통째로 기록하는 것과 같아야 한다는
        // 것이 계약이고, 순차 경로가 그 계약의 기준이 된다.
        try
        {
            RecordPassBarriers(encoder, pass);
            RecordPassBody(context, pass, 0, 1);
            RecordPassFinalBarriers(encoder, pass);
        }
        catch (const std::exception& exception)
        {
            if (nullptr != m_profiler)
            {
                m_profiler->EndPass(encoder, timerSlot);
            }
            outError = pass.name + ": " + exception.what();
            return false;
        }
        catch (...)
        {
            if (nullptr != m_profiler)
            {
                m_profiler->EndPass(encoder, timerSlot);
            }
            outError = pass.name + ": unknown recording failure";
            return false;
        }

        if (nullptr != m_profiler) m_profiler->EndPass(encoder, timerSlot);
    }

    m_recordedRecording = m_deviceServices->GetCurrentUploadRecordingId();
    return true;
}

void EnhancedRenderGraph::RecordPassBarriers(RHIEncoder& encoder, const Pass& pass) const
{
    if (pass.transitions.empty() && pass.bufferTransitions.empty() &&
        pass.uavBarriers.empty() && pass.uavBufferBarriers.empty() &&
        pass.aliasTextures.empty() && pass.aliasBuffers.empty())
    {
        return;
    }

    RHIBarrierBatch batch{};
    batch.textureTransitions = pass.transitions;
    batch.bufferTransitions = pass.bufferTransitions;
    batch.uavTextures = pass.uavBarriers;
    batch.uavBuffers = pass.uavBufferBarriers;
    batch.aliasTextures = pass.aliasTextures;
    batch.aliasBuffers = pass.aliasBuffers;
    encoder.ResourceBarriers(batch);
    for (const auto& [index, binding] : pass.aliasClears)
    {
        const auto& resource = m_resources[index];
        if (resource.desc.allowDepthStencil)
        {
            encoder.ClearDepthTarget(binding, m_poison ? 0.125f : 1.0f);
        }
        else
        {
            const auto description = TransientDescription(resource);
            encoder.ClearRenderTargets(binding, description.textureDesc.clearColor);
        }
    }
    for (const auto index : pass.poisonBuffers)
    {
        const auto& resource = m_resources[index];
        auto view = RHIBindingDesc::UavBuffer(resource.buffer,
            static_cast<uint32_t>(resource.bufferDesc.bytes / 4), 0);
        view.format = RHIFormat::R32Float;
        const float values[]{std::numeric_limits<float>::quiet_NaN(), 0, 0, 0};
        encoder.ClearUnorderedAccess(view, values);
        encoder.UavBarrierBuffers(std::span<const RHIBufferHandle>{&resource.buffer, 1});
    }
}

void EnhancedRenderGraph::RecordPassBody(const ExecuteContext& context, const Pass& pass,
    uint32_t slice, uint32_t sliceCount) const
{
    if (pass.repeated)
    {
        for (uint32_t iteration = 0; iteration < pass.repeatCount; ++iteration)
        {
            const auto& plans = iteration == 0 ? pass.firstPhaseBarriers : pass.repeatPhaseBarriers;
            for (uint32_t phase = 0; phase < pass.phases.size(); ++phase)
            {
                const auto& plan = plans[phase];
                RHIBarrierBatch barriers{};
                barriers.textureTransitions = plan.transitions;
                barriers.bufferTransitions = plan.bufferTransitions;
                barriers.uavTextures = plan.uavBarriers;
                barriers.uavBuffers = plan.uavBufferBarriers;
                if (!barriers.IsEmpty())
                {
                    context.encoder->ResourceBarriers(barriers);
                }
                pass.repeatedExecute(context, iteration, phase);
            }
        }
    }
    else if (pass.splitExecute)
    {
        pass.splitExecute(context, slice, sliceCount);
    }
    else if (pass.execute)
    {
        pass.execute(context);
    }
}

void EnhancedRenderGraph::RecordPassFinalBarriers(RHIEncoder& encoder, const Pass& pass) const
{
    if (pass.finalTransitions.empty() && pass.finalBufferTransitions.empty())
    {
        return;
    }
    RHIBarrierBatch batch{};
    batch.textureTransitions = pass.finalTransitions;
    batch.bufferTransitions = pass.finalBufferTransitions;
    encoder.ResourceBarriers(batch);
}

bool EnhancedRenderGraph::RecordParallel(IRHIParallelCommandPool& pool,
    uint32_t workerCount, const RHIRecordedBatchDesc& batchDesc,
    RHIRecordedBatch& outBatch, std::string& outError)
{
    const auto& executeOrder = GetExecuteOrder();
    if (!m_queueLease.expired() || m_queueExecutionAttempted)
    {
        outError = "Queue execution requires completion collection and Reset before reuse";
        return false;
    }
    if (outBatch.IsValid() ||
        (m_preparedPool && (m_preparedPool != &pool || m_preparedRecordingConsumed || m_preparedRecording == 0 ||
                            m_preparedRecording != m_deviceServices->GetCurrentUploadRecordingId() ||
                            m_preparedDescriptors != m_deviceServices->GetDescriptorVersionToken())))
    {
        outError = "Parallel recording has a nonempty result or stale/already consumed preparation.";
        return false;
    }
    if (!m_compiled)
    {
        outError = "Compile을 먼저 불러야 한다";
        return false;
    }
    if (!pool.IsInitialized())
    {
        outError = "병렬 커맨드 풀이 초기화되지 않았다";
        return false;
    }

    if (thread_pool::is_worker_thread())
    {
        outError = "공용 워커에서 동기 명령 기록을 중첩할 수 없다";
        return false;
    }

    const size_t passCount = executeOrder.size();
    if (0 == passCount)
    {
        if (!pool.FinalizeRecordedBatch({}, batchDesc, outBatch, outError))
        {
            return false;
        }
        m_recordedRecording = m_deviceServices->GetCurrentUploadRecordingId();
        return true;
    }

    // ── 기록 단위 만들기 ──
    //
    // 단위는 패스가 아니라 '조각'이다. 쪼갤 수 있는 패스는 여러 조각이 되고,
    // 그렇지 않은 패스는 조각 하나다.
    //
    // 왜 필요한가: 패스 단위 병렬화는 '가장 무거운 패스'보다 빨라질 수 없다.
    // 실측에서 워커를 늘려도 1.25배에서 평평했는데, GBuffer·Shadow가 기록
    // 시간의 대부분을 차지하고 나머지 패스는 거의 비어 있었기 때문이다.
    struct RecordUnit
    {
        size_t   order{ 0 };        // 실행 순서에서의 위치
        uint32_t slice{ 0 };
        uint32_t sliceCount{ 1 };
    };

    // ── 병렬로 갈 만한가 ──
    //
    // 인스턴싱을 넣고 나서 이 판단이 필요해졌다. 기록량이 줄면서 임계값이
    // 옮겨 갔기 때문이다 — 드로우 44에서 0.69배, 176에서 0.92배로 병렬 쪽이
    // 지는 구간이 생겼다. 워커를 깨우고 리스트를 여러 개 제출하는 비용이
    // 기록 자체보다 커지는 지점이 있고, 그 아래에서는 순차가 맞다.
    //
    // 임계값은 실측으로 정한다. 여기서 미리 정해 두고 '대충 이쯤이면 되겠지'로
    // 넘어가면, 다음에 기록 비용이 또 바뀌었을 때 아무도 다시 재지 않는다.
    uint32_t totalRecordCost = 0;
    uint64_t estimatedRecordingWork = 0;
    const auto bindingWork = [](const Pass& pass)
    {
        uint64_t uses = pass.usages.size();
        for (const auto& phase : std::span{pass.phases})
        {
            uses += uint64_t(phase.usages.size()) * pass.repeatCount;
        }
        return uses;
    };
    for (const size_t index : std::span{executeOrder})
    {
        totalRecordCost += m_passes[index].recordCost;
        estimatedRecordingWork += 1 + uint64_t(m_passes[index].recordCost) + bindingWork(m_passes[index]);
    }

    // 호출부가 병렬을 요청했더라도 그래프가 되무른다 — 이 판단을 호출부마다
    // 복제하면 한 곳이 빠졌을 때 조용히 느려진다.
    //
    // 되무르는 방법은 별도 경로가 아니라 '워커 1·조각 1'이다. 조각까지 1로
    // 되돌리는 것이 중요하다 — 워커만 1로 두면 한 워커가 조각들을 연달아
    // 맡으면서 조각마다 상태를 다시 거는 비용이 그대로 남는다. 조각 1은
    // 통째로 기록한 것과 같다는 것이 분할 조각의 계약이다.
    const bool recordingSideEffects = std::any_of(executeOrder.begin(), executeOrder.end(),
        [&](uint16_t pass) { return m_passes[pass].recordingSideEffect; });
    const bool bindingAware = m_recordingWorkloadBalance && m_scheduling == RGSchedulingMode::ExplicitVersioned;
    // 동일 바이너리의 진단 비교용이다. 미지정은 의존 작업 그래프를 사용한다.
    static const bool kDependencyRecordingEnabled = []
    {
        char* value = nullptr;
        size_t length = 0;
        const bool read = _dupenv_s(&value, &length, "CREATOR_RECORDING_TASK_GRAPH") == 0;
        const bool enabled = !read || !value || std::string_view{value} != "0";
        std::free(value);
        return enabled;
    }();
    // 동일 바이너리에서 조각·target 하나의 기록 비용을 병렬 정책과 비교한다.
    static const bool kSingleTargetRecording = []
    {
        char* value = nullptr;
        size_t length = 0;
        const bool read = _dupenv_s(&value, &length, "CREATOR_RECORDING_SINGLE_TARGET") == 0;
        const bool enabled = read && value && std::string_view{value} == "1";
        std::free(value);
        return enabled;
    }();
    const bool declineParallel = recordingSideEffects || kSingleTargetRecording ||
        (0 != totalRecordCost && totalRecordCost < m_parallelCostThreshold &&
            (!bindingAware || estimatedRecordingWork < m_parallelCostThreshold));

    m_stats.totalRecordCost = totalRecordCost;
    m_stats.estimatedRecordingWork = estimatedRecordingWork;
    m_stats.parallelDeclined = declineParallel;

    const uint32_t poolWorkers = declineParallel ? 1u : (std::max)(1u,
        (std::min)(workerCount, pool.GetWorkerCount()));

    std::vector<RecordUnit> units;
    units.reserve(passCount * 2);

    for (size_t order = 0; order < passCount; ++order)
    {
        const Pass& pass = m_passes[executeOrder[order]];

        // 조각 수는 워커 수를 넘겨 봐야 소용이 없다 — 어차피 같은 워커가
        // 여러 조각을 연달아 맡게 되고, 그러면 조각마다 상태를 다시 거는
        // 비용만 늘어난다.
        const uint32_t slices = (nullptr != pass.splitExecute && !declineParallel)
            ? (std::max)(1u, (std::min)(pass.maxSlices, poolWorkers))
            : 1u;

        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            units.push_back(RecordUnit{ order, slice, slices });
        }
    }

    const size_t unitCount = units.size();
    const uint32_t workers = (std::max)(1u,
        (std::min)(poolWorkers, static_cast<uint32_t>(unitCount)));

    // backend가 immediate upload/copy를 먼저 제출하고 worker recording용
    // allocation version을 연다. 호출부가 Flush를 빼먹을 수 없게 경계 안에 둔다.
    {
        ce::profile_scope profile{ce::marker<"RenderCommandRecord.Prepare">()};
        if (m_preparedPool)
        {
            m_preparedRecordingConsumed = true;
        }
        else if (!pool.Prepare(outError))
        {
            return false;
        }
    }

    // ── 배분: 연속 블록 ──
    //
    // 라운드로빈으로 흩으면 안 된다. 워커가 A,B,A 순으로 돌아오면 A의 리스트를
    // 두 번 제출해야 하는데, 그러면 A가 기록한 커맨드 전체가 두 번 실행된다.
    // 리스트는 통째로 실행되는 단위라 그 안의 일부만 제출할 수 없다.
    //
    // 연속 블록이면 워커 순서 = 선언 순서가 되고, 리스트도 한 번씩만 제출된다.
    //
    // ★ 워커를 단위 수보다 적게 주면 무거운 것들이 한 워커에 몰린다. 실측으로
    // 그 대가를 확인했다 — 패스 6 · 워커 4에서 Shadow와 GBuffer가 같은 워커에
    // 가면서 드로우 44에서 1.12배, 176에서 0.81배로 방향조차 흔들렸다.
    // 워커를 넉넉히 주면(아래에서 단위 수로 클램프한다) 일관된다.
    struct RecordingShape
    {
        uint16_t pass;
        uint32_t slice, slices, declared;
        uint64_t bindings;
        bool independent, split;
        bool operator==(const RecordingShape&) const = default;
    };
    struct CachedRecordingPlan
    {
        std::shared_ptr<const StructuralSchedule> schedule;
        uint32_t workers{0}, waveCount{1};
        bool balanced{false};
        std::vector<RecordingShape> shapes;
        std::vector<uint32_t> unitWorker, unitWave;
        std::vector<std::vector<size_t>> workerUnits;
        RHIRecordingTaskGraph taskGraph;
    };
    // CPU metadata only: no callbacks, encoders, native handles or frame owners.
    // A bounded owner-thread cache also serves graph storage in other frame slots.
    // Workers cannot recursively record, so an active entry cannot be evicted by
    // its callbacks. Exact immutable schedule identity and unit shapes gate reuse.
    static thread_local std::array<std::optional<CachedRecordingPlan>, MaxStructuralSchedules * 2> cachedPlans;
    static thread_local size_t nextCachedPlan = 0;
    const bool cacheEligible = bindingAware && !recordingSideEffects && workers > 1 &&
        m_activeStructuralSchedule && unitCount <= 4096;
    const auto shape = [&](size_t i)
    {
        const auto pass = executeOrder[units[i].order];
        const auto& value = m_passes[pass];
        return RecordingShape{pass, units[i].slice, units[i].sliceCount, value.recordCost,
            bindingWork(value), value.recordingIndependent, bool(value.splitExecute)};
    };
    CachedRecordingPlan scratch;
    const CachedRecordingPlan* selectedPlan = nullptr;
    if (cacheEligible)
    {
        for (const auto& entry : cachedPlans)
        {
            if (!entry || entry->schedule != m_activeStructuralSchedule || entry->workers != workers ||
                entry->shapes.size() != unitCount)
            {
                continue;
            }
            bool matches = true;
            for (size_t i = 0; i < unitCount; ++i)
            {
                if (entry->shapes[i] != shape(i))
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
            {
                selectedPlan = &*entry;
                break;
            }
        }
    }
    if (!selectedPlan)
    {
        ce::profile_scope profile{ce::marker<"RenderCommandRecord.Plan">()};
        std::vector<RHIRecordingPassDependencies> recordingPasses(m_passes.size());
        for (size_t pass = 0; pass < m_passes.size(); ++pass)
        {
            recordingPasses[pass].independent = m_passes[pass].recordingIndependent;
        }
        for (const auto& edge : std::span{VersionEdges()})
        {
            recordingPasses[edge.consumer].producers.push_back(edge.producer);
        }
        std::vector<uint32_t> unitPasses;
        unitPasses.reserve(unitCount);
        for (const auto& unit : units)
        {
            unitPasses.push_back(executeOrder[unit.order]);
        }
        auto& unitWorker = scratch.unitWorker;
        unitWorker.assign(unitCount, 0);
        for (size_t i = 0; i < unitCount; ++i)
        {
            unitWorker[i] = static_cast<uint32_t>(i * workers / unitCount);
        }
        auto& unitWave = scratch.unitWave;
        unitWave.assign(unitCount, 0);
        uint32_t waveCount = 1;
        m_stats.recordingBalanced = false;
        if (m_scheduling == RGSchedulingMode::ExplicitVersioned && workers > 1)
        {
            std::vector<uint32_t> passWave(m_passes.size()), targetWave(workers);
            std::vector<uint32_t> passCompletionTarget(m_passes.size());
            const auto planWaves = [&](const std::vector<uint32_t>& targets, std::vector<uint32_t>& waves)
            {
                std::ranges::fill(std::span{passWave}, 0u);
                std::ranges::fill(std::span{targetWave}, 0u);
                std::ranges::fill(std::span{passCompletionTarget}, UINT32_MAX);
                uint32_t count = 1;
                for (size_t begin = 0; begin < unitCount;)
                {
                    size_t end = begin + 1;
                    while (end < unitCount && units[end].order == units[begin].order)
                    {
                        ++end;
                    }
                    const auto pass = executeOrder[units[begin].order];
                    uint32_t wave = 0;
                    // 배분은 연속 블록이다. 여러 조각도 한 리스트에 모였으면 앞선
                    // 콜백이 같은 워커에서 먼저 끝나므로 별도 합류가 필요 없다.
                    const uint32_t singleTarget = targets[begin] == targets[end - 1] ? targets[begin] : UINT32_MAX;
                    for (const auto producer : recordingPasses[pass].producers)
                    {
                        // 분할 가능 여부와 CPU 독립성은 다른 계약이다. 작성자가 모든
                        // 조각의 독립성을 보장할 때만 GPU 의존성의 추가 CPU 합류를 뺀다.
                        const uint32_t join = m_passes[pass].recordingIndependent ||
                            (singleTarget != UINT32_MAX && passCompletionTarget[producer] == singleTarget) ? 0u : 1u;
                        wave = (std::max)(wave, passWave[producer] + join);
                    }
                    for (size_t i = begin; i < end; ++i)
                    {
                        wave = (std::max)(wave, targetWave[targets[i]]);
                    }
                    // 독립 콜백을 사이에 둔 선행 CPU 작업도 보존한다. 같은 차례의
                    // 선행 패스가 다른 target에 남아 있으면, 뒤의 미선언 소비자는
                    // 자기 직전 콜백과 같은 target이어도 전체 합류가 필요하다.
                    passCompletionTarget[pass] = singleTarget;
                    for (const auto producer : recordingPasses[pass].producers)
                    {
                        if (passWave[producer] == wave && passCompletionTarget[producer] != singleTarget)
                        {
                            passCompletionTarget[pass] = UINT32_MAX;
                            break;
                        }
                    }
                    for (size_t i = begin; i < end; ++i)
                    {
                        waves[i] = wave;
                        targetWave[targets[i]] = wave;
                    }
                    passWave[pass] = wave;
                    count = (std::max)(count, wave + 1);
                    begin = end;
                }
                return count;
            };
            waveCount = planWaves(unitWorker, unitWave);
            if (m_recordingWorkloadBalance && unitCount > workers && workers <= IRHIParallelCommandPool::kMaxWorkers)
            {
                // These are CPU command-recording work units, never GPU timings.
                // Base callback + declared resource uses + declared draw-batch cost.
                // Split callbacks share the declared work; every slice has overhead.
                std::vector<uint64_t> costs(unitCount);
                uint64_t total = 0;
                for (size_t i = 0; i < unitCount; ++i)
                {
                    const auto index = executeOrder[units[i].order];
                    const auto& pass = m_passes[index];
                    const uint64_t declared = uint64_t(pass.recordCost) / units[i].sliceCount;
                    // Physical history-buffer states alternate between frames. Using
                    // emitted barrier counts would move CPU partition boundaries and
                    // make capture join counts nondeterministic for identical inputs.
                    const uint64_t bindings = units[i].slice == 0 ? bindingWork(pass) : 0;
                    costs[i] = (std::min)(uint64_t(UINT32_MAX), 1 + declared + bindings);
                    total += costs[i];
                }
                // Contiguous nonempty blocks: target order remains GPU execute order.
                std::vector<uint32_t> candidate(unitCount);
                uint64_t prefix = 0;
                uint32_t target = 0;
                for (size_t i = 0; i < unitCount; ++i)
                {
                    const uint32_t desired = static_cast<uint32_t>((std::min)(uint64_t(workers - 1), prefix * workers / total));
                    if (i && target + 1 < workers &&
                        (desired > target || unitCount - i <= workers - 1 - target))
                    {
                        ++target;
                    }
                    candidate[i] = target;
                    prefix += costs[i];
                }
                // Minimize the wave critical path, not the largest lifetime sum
                // for a target: separate target blocks may still execute serially.
                const uint32_t maximumWaves = waveCount;
                const auto criticalWork =
                    [&](const std::vector<uint32_t>& targets, const std::vector<uint32_t>& waves, uint32_t count)
                {
                    RHIRecordingTaskGraph graph;
                    if (kDependencyRecordingEnabled &&
                        RHIBuildRecordingTaskGraph(targets, waves, unitPasses, costs, recordingPasses, workers,
                                                   graph) &&
                        graph.ReplacesWaveJoins())
                    {
                        return graph.CriticalWork();
                    }
                    std::vector<uint64_t> sums(size_t(count) * workers, 0);
                    for (size_t i = 0; i < unitCount; ++i)
                    {
                        sums[size_t(waves[i]) * workers + targets[i]] += costs[i];
                    }
                    uint64_t result = 0;
                    for (uint32_t wave = 0; wave < count; ++wave)
                    {
                        uint64_t longest = 0;
                        for (uint32_t worker = 0; worker < workers; ++worker)
                        {
                            longest = (std::max)(longest, sums[size_t(wave) * workers + worker]);
                        }
                        result += longest;
                    }
                    return result;
                };
                uint64_t best = criticalWork(unitWorker, unitWave, waveCount);
                const auto consider = [&](const std::vector<uint32_t>& targets)
                {
                    std::vector<uint32_t> waves(unitCount);
                    const uint32_t count = planWaves(targets, waves);
                    if (count > maximumWaves)
                    {
                        return;
                    }
                    const uint64_t work = criticalWork(targets, waves, count);
                    if (work < best || (work == best && count < waveCount))
                    {
                        unitWorker = targets;
                        unitWave = std::move(waves);
                        waveCount = count;
                        best = work;
                        m_stats.recordingBalanced = true;
                    }
                };
                consider(candidate);
                // Bounded cold-plan search. Boundaries remain contiguous/nonempty;
                // every trial retains the same CPU dependencies and GPU list order.
                if (cacheEligible && unitCount <= 512)
                {
                    uint32_t trials = 0;
                    for (unsigned iteration = 0; iteration < 2; ++iteration)
                    {
                        for (uint32_t boundary = 1; boundary < workers && trials < 64; ++boundary)
                        {
                            size_t left = 0;
                            while (left < unitCount && unitWorker[left] < boundary - 1)
                            {
                                ++left;
                            }
                            size_t right = left;
                            while (right < unitCount && unitWorker[right] <= boundary)
                            {
                                ++right;
                            }
                            for (size_t step = 1; step < 9 && trials < 64; ++step)
                            {
                                const size_t position = left + (right - left) * step / 9;
                                if (position <= left || position >= right)
                                {
                                    continue;
                                }
                                auto trial = unitWorker;
                                for (size_t i = left; i < right; ++i)
                                {
                                    trial[i] = i < position ? boundary - 1 : boundary;
                                }
                                ++trials;
                                consider(trial);
                            }
                        }
                    }
                }
            }
        }
        auto& workerUnits = scratch.workerUnits;
        workerUnits.resize(workers);
        for (size_t i = 0; i < unitCount; ++i)
        {
            workerUnits[unitWorker[i]].push_back(i);
        }
        scratch.waveCount = waveCount;
        if (!recordingSideEffects && m_scheduling == RGSchedulingMode::ExplicitVersioned && waveCount > 1 &&
            !RHIBuildRecordingTaskGraph(unitWorker, unitWave, unitPasses, {}, recordingPasses, workers,
                                        scratch.taskGraph))
        {
            outError = "명령 기록 CPU 의존 그래프를 구성할 수 없다";
            return false;
        }
        scratch.balanced = m_stats.recordingBalanced;
        scratch.workers = workers;
        if (cacheEligible)
        {
            scratch.schedule = m_activeStructuralSchedule;
            scratch.shapes.reserve(unitCount);
            for (size_t i = 0; i < unitCount; ++i)
            {
                scratch.shapes.push_back(shape(i));
            }
            auto& entry = cachedPlans[nextCachedPlan++ % cachedPlans.size()];
            entry.emplace(std::move(scratch));
            selectedPlan = &*entry;
        }
        else
        {
            selectedPlan = &scratch;
        }
        ce::profile_instant(ce::marker<"RenderCommandRecord.PlanBuild">());
    }
    else
    {
        ce::profile_instant(ce::marker<"RenderCommandRecord.PlanReuse">());
    }
    const auto& unitWorker = selectedPlan->unitWorker;
    const auto& unitWave = selectedPlan->unitWave;
    const auto& workerUnits = selectedPlan->workerUnits;
    const uint32_t waveCount = selectedPlan->waveCount;
    m_stats.recordingBalanced = selectedPlan->balanced;
    m_stats.recordingWaveCount = waveCount;
    m_stats.recordingJobDispatchCount = 0;
    m_stats.recordingOwnerJoinCount = 0;
    m_stats.recordingWorkerJobCount = 0;
    std::vector<size_t> workerCursors(workers, 0);
    uint32_t activeWave = 0;

    const auto discardRecording = [&]
    {
        std::string closeError;
        if (!pool.CloseAll(closeError)) outError += " / 기록 폐기 중 닫기 실패: " + closeError;
    };

    // 리스트를 먼저 전부 연다.
    //
    // Open은 얼로케이터를 Reset하므로 스레드 안전하지 않다. 기록에 들어가기
    // 전에 한 스레드에서 끝내 두면 워커는 '이미 열린 리스트에 기록'만 한다.
    {
        ce::profile_scope profile{ce::marker<"RenderCommandRecord.Open">()};
        for (uint32_t worker = 0; worker < workers; ++worker)
        {
            if (!pool.OpenWorker(worker, outError))
            {
                discardRecording();
                return false;
            }
        }
    }

    // 워커에서 터진 예외를 삼키지 않는다. 조용히 사라지면 '가끔 화면이 빈다'가
    // 되고, 그 상태는 원인을 찾기가 매우 어렵다.
    std::vector<std::string> workerErrors(workers);
    std::atomic<bool> failed{ false };
#if !CE_SHIPPING
    const auto recordingContext = ce::current_cpu_context;
#endif
    static const std::array workerMarkers{
        ce::marker<"RenderCommandRecord.Worker0">(), ce::marker<"RenderCommandRecord.Worker1">(),
        ce::marker<"RenderCommandRecord.Worker2">(), ce::marker<"RenderCommandRecord.Worker3">(),
        ce::marker<"RenderCommandRecord.Worker4">(), ce::marker<"RenderCommandRecord.Worker5">(),
        ce::marker<"RenderCommandRecord.Worker6">(), ce::marker<"RenderCommandRecord.Worker7">()};

    const auto recordRange = [&](uint32_t worker, uint32_t recordingWave)
    {
#if !CE_SHIPPING
        ce::profile_context_scope contextScope{recordingContext};
#endif
        const auto workerMarker = worker < workerMarkers.size() ? workerMarkers[worker] :
            ce::marker<"RenderCommandRecord.WorkerOther">();
        ce::profile_scope profile{workerMarker};
        ExecuteContext context{};
        context.graph = this;

        try
        {
            auto& cursor = workerCursors[worker];
            const auto& indices = workerUnits[worker];
            while (cursor < indices.size())
            {
                const size_t i = indices[cursor];
                // Waves are nondecreasing for each worker (targetWave above).
                if (unitWave[i] != recordingWave)
                {
                    break;
                }
                ++cursor;

                const RecordUnit& unit = units[i];
                Pass& pass = m_passes[executeOrder[unit.order]];


                // 순차 경로와 같은 이유로 조각마다 새로 만든다(Execute 주석 참고).
                // 워커마다 자기 커맨드 리스트라 조각끼리도 섞이지 않아야 한다.
                RHIEncoder& encoder = pool.AcquireEncoder(worker);
                context.encoder = &encoder;

                // 패스별 GPU 시간을 병렬 경로에서도 잰다.
                //
                // 질의 힙은 하나여도 된다 — 인덱스가 겹치지 않으면 여러 커맨드
                // 리스트가 같은 힙에 써도 되고, 슬롯 예약이 원자적이라 겹치지
                // 않는다. 워커마다 힙을 나눌 필요는 없었다.
                //
                // 분할 패스는 조각마다 구간을 찍는다. 수집 때 이름으로 묶어
                // 처음 시작~마지막 끝으로 합친다 — GPU 타임라인은 하나이고
                // 조각들은 순서대로 실행되므로 그 구간이 곧 패스 전체 시간이다.
                const uint32_t timerSlot = (nullptr != m_profiler)
                    ? m_profiler->BeginPass(encoder, pass.name, TimingIdentity(executeOrder[unit.order]))
                    : IRHIGpuProfiler::kInvalidSlot;

                // 배리어는 패스의 첫 조각에만 넣는다. 조각들은 순서대로
                // 실행되므로 앞에 한 번이면 뒤 조각까지 덮는다.
                if (0 == unit.slice)
                {
                    RecordPassBarriers(encoder, pass);
                }

                RecordPassBody(context, pass, unit.slice, unit.sliceCount);

                if (unit.slice + 1 == unit.sliceCount)
                {
                    RecordPassFinalBarriers(encoder, pass);
                }

                if (nullptr != m_profiler)
                {
                    m_profiler->EndPass(encoder, timerSlot);
                }
            }
        }
        catch (const std::exception& e)
        {
            workerErrors[worker] = e.what();
            failed.store(true, std::memory_order_relaxed);
        }
        catch (...)
        {
            workerErrors[worker] = "알 수 없는 예외";
            failed.store(true, std::memory_order_relaxed);
        }
    };

    // 기록 구간당 Job 하나를 엔진 공용 워커에 제출한다. 단일 구간도 같은 경로다.
    // RunParallel은 모든 콜백이 끝난 뒤 반환/throw하므로 아래에서 안전하게 닫는다.
    try
    {
        ce::profile_scope profile{ce::marker<"RenderCommandRecord.DispatchJoin">()};
        if (recordingSideEffects)
        {
            // The entire graph is one unsplit target in this uncommon path.
            // Keep explicitly declared external CPU effects on the owner thread.
            recordRange(0, activeWave);
        }
        else if (kDependencyRecordingEnabled && selectedPlan->taskGraph.ReplacesWaveJoins())
        {
            // 다음 작업을 미리 제출한다. 같은 target과 실제 CPU 선행 작업만 기다린다.
            ce::profile_scope dependencyProfile{ce::marker<"RenderCommandRecord.DependencyGraph">()};
            const auto& graph = selectedPlan->taskGraph;
            m_stats.recordingJobDispatchCount = static_cast<uint32_t>(graph.nodes.size());
            m_stats.recordingOwnerJoinCount = 1;
            m_stats.recordingWorkerJobCount = static_cast<uint32_t>(graph.nodes.size());
            pool.RunTaskGraph(graph,
                              [&](uint32_t node)
                              {
                                  const auto& task = graph.nodes[node];
                                  if (!failed.load(std::memory_order_relaxed))
                                  {
                                      recordRange(task.target, task.wave);
                                  }
                              });
        }
        else
        {
            // 의존이 줄줄이 이어진 그래프는 차례가 거의 패스 수만큼 생긴다(37 패스에
            // 33 차례). 차례마다 워커를 깨워 합류시키면 그 비용만 프레임당 2 ms 였다
            // (기록 0.33 → 2.45 ms, 초당 994 → 315장). 워커 하나만 일하는 차례가
            // 연달아 오면 그 묶음을 Job 하나가 차례 순서대로 기록한다 — 합류 조건과
            // 리스트 순서 제약이 그대로이고, 기록은 여전히 공용 워커에서만 일어난다.
            constexpr uint32_t kMaskedWorkers = 32;
            std::vector<uint32_t> waveWorkerMask(waveCount, 0);
            for (size_t i = 0; i < unitCount && workers <= kMaskedWorkers; ++i)
            {
                waveWorkerMask[unitWave[i]] |= 1u << unitWorker[i];
            }
            const auto isSingleWorkerWave = [&](uint32_t wave)
            {
                const uint32_t mask = waveWorkerMask[wave];
                return workers <= kMaskedWorkers && 0 == (mask & (mask - 1));
            };
            for (uint32_t wave = 0; wave < waveCount && !failed.load(std::memory_order_relaxed);)
            {
                if (!isSingleWorkerWave(wave))
                {
                    activeWave = wave++;
                    // Dispatch only targets with units in this wave. The owner
                    // still joins every submitted job before advancing the wave.
                    std::vector<uint32_t> active;
                    for (uint32_t worker = 0; worker < workers; ++worker)
                    {
                        const auto cursor = workerCursors[worker];
                        if (cursor < workerUnits[worker].size() &&
                            unitWave[workerUnits[worker][cursor]] == activeWave)
                            active.push_back(worker);
                    }
                    m_stats.recordingJobDispatchCount += 1;
                    m_stats.recordingOwnerJoinCount += 1;
                    m_stats.recordingWorkerJobCount += static_cast<uint32_t>(active.size());
                    pool.RunParallel([&](uint32_t job) { recordRange(active[job], activeWave); },
                                     static_cast<uint32_t>(active.size()));
                    continue;
                }
                const uint32_t runBegin = wave;
                while (wave < waveCount && isSingleWorkerWave(wave))
                {
                    ++wave;
                }
                const uint32_t runEnd = wave;
                ++m_stats.recordingJobDispatchCount;
                ++m_stats.recordingOwnerJoinCount;
                ++m_stats.recordingWorkerJobCount;
                pool.RunParallel([&](uint32_t)
                {
                    for (uint32_t run = runBegin; run < runEnd && !failed.load(std::memory_order_relaxed); ++run)
                    {
                        activeWave = run;
                        const uint32_t mask = waveWorkerMask[run];
                        for (uint32_t worker = 0; worker < workers; ++worker)
                        {
                            if (0 != (mask & (1u << worker)))
                            {
                                recordRange(worker, activeWave);
                            }
                        }
                    }
                }, 1);
            }
        }
    }
    catch (const std::exception& e)
    {
        outError = "명령 기록 Job 실패: " + std::string(e.what());
        discardRecording();
        return false;
    }
    catch (...)
    {
        outError = "명령 기록 Job 실패: 알 수 없는 예외";
        discardRecording();
        return false;
    }

    if (failed.load(std::memory_order_relaxed))
    {
        for (uint32_t worker = 0; worker < workers; ++worker)
        {
            if (!workerErrors[worker].empty())
            {
                outError = "워커 " + std::to_string(worker) + " 기록 실패: "
                    + workerErrors[worker];
                discardRecording();
                return false;
            }
        }
        outError = "워커 기록 실패(사유 미상)";
        discardRecording();
        return false;
    }

    // batch 순서는 워커 번호 순서다. 연속 블록 배분이므로 그것이 곧 선언 순서다.
    std::vector<uint32_t> submission;
    submission.reserve(workers);
    for (uint32_t worker = 0; worker < workers; ++worker)
    {
        if (!pool.HasRecorded(worker)) continue;
        submission.push_back(worker);
    }

    // 3-15A의 경계: 여기서는 native queue를 건드리지 않는다. 기록 당시 frame
    // slot과 순서를 batch 값에 밀봉하고 제출은 호출부(향후 RHI thread)가 한다.
    {
        ce::profile_scope profile{ce::marker<"RenderCommandRecord.Finalize">()};
        if (!pool.FinalizeRecordedBatch(submission, batchDesc, outBatch, outError))
        {
            return false;
        }
    }
    // Batch recording IDs are assigned later, when admission reserves completion.
    m_recordedRecording = m_deviceServices->GetCurrentUploadRecordingId();

    m_stats.recordWorkers = workers;
    m_stats.recordedLists = static_cast<uint32_t>(submission.size());
    m_stats.recordUnits = static_cast<uint32_t>(unitCount);
    m_stats.recordingWaveCount=waveCount;
    return true;
}

bool EnhancedRenderGraph::IsPassCulled(RGPassId pass) const
{
    if (!pass.IsValid() || pass.index >= m_passes.size()) return true;
    return m_passes[pass.index].culled;
}

uint32_t EnhancedRenderGraph::GetPassBarrierCount(RGPassId pass) const
{
    if (!pass.IsValid() || pass.index >= m_passes.size())
    {
        return 0;
    }
    const Pass& planned = m_passes[pass.index];
    uint64_t count = planned.transitions.size() +
        planned.bufferTransitions.size() + planned.uavBarriers.size() +
        planned.uavBufferBarriers.size() + planned.finalTransitions.size() + planned.finalBufferTransitions.size() +
        planned.aliasTextures.size() + planned.aliasBuffers.size() + planned.poisonBuffers.size();
    const auto phaseCount = [](const std::vector<PhaseBarrierPlan>& plans)
    {
        uint64_t total = 0;
        for (const auto& phase : plans)
        {
            total += phase.transitions.size() + phase.bufferTransitions.size() +
                phase.uavBarriers.size() + phase.uavBufferBarriers.size();
        }
        return total;
    };
    count += phaseCount(planned.firstPhaseBarriers);
    if (planned.repeatCount > 1)
    {
        count += uint64_t(planned.repeatCount - 1) * phaseCount(planned.repeatPhaseBarriers);
    }
    // Compile bounds total emitted accesses before creating these barrier plans.
    return static_cast<uint32_t>(count);
}

bool EnhancedRenderGraph::GetTransientLifetime(RGHandle handle,
    uint32_t& outFirst, uint32_t& outLast) const
{
    if (!handle.IsValid() || handle.index >= m_resources.size()) return false;
    if(m_scheduling==RGSchedulingMode::ExplicitVersioned && !ValidVersionHandle(handle,true)) return false;

    const Resource& resource = m_resources[handle.index];
    if (!resource.used) return false;

    outFirst = resource.firstUse;
    outLast = resource.lastUse;
    return true;
}
