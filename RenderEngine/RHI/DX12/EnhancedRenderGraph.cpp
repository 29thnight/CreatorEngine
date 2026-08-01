#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedRenderGraph.h"

#include <algorithm>
#include <atomic>
#include <thread>

#include <algorithm>
#include <queue>
#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string GraphHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // 쓰기로 보는 상태. 의존성 유도와 컬링이 이 구분을 쓴다.
    bool IsWriteState(RGResourceState state)
    {
        switch (state)
        {
        case RGResourceState::RenderTarget:
        case RGResourceState::DepthWrite:
        case RGResourceState::UnorderedAccess:
        case RGResourceState::CopyDest:
            return true;
        default:
            return false;
        }
    }
}

D3D12_RESOURCE_STATES EnhancedRenderGraph::ToD3D12(RGResourceState state)
{
    switch (state)
    {
    case RGResourceState::RenderTarget:    return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RGResourceState::DepthWrite:      return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RGResourceState::DepthRead:       return D3D12_RESOURCE_STATE_DEPTH_READ;
    case RGResourceState::ShaderResource:  return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case RGResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case RGResourceState::CopySource:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RGResourceState::CopyDest:        return D3D12_RESOURCE_STATE_COPY_DEST;
    case RGResourceState::Common:
    default:                               return D3D12_RESOURCE_STATE_COMMON;
    }
}

ID3D12Resource* EnhancedRenderGraph::ExecuteContext::Resolve(RGHandle handle) const
{
    if (nullptr == graph || !handle.IsValid()) return nullptr;
    if (handle.index >= graph->m_resources.size()) return nullptr;

    const Resource& resource = graph->m_resources[handle.index];
    return resource.imported ? resource.external : resource.owned.Get();
}

void EnhancedRenderGraph::Reset()
{
    m_resources.clear();
    m_passes.clear();
    m_executeOrder.clear();
    m_compiled = false;
    m_stats = Stats{};
}

RGHandle EnhancedRenderGraph::ImportTexture(ID3D12Resource* resource,
    RGResourceState currentState, const std::string& name)
{
    RGHandle handle{};
    if (nullptr == resource) return handle;

    Resource entry{};
    entry.external = resource;
    entry.state = currentState;
    entry.imported = true;
    entry.name = name;

    handle.index = static_cast<uint16_t>(m_resources.size());
    m_resources.push_back(std::move(entry));
    return handle;
}

RGHandle EnhancedRenderGraph::CreateTexture(const RGTextureDesc& desc)
{
    Resource entry{};
    entry.desc = desc;
    entry.state = RGResourceState::Common;
    entry.imported = false;
    entry.name = desc.name;

    RGHandle handle{};
    handle.index = static_cast<uint16_t>(m_resources.size());
    m_resources.push_back(std::move(entry));
    return handle;
}

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
    return id;
}

bool EnhancedRenderGraph::BuildOrder(std::string& outError)
{
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
    const size_t passCount = m_passes.size();
    for (auto& pass : m_passes) pass.culled = true;

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
                if (m_resources[usage.handle.index].imported && IsWriteState(usage.state))
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
            if (IsWriteState(usage.state)) continue;
            if (!usage.handle.IsValid() || usage.handle.index >= m_resources.size()) continue;

            // 이 리소스를 앞서 쓴 패스들을 살린다.
            for (size_t producer = 0; producer < passCount; ++producer)
            {
                if (!m_passes[producer].culled) continue;

                for (const auto& producerUsage : m_passes[producer].usages)
                {
                    if (producerUsage.handle.index == usage.handle.index &&
                        IsWriteState(producerUsage.state))
                    {
                        m_passes[producer].culled = false;
                        stack.push_back(producer);
                        break;
                    }
                }
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

bool EnhancedRenderGraph::CreateTransients(ID3D12Device* device, std::string& outError)
{
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
        if (resource.imported || !resource.used || resource.owned) continue;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = resource.desc.width;
        desc.Height = resource.desc.height;
        desc.DepthOrArraySize = static_cast<UINT16>((std::max)(1u, resource.desc.arraySize));
        desc.MipLevels = 1;
        desc.Format = resource.desc.format;
        desc.SampleDesc.Count = 1;
        if (resource.desc.allowRenderTarget)    desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (resource.desc.allowDepthStencil)    desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (resource.desc.allowUnorderedAccess) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // 클리어 힌트는 실제 클리어 값과 일치해야 검증 레이어가 조용하다
        // (3-3에서 실측한 규칙 — 힌트 없음도, 다른 값도 경고를 쌓는다).
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = resource.desc.format;
        const bool wantsClearValue = resource.desc.allowRenderTarget || resource.desc.allowDepthStencil;
        if (resource.desc.allowDepthStencil)
        {
            clearValue.DepthStencil.Depth = 1.f;
        }
        else
        {
            for (int i = 0; i < 4; ++i) clearValue.Color[i] = resource.desc.clearColor[i];
        }

        const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, wantsClearValue ? &clearValue : nullptr,
            IID_PPV_ARGS(&resource.owned));
        if (FAILED(hr))
        {
            outError = "transient 리소스 생성 실패(" + resource.name + ") " + GraphHrToString(hr);
            return false;
        }

        if (!resource.name.empty())
        {
            const std::wstring wide(resource.name.begin(), resource.name.end());
            resource.owned->SetName(wide.c_str());
        }
        resource.state = RGResourceState::Common;
        ++m_stats.transientCreated;
    }

    return true;
}

