#include "EnhancedRenderGraph.h"
#include "../../RHI/IRenderDeviceServices.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

#include <algorithm>
#include <atomic>
#include <queue>

namespace
{
    // An address may be reused while a packet still retains an old graph output.
    // Epochs must distinguish both Reset and reconstruction at the same address.
    uint64_t NextResourceEpoch()
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
            : usage.access == RGAccessMode::Write || usage.access == RGAccessMode::ReadWrite;
    }
    bool Reads(const EnhancedRenderGraph::RGPassUsage& usage)
    {
        return usage.access == RGAccessMode::LegacyState ? !IsWriteState(usage.state)
            : usage.access == RGAccessMode::Read || usage.access == RGAccessMode::ReadWrite;
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
    ReleaseResources();
    m_resources.clear();
    m_passes.clear();
    m_executeOrder.clear();
    m_versionEdges.clear();
    m_compiled = false;
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
    if (buffer.IsValid())
    {
        for (size_t i = 0; i < m_resources.size(); ++i)
        {
            if (m_resources[i].imported && m_resources[i].buffer == buffer)
            {
                return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(static_cast<uint16_t>(i), 0) : RGHandle{static_cast<uint16_t>(i)};
            }
        }
    }
    return {};
}

RGHandle EnhancedRenderGraph::FindImportedTexture(RHITextureHandle texture) const
{
    if (texture.IsValid())
    {
        for (size_t i = 0; i < m_resources.size(); ++i)
        {
            if (m_resources[i].imported && m_resources[i].handle == texture)
            {
                return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(static_cast<uint16_t>(i), 0) : RGHandle{static_cast<uint16_t>(i)};
            }
        }
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

RGHandle EnhancedRenderGraph::ImportBuffer(RHIBufferHandle resource,
    RHIResourceState currentState, const std::string& name,
    RHIResourceState* stateWriteback)
{
    RGHandle handle{};
    if (!resource.IsValid()) return handle;

    Resource entry{};
    entry.buffer = resource;
    entry.state = currentState;
    entry.writeback = stateWriteback;
    entry.imported = true;
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned) entry.versions.push_back({RGHandle::kInvalid, false});
    entry.name = name;

    handle.index = static_cast<uint16_t>(m_resources.size());
    m_resources.push_back(std::move(entry));
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(handle.index,
        m_resources[handle.index].imported ? 0 : RGHandle::kInvalid) : handle;
}

RGHandle EnhancedRenderGraph::ImportTexture(RHITextureHandle resource,
    RHIResourceState currentState, const std::string& name,
    RHIResourceState* stateWriteback)
{
    RGHandle handle{};
    if (!resource.IsValid()) return handle;

    Resource entry{};
    entry.handle = resource;
    entry.state = currentState;
    entry.writeback = stateWriteback;
    entry.imported = true;
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned) entry.versions.push_back({RGHandle::kInvalid, false});
    entry.name = name;

    handle.index = static_cast<uint16_t>(m_resources.size());
    m_resources.push_back(std::move(entry));
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(handle.index,
        m_resources[handle.index].imported ? 0 : RGHandle::kInvalid) : handle;
}