void EnhancedRenderGraph::PlanBarriers()
{
    for (auto& pass : m_passes) pass.barriers.clear();

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

            ID3D12Resource* native = resource.imported ? resource.external : resource.owned.Get();
            if (nullptr == native) continue;

            if (resource.state == usage.state)
            {
                // 같은 상태로 연속해서 쓰는 경우, UAV만은 배리어가 필요하다 —
                // 상태는 그대로지만 앞 패스의 쓰기가 끝났음을 알려야 한다.
                if (RGResourceState::UnorderedAccess == usage.state)
                {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.UAV.pResource = native;
                    pass.barriers.push_back(barrier);
                }
                continue;
            }

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = native;
            barrier.Transition.StateBefore = ToD3D12(resource.state);
            barrier.Transition.StateAfter = ToD3D12(usage.state);
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            pass.barriers.push_back(barrier);

            resource.state = usage.state;
        }

        m_stats.barriersEmitted += static_cast<uint32_t>(pass.barriers.size());
        if (!pass.barriers.empty()) ++m_stats.barrierBatches;
    }
}

bool EnhancedRenderGraph::Compile(ID3D12Device* device, std::string& outError)
{
    if (nullptr == device)
    {
        outError = "디바이스가 없다";
        return false;
    }

    m_stats = Stats{};
    m_stats.passesDeclared = static_cast<uint32_t>(m_passes.size());

    if (!BuildOrder(outError)) return false;

    CullPasses();

    if (!CreateTransients(device, outError)) return false;

    PlanBarriers();

    m_stats.passesExecuted = static_cast<uint32_t>(m_executeOrder.size());
    m_compiled = true;
    return true;
}

bool EnhancedRenderGraph::Execute(ID3D12GraphicsCommandList* commandList, std::string& outError)
{
    if (!m_compiled)
    {
        outError = "Compile을 먼저 불러야 한다";
        return false;
    }
    if (nullptr == commandList)
    {
        outError = "커맨드 리스트가 없다";
        return false;
    }

    ExecuteContext context{};
    context.commandList = commandList;
    context.graph = this;

    for (uint16_t passIndex : m_executeOrder)
    {
        Pass& pass = m_passes[passIndex];

        // 배리어를 측정 구간 안에 둔다. 배리어도 GPU 시간을 쓰고, 그 비용이
        // 어느 패스 때문에 생겼는지가 곧 그 패스의 비용이다.
        const uint32_t timerSlot = (nullptr != m_profiler)
            ? m_profiler->BeginPass(commandList, pass.name)
            : DX12GpuProfiler::kInvalidSlot;

        if (!pass.barriers.empty())
        {
            commandList->ResourceBarrier(static_cast<UINT>(pass.barriers.size()),
                pass.barriers.data());
        }

        if (pass.execute) pass.execute(context);

        if (nullptr != m_profiler) m_profiler->EndPass(commandList, timerSlot);
    }

    return true;
}

bool EnhancedRenderGraph::ExecuteParallel(DX12CommandListPool& pool,
    ID3D12CommandQueue* queue, uint32_t workerCount, std::string& outError)
{
    if (!m_compiled)
    {
        outError = "Compile을 먼저 불러야 한다";
        return false;
    }
    if (!pool.IsInitialized() || nullptr == queue)
    {
        outError = "커맨드 리스트 풀이나 큐가 없다";
        return false;
    }

    const size_t passCount = m_executeOrder.size();
    if (0 == passCount) return true;

    const uint32_t workers = (std::max)(1u, (std::min)(
        (std::min)(workerCount, pool.GetWorkerCount()),
        static_cast<uint32_t>(passCount)));

    // ── 배분: 연속 블록 ──
    //
    // 라운드로빈으로 흩으면 안 된다. 워커가 A,B,A 순으로 돌아오면 A의 리스트를
    // 두 번 제출해야 하는데, 그러면 A가 기록한 커맨드 전체가 두 번 실행된다.
    // 리스트는 통째로 실행되는 단위라 '그 안의 일부만' 제출할 수 없다.
    //
    // 연속 블록이면 워커 순서 = 선언 순서가 되고, 리스트도 한 번씩만 제출된다.
    // 대신 부하가 한쪽에 몰릴 수 있다 — 패스별 비용을 알기 전에는 균등 분할이
    // 최선이고, 비용 기반 분할은 프로파일러가 패스별 시간을 병렬 경로에서도
    // 줄 수 있게 된 뒤에 얹는다.
    std::vector<uint32_t> passWorker(passCount, 0);
    for (size_t i = 0; i < passCount; ++i)
    {
        passWorker[i] = static_cast<uint32_t>(i * workers / passCount);
    }

    // 리스트를 먼저 전부 연다.
    //
    // Open은 얼로케이터를 Reset하므로 스레드 안전하지 않다. 기록에 들어가기
    // 전에 한 스레드에서 끝내 두면 워커는 '이미 열린 리스트에 기록'만 한다.
    std::vector<ID3D12GraphicsCommandList*> workerLists(workers, nullptr);
    for (uint32_t worker = 0; worker < workers; ++worker)
    {
        workerLists[worker] = pool.Open(worker, outError);
        if (nullptr == workerLists[worker]) return false;
    }

    // 워커에서 터진 예외를 삼키지 않는다. 조용히 사라지면 '가끔 화면이 빈다'가
    // 되고, 그 상태는 원인을 찾기가 매우 어렵다.
    std::vector<std::string> workerErrors(workers);
    std::atomic<bool> failed{ false };

    const auto recordRange = [&](uint32_t worker)
    {
        ExecuteContext context{};
        context.commandList = workerLists[worker];
        context.graph = this;

        try
        {
            for (size_t order = 0; order < passCount; ++order)
            {
                if (passWorker[order] != worker) continue;

                Pass& pass = m_passes[m_executeOrder[order]];

                // 패스별 GPU 타임스탬프는 병렬 경로에서 재지 않는다.
                // 질의 슬롯은 리스트 하나 안에서 순서가 맞아야 하는데 지금은
                // 리스트가 여럿이다. 워커별 질의 힙으로 나누는 것이 다음 단계다.
                if (!pass.barriers.empty())
                {
                    context.commandList->ResourceBarrier(
                        static_cast<UINT>(pass.barriers.size()), pass.barriers.data());
                }

                if (pass.execute) pass.execute(context);
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

    if (1 == workers)
    {
        // 워커가 하나면 스레드를 띄우지 않는다. 비교 기준을 잴 때 스레드 생성
        // 비용이 섞이면 '병렬이 느리다'가 무엇 때문인지 흐려진다.
        recordRange(0);
    }
    else
    {
        std::vector<std::thread> threads;
        threads.reserve(workers - 1);
        for (uint32_t worker = 1; worker < workers; ++worker)
        {
            threads.emplace_back(recordRange, worker);
        }
        recordRange(0);   // 호출 스레드도 한 몫 한다
        for (auto& thread : threads) thread.join();
    }

    if (failed.load(std::memory_order_relaxed))
    {
        for (uint32_t worker = 0; worker < workers; ++worker)
        {
            if (!workerErrors[worker].empty())
            {
                outError = "워커 " + std::to_string(worker) + " 기록 실패: "
                    + workerErrors[worker];
                return false;
            }
        }
        outError = "워커 기록 실패(사유 미상)";
        return false;
    }

    if (!pool.CloseAll(outError)) return false;

    // 제출은 워커 번호 순서다. 연속 블록 배분이므로 그것이 곧 선언 순서다.
    std::vector<ID3D12CommandList*> submission;
    submission.reserve(workers);
    for (uint32_t worker = 0; worker < workers; ++worker)
    {
        if (!pool.HasRecorded(worker)) continue;
        submission.push_back(workerLists[worker]);
    }

    if (!submission.empty())
    {
        queue->ExecuteCommandLists(static_cast<UINT>(submission.size()), submission.data());
    }

    m_stats.recordWorkers = workers;
    m_stats.submittedLists = static_cast<uint32_t>(submission.size());
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
    return static_cast<uint32_t>(m_passes[pass.index].barriers.size());
}

bool EnhancedRenderGraph::GetTransientLifetime(RGHandle handle,
    uint32_t& outFirst, uint32_t& outLast) const
{
    if (!handle.IsValid() || handle.index >= m_resources.size()) return false;

    const Resource& resource = m_resources[handle.index];
    if (!resource.used) return false;

    outFirst = resource.firstUse;
    outLast = resource.lastUse;
    return true;
}

#endif