RGHandle EnhancedRenderGraph::CreateTexture(const RGTextureDesc& desc)
{
    Resource entry{};
    entry.desc = desc;
    entry.state = RHIResourceState::Common;
    entry.imported = false;
    entry.name = desc.name;

    RGHandle handle{};
    handle.index = static_cast<uint16_t>(m_resources.size());
    m_resources.push_back(std::move(entry));
    m_compiled = false;
    return m_scheduling == RGSchedulingMode::ExplicitVersioned ? VersionHandle(handle.index,
        m_resources[handle.index].imported ? 0 : RGHandle::kInvalid) : handle;
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

RGHandle EnhancedRenderGraph::Write(RGHandle previous) { return AdvanceVersion(previous, false); }
RGHandle EnhancedRenderGraph::Modify(RGHandle previous) { return AdvanceVersion(previous, true); }

RGPassId EnhancedRenderGraph::AddPass(const std::string& name,
    const std::vector<RGPassUsage>& usages, ExecuteCallback execute, bool hasSideEffect)
{
    Pass pass{};
    pass.name = name;
    pass.usages = usages;
    pass.execute = std::move(execute);
    pass.hasSideEffect = hasSideEffect;

    RGPassId id{};
    id.index = static_cast<uint16_t>(m_passes.size());
    m_passes.push_back(std::move(pass));
    m_compiled = false;
    return id;
}

RGPassId EnhancedRenderGraph::AddSplitPass(const std::string& name,
    const std::vector<RGPassUsage>& usages, SplitExecuteCallback execute,
    uint32_t maxSlices, bool hasSideEffect, uint32_t recordCost)
{
    Pass pass{};
    pass.name = name;
    pass.usages = usages;
    pass.splitExecute = std::move(execute);
    pass.maxSlices = (std::max)(1u, maxSlices);
    pass.hasSideEffect = hasSideEffect;
    pass.recordCost = recordCost;

    RGPassId id{};
    id.index = static_cast<uint16_t>(m_passes.size());
    m_passes.push_back(std::move(pass));
    m_compiled = false;
    return id;
}

bool EnhancedRenderGraph::BuildExplicitOrder(std::string& outError)
{
    m_executeOrder.clear();
    const size_t count = m_passes.size();
    if (count > RGPassId::kInvalid || m_resources.size() > RGHandle::kInvalid)
    { outError = "RG1 declaration exceeds handle capacity"; return false; }
    struct Edge { uint16_t to, resource; uint16_t version{0}; };
    std::vector<std::vector<Edge>> edges(count);
    std::vector<uint32_t> incoming(count, 0);
    std::vector<bool> alive(count, true);
    m_versionEdges.clear();
    if (m_scheduling == RGSchedulingMode::ExplicitVersioned)
    {
        using Reason = DiagnosticSnapshot::VersionEdge::Reason;
        std::vector<std::vector<int32_t>> producers(m_resources.size());
        std::vector<std::vector<std::vector<uint16_t>>> consumers(m_resources.size());
        const auto fail = [&](const std::string& why, size_t r, uint16_t v) {
            outError = "RG2 " + why + ": " + m_resources[r].name + " v" + std::to_string(v); return false;
        };
        for (size_t r=0; r<m_resources.size(); ++r)
        {
            const auto& resource=m_resources[r];
            // One physical storage must have one logical version chain.
            if (resource.imported)
                for(size_t other=0; other<r; ++other)
                    if(m_resources[other].imported &&
                        ((resource.IsBuffer() && m_resources[other].buffer==resource.buffer) ||
                        (!resource.IsBuffer() && m_resources[other].handle==resource.handle)))
                        return fail("duplicate physical import; reuse its handle",r,0);
            producers[r].assign(resource.versions.size(), -1);
            consumers[r].resize(resource.versions.size());
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
            for (const auto& u:m_passes[p].usages)
            {
                const auto bad = [&](const std::string& why) {
                    outError="RG2 pass '"+m_passes[p].name+"': "+why;
                    if(u.handle.index<m_resources.size()) outError += ": "+m_resources[u.handle.index].name;
                    outError += " [resource="+std::to_string(u.handle.index)+", v"+std::to_string(u.handle.version)+"]";
                    return false;
                };
                if (!ValidVersionHandle(u.handle)) return bad("stale/foreign/invalid resource version or kind");
                const auto r=u.handle.index, v=u.handle.version;
                if (std::find(seen.begin(),seen.end(),r)!=seen.end()) return bad("duplicate resource access; use Modify: "+m_resources[r].name);
                seen.push_back(r);
                if (u.access!=RGAccessMode::Read && u.access!=RGAccessMode::Write && u.access!=RGAccessMode::ReadWrite)
                    return bad("explicit access required");
                if (u.state<RHIResourceState::Common || u.state>RHIResourceState::IndexBuffer ||
                    (Writes(u) && !IsWriteState(u.state)) ||
                    (u.access==RGAccessMode::Read && IsWriteState(u.state) && u.state!=RHIResourceState::UnorderedAccess))
                    return bad("access/state mismatch: "+m_resources[r].name);
                if (Writes(u))
                {
                    if (m_resources[r].imported && v==0) return fail("cannot overwrite imported v0; call Write/Modify",r,v);
                    const auto& declaration=m_resources[r].versions[v];
                    if (declaration.modify!=(u.access==RGAccessMode::ReadWrite)) return fail("Write/Modify access mismatch",r,v);
                    if (producers[r][v]>=0) return fail("multiple version producers",r,v);
                    producers[r][v]=static_cast<int32_t>(p);
                    if (declaration.modify) consumers[r][declaration.parent].push_back(static_cast<uint16_t>(p));
                }
                else consumers[r][v].push_back(static_cast<uint16_t>(p));
            }
        }
        for(size_t r=0; r<m_resources.size(); ++r)
            for(size_t v=0; v<producers[r].size(); ++v)
                if(producers[r][v]<0 && !(m_resources[r].imported && v==0))
                    return fail("missing version producer",r,static_cast<uint16_t>(v));
        // Only data dependencies retain work. WAR/WAW constrain surviving
        // accesses; they must not keep discarded versions or dead readers alive.
        alive.assign(count, false);
        std::vector<uint16_t> roots;
        const auto retain = [&](uint16_t p) { if(!alive[p]) { alive[p]=true; roots.push_back(p); } };
        for(size_t p=0; p<count; ++p) if(m_passes[p].hasSideEffect) retain(static_cast<uint16_t>(p));
        for(size_t r=0; r<m_resources.size(); ++r)
            if(m_resources[r].imported && producers[r].size()>1)
                retain(static_cast<uint16_t>(producers[r].back()));
        while(!roots.empty())
        {
            const auto p=roots.back(); roots.pop_back();
            for(const auto& u:m_passes[p].usages)
            {
                if(!Reads(u)) continue;
                const auto r=u.handle.index;
                const auto v=u.access==RGAccessMode::ReadWrite ? m_resources[r].versions[u.handle.version].parent : u.handle.version;
                const auto producer=producers[r][v];
                if(producer>=0) retain(static_cast<uint16_t>(producer));
            }
        }
        for(size_t p=0; p<count; ++p) m_passes[p].culled=!alive[p];
        const auto edge = [&](int32_t from, uint16_t to, uint16_t r, uint16_t v, Reason reason) {
            if (from<0 || from==to) return;
            m_versionEdges.push_back({static_cast<uint32_t>(from),to,r,v,reason});
            auto& outgoing=edges[from];
            if (std::none_of(outgoing.begin(),outgoing.end(),[&](const Edge& e){return e.to==to;}))
            { outgoing.push_back({to,r,v}); ++incoming[to]; }
        };
        for (size_t r=0; r<m_resources.size(); ++r)
        {
            int32_t previousWriter=-1;
            uint16_t previousVersion=0;
            std::vector<std::pair<uint16_t,uint16_t>> pendingReaders;
            for (size_t v=0; v<producers[r].size(); ++v)
            {
                const auto producer=producers[r][v];
                if (producer<0 && !(m_resources[r].imported && v==0)) return fail("missing version producer",r,static_cast<uint16_t>(v));
                if(producer>=0 && alive[producer])
                {
                    edge(previousWriter,static_cast<uint16_t>(producer),static_cast<uint16_t>(r),previousVersion,Reason::WAW);
                    for(const auto& [reader,readVersion]:pendingReaders)
                        edge(reader,static_cast<uint16_t>(producer),static_cast<uint16_t>(r),readVersion,Reason::WAR);
                    pendingReaders.clear(); previousWriter=producer; previousVersion=static_cast<uint16_t>(v);
                }
                for(const auto reader:consumers[r][v]) if(alive[reader])
                {
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
    std::vector<bool> modifies(m_resources.size(), false);
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
            if (u.state < RHIResourceState::Common || u.state > RHIResourceState::IndexBuffer)
                return fail("invalid resource state: " + m_resources[r].name);
            if ((Writes(u) && !IsWriteState(u.state)) ||
                (u.access == RGAccessMode::Read && IsWriteState(u.state) && u.state != RHIResourceState::UnorderedAccess))
                return fail("access/state mismatch: " + m_resources[r].name);
            if (Writes(u))
            {
                if (writer[r] >= 0) return fail("multiple writers require RG2 versions: " + m_resources[r].name
                    + " (first: " + m_passes[writer[r]].name + ")");
                writer[r] = static_cast<int32_t>(p);
                modifies[r] = u.access == RGAccessMode::ReadWrite;
                if (modifies[r] && !m_resources[r].imported) return fail("ReadWrite needs initialized imported input: " + m_resources[r].name);
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
            if (modifies[r])
            { outError = "RG1 ambiguous ReadWrite chain requires RG2 versions: " + m_resources[r].name; return false; }
            auto& outgoing = edges[producer];
            if (std::none_of(outgoing.begin(), outgoing.end(), [&](const Edge& e) { return e.to == consumer; }))
            { outgoing.push_back({consumer, static_cast<uint16_t>(r)}); ++incoming[consumer]; }
        }
    }
    std::priority_queue<uint16_t, std::vector<uint16_t>, std::greater<uint16_t>> ready;
    for (size_t p = 0; p < count; ++p) if (alive[p] && !incoming[p]) ready.push(static_cast<uint16_t>(p));
    std::vector<uint16_t> order;
    while (!ready.empty())
    {
        const auto p = ready.top(); ready.pop(); order.push_back(p);
        for (const auto& e : edges[p]) if (--incoming[e.to] == 0) ready.push(e.to);
    }
    if (order.size() != static_cast<size_t>(std::count(alive.begin(),alive.end(),true)))
    {
        // Iterative DFS: report a real cycle with resource labels, without
        // recursion depth depending on the number of authored passes.
        std::vector<uint8_t> color(count, 0);
        struct Visit { uint16_t pass; size_t next; };
        std::vector<Visit> stack;
        for (size_t root = 0; root < count; ++root)
        {
            if (!alive[root] || color[root]) continue;
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
                        outError += m_passes[it->pass].name + " --" + m_resources[resource].name
                            + (m_scheduling==RGSchedulingMode::ExplicitVersioned ? " v"+std::to_string(v) : "") + "--> ";
                    }
                    outError += m_passes[edge.to].name; return false;
                }
                color[edge.to] = 1; stack.push_back({edge.to, 0});
            }
        }
        outError = "RG1 cycle"; return false;
    }
    m_executeOrder.swap(order);
    outError.clear(); return true;
}

bool EnhancedRenderGraph::BuildOrder(std::string& outError)
{
    if (m_scheduling != RGSchedulingMode::DeclarationOrder && m_scheduling != RGSchedulingMode::ExplicitSingleWriter && m_scheduling != RGSchedulingMode::ExplicitVersioned)
    { m_executeOrder.clear(); outError = "Invalid RenderGraph scheduling mode"; return false; }
    if (m_scheduling != RGSchedulingMode::DeclarationOrder) return BuildExplicitOrder(outError);
    ce::profile_scope profile{ce::marker<"RenderGraphOrder">()};
    // ── 실행 순서는 선언 순서다. 그래프가 다시 정렬하지 않는다. ──
    //
    // 처음에는 위상 정렬로 순서를 '유도'하게 짰다가 자가 검증에서 뒤집었다.
    // 순수 데이터 흐름만으로는 순서가 정해지지 않기 때문이다 — 한 리소스에 두
    // 패스가 쓰면 둘 중 무엇이 먼저인지 알 방법이 없고, 결국 선언 순서로
    // 되돌아온다. 그러면 정렬은 선언 순서를 다시 만들어 내는 일이 되고,
    // 어쩌다 뒤집히면 프레임이 실행마다 달라져 픽셀 대조가 흔들린다.
    //
    // 그래서 계약을 이렇게 정한다:
    //   선언 순서 = 실행 순서(컬링된 것만 빠진다)
    //   그래프가 하는 일은 검증·배리어·컬링·수명이다
    //
    // 대신 선언이 데이터 흐름과 어긋나면 잡아 준다: 그래프가 만든 리소스를
    // 아무도 쓰기 전에 읽는 패스가 있으면 초기화되지 않은 메모리를 읽는 것이다.
    // 그 증상은 검은 화면이 아니라 '이전 프레임 내용이 보인다'라서 알아채기
    // 어렵다 — 컴파일에서 실패로 알린다.
    //
    // 임포트한 리소스는 다르다. 그래프 밖에서 이미 내용이 있고, 지난 프레임
    // 결과를 읽는 것(히스토리 버퍼)이 정상 사용이라 검사 대상이 아니다.
    const size_t passCount = m_passes.size();
    m_executeOrder.clear();
    if (0 == passCount) return true;

    std::vector<bool> written(m_resources.size(), false);

    for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        const Pass& pass = m_passes[passIndex];

        for (const auto& usage : pass.usages)
        {
            if (usage.access != RGAccessMode::LegacyState)
            { outError = "Explicit access requires ExplicitSingleWriter scheduling: " + pass.name; return false; }
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;
            const size_t resourceIndex = usage.handle.index;
            const Resource& resource = m_resources[resourceIndex];

            if (IsWriteState(usage.state))
            {
                written[resourceIndex] = true;
                continue;
            }

            if (!resource.imported && !written[resourceIndex])
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
    for (uint16_t index : m_executeOrder)
    {
        if (!m_passes[index].culled) survivors.push_back(index);
    }

    m_stats.passesCulled = static_cast<uint32_t>(m_executeOrder.size() - survivors.size());
    m_executeOrder.swap(survivors);
}

bool EnhancedRenderGraph::CreateTransients(std::string& outError)
{
    ce::profile_scope profile{ce::marker<"RenderGraphTransients">()};
    // 살아남은 패스가 실제로 쓰는 리소스만 만든다. 컬링된 패스만 쓰던 것을
    // 만드는 것은 낭비이고, 그 낭비는 프레임마다 반복된다.
    for (auto& resource : m_resources)
    {
        resource.used = false;
        resource.firstUse = 0xFFFFFFFF;
        resource.lastUse = 0;
    }

    for (uint32_t order = 0; order < m_executeOrder.size(); ++order)
    {
        for (const auto& usage : m_passes[m_executeOrder[order]].usages)
        {
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;
            Resource& resource = m_resources[usage.handle.index];
            resource.used = true;
            resource.firstUse = (std::min)(resource.firstUse, order);
            resource.lastUse = (std::max)(resource.lastUse, order);
        }
    }

    for (auto& resource : m_resources)
    {
        if (resource.imported || !resource.used || resource.handle.IsValid()) continue;

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

EnhancedRenderGraph::EnhancedRenderGraph(IRenderDeviceServices& services, RGSchedulingMode scheduling)
    : m_deviceServices(&services), m_scheduling(scheduling)
{
    // A live scene declares dozens of passes/resources per recording. Pass owns
    // several vectors; growing the outer array repeatedly also rebuilds their
    // checked-iterator proxies in Debug. These are initial capacities, not limits.
    m_resources.reserve(256);
    m_passes.reserve(128);
    m_executeOrder.reserve(128);
    m_resourceEpoch = NextResourceEpoch();
}

EnhancedRenderGraph::~EnhancedRenderGraph()
{
    ReleaseResources();
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
    for (auto& resource : m_resources)
    {
        if (!resource.IsValid()) continue;

        // ImportBuffer는 남의 지속 버퍼를 추적만 한다. 그래프가 등록하거나
        // 만든 것이 아니므로 반납할 일이 없다.
        if (resource.IsBuffer()) continue;

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
        if (nullptr != m_transientPool)
        {
            m_transientPool->freeList[resource.poolKey].push_back(
                { resource.handle, resource.state });
        }
        else if (nullptr != m_deviceServices)
        {
            m_deviceServices->ReleaseTexture(resource.handle);
        }
        resource.handle = {};   // 두 번 내놓지 않는다
    }
}

void EnhancedRenderGraph::PlanBarriers()
{
    std::vector<bool> previousWrite(m_resources.size(),true);
    for (auto& pass : m_passes)
    {
        pass.transitions.clear();
        pass.bufferTransitions.clear();
        pass.uavBarriers.clear();
        pass.uavBufferBarriers.clear();
    }

    // 실행 순서를 따라가며 상태를 추적한다. 요구 상태와 다르면 그 패스 앞에
    // 전이를 붙인다. 한 패스의 전이는 전부 모아 두었다가 한 번에 넣는다 —
    // 배리어를 호출마다 흩뿌리면 GPU가 그때마다 파이프라인을 비운다.
    for (uint16_t passIndex : m_executeOrder)
    {
        Pass& pass = m_passes[passIndex];

        for (const auto& usage : pass.usages)
        {
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;
            Resource& resource = m_resources[usage.handle.index];

            // ★ 핸들이 유효한가만 본다 (G-2). 예전에는 여기서 `Resolve` 로
            //   포인터를 풀어 배리어 구조체에 박았는데, 그 한 줄 때문에
            //   계획 단계가 DX12 를 알아야 했다. 실물은 기록 시점에 백엔드가
            //   푼다 — `RHIEncoder::ResourceBarriers` 가 핸들을 받는다.
            if (!resource.IsValid()) continue;

            if (resource.state == usage.state)
            {
                // 같은 상태로 연속해서 쓰는 경우, UAV만은 배리어가 필요하다 —
                // 상태는 그대로지만 앞 패스의 쓰기가 끝났음을 알려야 한다.
                if (RHIResourceState::UnorderedAccess == usage.state &&
                    (m_scheduling!=RGSchedulingMode::ExplicitVersioned || previousWrite[usage.handle.index] || Writes(usage)))
                {
                    if (resource.IsBuffer())
                        pass.uavBufferBarriers.push_back(resource.buffer);
                    else
                        pass.uavBarriers.push_back(resource.handle);
                }
                previousWrite[usage.handle.index]=Writes(usage);
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
            previousWrite[usage.handle.index]=Writes(usage);
        }

        const size_t barrierCount = pass.transitions.size() +
            pass.bufferTransitions.size() + pass.uavBarriers.size() +
            pass.uavBufferBarriers.size();
        m_stats.barriersEmitted += static_cast<uint32_t>(barrierCount);
        if (0 != barrierCount) ++m_stats.barrierBatches;
    }

    // 프레임을 넘겨 사는 리소스에 최종 상태를 돌려준다. 다음 프레임의
    // Import가 이 값을 '현재 상태'로 알려 줘야 첫 배리어가 맞는다.
    for (auto& resource : m_resources)
    {
        if (nullptr != resource.writeback) *resource.writeback = resource.state;
    }
}

bool EnhancedRenderGraph::CaptureDiagnosticSnapshot(DiagnosticSnapshot& output) const
{
    output = {};
    if (!m_compiled) return false;
    output.executeOrder = m_executeOrder;
    for (const auto& resource : m_resources)
        output.resources.push_back({resource.name, resource.imported, resource.IsBuffer(),
            resource.used, resource.firstUse, resource.lastUse});
    const auto textureIndex = [this](RHITextureHandle handle) -> uint32_t
    {
        for (uint32_t i = 0; i < m_resources.size(); ++i)
            if (!m_resources[i].IsBuffer() && m_resources[i].handle == handle) return i;
        return RGHandle::kInvalid;
    };
    const auto bufferIndex = [this](RHIBufferHandle handle) -> uint32_t
    {
        for (uint32_t i = 0; i < m_resources.size(); ++i)
            if (m_resources[i].IsBuffer() && m_resources[i].buffer == handle) return i;
        return RGHandle::kInvalid;
    };
    for (uint32_t i = 0; i < m_passes.size(); ++i)
    {
        const auto& pass = m_passes[i];
        DiagnosticPass copy{pass.name, i, -1, pass.culled, pass.hasSideEffect,
            pass.recordCost, pass.maxSlices, {}, {}};
        const auto position = std::find(m_executeOrder.begin(), m_executeOrder.end(), i);
        if (position != m_executeOrder.end())
            copy.compiledIndex = static_cast<int32_t>(position - m_executeOrder.begin());
        for (const auto& usage : pass.usages)
            copy.usages.push_back({usage.handle.index, usage.state, Writes(usage), usage.access, usage.handle.version, usage.handle.kind});
        for (const auto& barrier : pass.transitions)
            copy.barriers.push_back({textureIndex(barrier.texture), barrier.before, barrier.after, false});
        for (const auto& barrier : pass.bufferTransitions)
            copy.barriers.push_back({bufferIndex(barrier.buffer), barrier.before, barrier.after, false});
        for (const auto handle : pass.uavBarriers)
            copy.barriers.push_back({textureIndex(handle), RHIResourceState::UnorderedAccess,
                RHIResourceState::UnorderedAccess, true});
        for (const auto handle : pass.uavBufferBarriers)
            copy.barriers.push_back({bufferIndex(handle), RHIResourceState::UnorderedAccess,
                RHIResourceState::UnorderedAccess, true});
        output.passes.push_back(std::move(copy));
    }
    output.versionEdges = m_versionEdges;
    if(m_scheduling==RGSchedulingMode::ExplicitVersioned)
    {
        for(const auto& edge:m_versionEdges)
            if(edge.reason==DiagnosticSnapshot::VersionEdge::Reason::RAW)
                output.reachabilityEdges.push_back({edge.producer,edge.consumer,edge.resource});
        return true;
    }
    // Matches legacy CullPasses: all state-inferred writers of a read resource.
    // These are reachability edges, not a scheduled/versioned DAG.
    for (uint32_t consumer = 0; consumer < output.passes.size(); ++consumer)
        for (const auto& read : output.passes[consumer].usages)
            if (!read.inferredWrite)
                for (uint32_t producer = 0; producer < output.passes.size(); ++producer)
                    for (const auto& write : output.passes[producer].usages)
                        if (write.inferredWrite && write.resource == read.resource)
                            output.reachabilityEdges.push_back({producer, consumer, read.resource});
    return true;
}

bool EnhancedRenderGraph::Compile(std::string& outError)
{
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
    m_stats.passesDeclared = static_cast<uint32_t>(m_passes.size());

    if (!BuildOrder(outError)) return false;

    CullPasses();

    if (!CreateTransients(outError)) return false;

    PlanBarriers();

    m_stats.passesExecuted = static_cast<uint32_t>(m_executeOrder.size());
    m_compiled = true;
    return true;
}

bool EnhancedRenderGraph::Execute(std::string& outError)
{
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

    for (uint16_t passIndex : m_executeOrder)
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
            ? m_profiler->BeginPass(encoder, pass.name)
            : IRHIGpuProfiler::kInvalidSlot;

        // 네 부류를 하나의 batch로 건다. 인코더가 감싼 command target에
        // 기록하므로 순차와 병렬 경로의 배리어 구현이 갈리지 않는다(G-2).
        RecordPassBarriers(encoder, pass);

        // 분할 패스는 조각 하나로 부른다 — 통째로 기록하는 것과 같아야 한다는
        // 것이 계약이고, 순차 경로가 그 계약의 기준이 된다.
        try
        {
            if (pass.splitExecute)
                pass.splitExecute(context, 0, 1);
            else if (pass.execute)
                pass.execute(context);
        }
        catch (const std::exception& exception)
        {
            if (nullptr != m_profiler)
                m_profiler->EndPass(encoder, timerSlot);
            outError = pass.name + ": " + exception.what();
            return false;
        }
        catch (...)
        {
            if (nullptr != m_profiler)
                m_profiler->EndPass(encoder, timerSlot);
            outError = pass.name + ": unknown recording failure";
            return false;
        }

        if (nullptr != m_profiler) m_profiler->EndPass(encoder, timerSlot);
    }

    return true;
}

void EnhancedRenderGraph::RecordPassBarriers(RHIEncoder& encoder, const Pass& pass) const
{
    if (pass.transitions.empty() && pass.bufferTransitions.empty() &&
        pass.uavBarriers.empty() && pass.uavBufferBarriers.empty()) return;

    RHIBarrierBatch batch{};
    batch.textureTransitions = pass.transitions;
    batch.bufferTransitions = pass.bufferTransitions;
    batch.uavTextures = pass.uavBarriers;
    batch.uavBuffers = pass.uavBufferBarriers;
    encoder.ResourceBarriers(batch);
}

bool EnhancedRenderGraph::RecordParallel(IRHIParallelCommandPool& pool,
    uint32_t workerCount, const RHIRecordedBatchDesc& batchDesc,
    RHIRecordedBatch& outBatch, std::string& outError)
{
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

    const size_t passCount = m_executeOrder.size();
    if (0 == passCount)
    {
        return pool.FinalizeRecordedBatch({}, batchDesc, outBatch, outError);
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
    for (const size_t index : m_executeOrder)
    {
        totalRecordCost += m_passes[index].recordCost;
    }

    // 호출부가 병렬을 요청했더라도 그래프가 되무른다 — 이 판단을 호출부마다
    // 복제하면 한 곳이 빠졌을 때 조용히 느려진다.
    //
    // 되무르는 방법은 별도 경로가 아니라 '워커 1·조각 1'이다. 조각까지 1로
    // 되돌리는 것이 중요하다 — 워커만 1로 두면 한 워커가 조각들을 연달아
    // 맡으면서 조각마다 상태를 다시 거는 비용이 그대로 남는다. 조각 1은
    // 통째로 기록한 것과 같다는 것이 분할 조각의 계약이다.
    const bool declineParallel =
        (0 != totalRecordCost && totalRecordCost < m_parallelCostThreshold);

    m_stats.totalRecordCost = totalRecordCost;
    m_stats.parallelDeclined = declineParallel;

    const uint32_t poolWorkers = declineParallel ? 1u : (std::max)(1u,
        (std::min)(workerCount, pool.GetWorkerCount()));

    std::vector<RecordUnit> units;
    units.reserve(passCount * 2);

    for (size_t order = 0; order < passCount; ++order)
    {
        const Pass& pass = m_passes[m_executeOrder[order]];

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
    if (m_preparedPool)
    {
        m_preparedRecordingConsumed = true;
    }
    else if (!pool.Prepare(outError))
        return false;

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
    std::vector<uint32_t> unitWorker(unitCount, 0);
    for (size_t i = 0; i < unitCount; ++i)
    {
        unitWorker[i] = static_cast<uint32_t>(i * workers / unitCount);
    }

    const auto discardRecording = [&]
    {
        std::string closeError;
        if (!pool.CloseAll(closeError)) outError += " / 기록 폐기 중 닫기 실패: " + closeError;
    };

    // 리스트를 먼저 전부 연다.
    //
    // Open은 얼로케이터를 Reset하므로 스레드 안전하지 않다. 기록에 들어가기
    // 전에 한 스레드에서 끝내 두면 워커는 '이미 열린 리스트에 기록'만 한다.
    for (uint32_t worker = 0; worker < workers; ++worker)
    {
        if (!pool.OpenWorker(worker, outError))
        {
            discardRecording();
            return false;
        }
    }

    // 워커에서 터진 예외를 삼키지 않는다. 조용히 사라지면 '가끔 화면이 빈다'가
    // 되고, 그 상태는 원인을 찾기가 매우 어렵다.
    std::vector<std::string> workerErrors(workers);
    std::atomic<bool> failed{ false };

    const auto recordRange = [&](uint32_t worker)
    {
        ExecuteContext context{};
        context.graph = this;

        try
        {
            for (size_t i = 0; i < unitCount; ++i)
            {
                if (unitWorker[i] != worker) continue;

                const RecordUnit& unit = units[i];
                Pass& pass = m_passes[m_executeOrder[unit.order]];

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
                    ? m_profiler->BeginPass(encoder, pass.name)
                    : IRHIGpuProfiler::kInvalidSlot;

                // 배리어는 패스의 첫 조각에만 넣는다. 조각들은 순서대로
                // 실행되므로 앞에 한 번이면 뒤 조각까지 덮는다.
                if (0 == unit.slice)
                {
                    RecordPassBarriers(encoder, pass);
                }

                if (pass.splitExecute) pass.splitExecute(context, unit.slice, unit.sliceCount);
                else if (pass.execute) pass.execute(context);

                if (nullptr != m_profiler) m_profiler->EndPass(encoder, timerSlot);
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
        pool.RunParallel(recordRange, workers);
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
    if (!pool.FinalizeRecordedBatch(submission, batchDesc, outBatch, outError)) return false;

    m_stats.recordWorkers = workers;
    m_stats.recordedLists = static_cast<uint32_t>(submission.size());
    m_stats.recordUnits = static_cast<uint32_t>(unitCount);
    return true;
}

bool EnhancedRenderGraph::IsPassCulled(RGPassId pass) const
{
    if (!pass.IsValid() || pass.index >= m_passes.size()) return true;
    return m_passes[pass.index].culled;
}

uint32_t EnhancedRenderGraph::GetPassBarrierCount(RGPassId pass) const
{
    if (!pass.IsValid() || pass.index >= m_passes.size()) return 0;
    const Pass& planned = m_passes[pass.index];
    return static_cast<uint32_t>(planned.transitions.size() +
        planned.bufferTransitions.size() + planned.uavBarriers.size() +
        planned.uavBufferBarriers.size());
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
