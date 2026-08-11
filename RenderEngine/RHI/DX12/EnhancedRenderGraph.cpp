#ifndef DYNAMICCPP_EXPORTS
#include "DX12Format.h"
#include "EnhancedRenderGraph.h"
#include "DX12Encoder.h"
#include "DX12DeviceResources.h"

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
}

// ★ 변환은 디바이스에 한 벌만 둔다(V3). 그래프 밖 전이가
//   DX12DeviceResources::ToD3D12를 쓰는데 그래프가 자기 것을 따로 들면,
//   두 벌이 어긋나는 날 배리어의 before와 after가 다른 규칙으로 만들어진다.
D3D12_RESOURCE_STATES EnhancedRenderGraph::ToD3D12(RHIResourceState state)
{
    return DX12DeviceResources::ToD3D12(state);
}

RHITextureHandle EnhancedRenderGraph::ExecuteContext::ResolveHandle(RGHandle handle) const
{
    if (nullptr == graph || !handle.IsValid()) return {};
    if (handle.index >= graph->m_resources.size()) return {};

    return graph->m_resources[handle.index].handle;
}

void EnhancedRenderGraph::Reset()
{
    ReleaseResources();
    m_resources.clear();
    m_passes.clear();
    m_executeOrder.clear();
    m_compiled = false;
    m_stats = Stats{};
}

RGHandle EnhancedRenderGraph::ImportTexture(ID3D12Resource* resource,
    RHIResourceState currentState, const std::string& name,
    RHIResourceState* stateWriteback)
{
    if (nullptr == resource || nullptr == m_deviceServices) return {};

    // 표에 없는 것이므로 그래프가 등록하고, 소멸할 때 놓는다.
    const RHITextureHandle registered = m_deviceServices->RegisterExternalTexture(resource);
    RGHandle handle = ImportTexture(registered, currentState, name, stateWriteback);
    if (handle.IsValid()) m_resources[handle.index].ownsRegistration = true;
    return handle;
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
    entry.name = name;

    handle.index = static_cast<uint16_t>(m_resources.size());
    m_resources.push_back(std::move(entry));
    return handle;
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

bool EnhancedRenderGraph::CreateTransients(std::string& outError)
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

EnhancedRenderGraph::EnhancedRenderGraph(DX12DeviceResources& resources)
    : m_deviceServices(&resources)
{
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
        if (!resource.handle.IsValid()) continue;

        // ── transient: 플래그를 보지 않는다 ──
        //
        // ★ 여기에 결함이 있었다(V2-c2, 2026-08-10). 반납 조건이
        //   ownsRegistration이었는데 풀에서 '빌려 온' 경로가 그 플래그를
        //   세우지 않아, 빌린 것이 반납되지 않았다. 결과는 한 프레임 걸러
        //   전 transient를 CreateCommittedResource로 다시 만드는 것 —
        //   PHASE 3-9가 풀을 넣어 없앤 바로 그 비용(프레임당 수십 ms)이
        //   되살아났고, 에디터 카메라가 뚝뚝 끊겼다.
        //
        //   고치면서 조건 자체를 없앴다. transient는 '그래프가 만들었든
        //   빌렸든 그래프가 끝나면 내놓는다'가 예외 없는 규칙이므로, 그것을
        //   기억하는 플래그가 있으면 안 된다. 세우는 자리가 둘이면 언젠가
        //   한쪽을 빠뜨린다 — 실제로 빠뜨렸다.
        if (!resource.imported)
        {
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
            continue;
        }

        // ── 임포트: 그래프가 표에 올린 것만 놓는다 ──
        //
        // 핸들로 받은 것(ImportTexture(RHITextureHandle))은 남의 것이다.
        // 이 플래그는 이제 그 한 가지만 뜻한다.
        if (resource.ownsRegistration)
        {
            if (nullptr != m_deviceServices) m_deviceServices->ReleaseTexture(resource.handle);
            resource.ownsRegistration = false;
            resource.handle = {};
        }
    }
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

            ID3D12Resource* native = m_deviceServices->Resolve(resource.handle);
            if (nullptr == native) continue;

            if (resource.state == usage.state)
            {
                // 같은 상태로 연속해서 쓰는 경우, UAV만은 배리어가 필요하다 —
                // 상태는 그대로지만 앞 패스의 쓰기가 끝났음을 알려야 한다.
                if (RHIResourceState::UnorderedAccess == usage.state)
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

    // 프레임을 넘겨 사는 리소스에 최종 상태를 돌려준다. 다음 프레임의
    // Import가 이 값을 '현재 상태'로 알려 줘야 첫 배리어가 맞는다.
    for (auto& resource : m_resources)
    {
        if (nullptr != resource.writeback) *resource.writeback = resource.state;
    }
}

bool EnhancedRenderGraph::Compile(std::string& outError)
{
    // ★ 디바이스를 인자로 받지 않는다 (G-1). 호출부 37곳이 전부
    //   `resources.GetDevice()` 를 넘기고 있었는데, 그래프는 그
    //   `DX12DeviceResources&` 를 **생성자로 이미 든다** — 받는 쪽이 아는
    //   값을 넘기던 동어반복이다(A-3 이 VolFog 에서 없앤 것과 같은 부류).
    if (nullptr == m_deviceServices)
    {
        outError = "디바이스 서비스가 없다";
        return false;
    }

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

    ID3D12GraphicsCommandList* commandList = m_deviceServices->GetCommandList();
    if (nullptr == commandList)
    {
        outError = "커맨드 리스트가 없다";
        return false;
    }

    ExecuteContext context{};

    context.graph = this;

    for (uint16_t passIndex : m_executeOrder)
    {
        Pass& pass = m_passes[passIndex];

        // ★ 인코더는 패스마다 새로 만든다. 인코더가 "지금 걸려 있는 루트
        //   시그니처"를 기억하는데, 그 기억이 패스 경계를 넘으면 틀린다 —
        //   아직 안 옮긴 패스가 같은 커맨드 리스트에 원시로 루트를 걸면
        //   기억이 낡고, 다음 패스가 중복이라 여겨 건너뛴다.
        //
        //   부르는 것을 잊으면 조용히 잘못 그리는 무효화 함수를 두는 대신,
        //   수명으로 막는다. 기억이 패스보다 오래 살 수 없다.
        DX12Encoder encoder(commandList, m_deviceServices);
        context.encoder = &encoder;

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

        // 분할 패스는 조각 하나로 부른다 — 통째로 기록하는 것과 같아야 한다는
        // 것이 계약이고, 순차 경로가 그 계약의 기준이 된다.
        if (pass.splitExecute) pass.splitExecute(context, 0, 1);
        else if (pass.execute)  pass.execute(context);

        if (nullptr != m_profiler) m_profiler->EndPass(commandList, timerSlot);
    }

    return true;
}

bool EnhancedRenderGraph::ExecuteParallel(DX12CommandListPool& pool,
    uint32_t workerCount, std::string& outError)
{
    if (!m_compiled)
    {
        outError = "Compile을 먼저 불러야 한다";
        return false;
    }
    // 〃 큐도 인자가 아니다 (G-1). 호출부 둘이 `resources.GetCommandQueue()` 를
    // 넘기고 있었다. 풀은 남긴다 — 그것은 호출부가 소유하고 수명도 그쪽 것이다.
    if (nullptr == m_deviceServices)
    {
        outError = "디바이스 서비스가 없다";
        return false;
    }

    ID3D12CommandQueue* queue = m_deviceServices->GetCommandQueue();
    if (!pool.IsInitialized() || nullptr == queue)
    {
        outError = "커맨드 리스트 풀이나 큐가 없다";
        return false;
    }

    const size_t passCount = m_executeOrder.size();
    if (0 == passCount) return true;

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
        ID3D12GraphicsCommandList* const workerList = workerLists[worker];
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
                DX12Encoder encoder(workerList, m_deviceServices);
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
                    ? m_profiler->BeginPass(workerList, pass.name)
                    : DX12GpuProfiler::kInvalidSlot;

                // 배리어는 패스의 첫 조각에만 넣는다. 조각들은 순서대로
                // 실행되므로 앞에 한 번이면 뒤 조각까지 덮는다.
                if (0 == unit.slice && !pass.barriers.empty())
                {
                    workerList->ResourceBarrier(
                        static_cast<UINT>(pass.barriers.size()), pass.barriers.data());
                }

                if (pass.splitExecute) pass.splitExecute(context, unit.slice, unit.sliceCount);
                else if (pass.execute) pass.execute(context);

                if (nullptr != m_profiler) m_profiler->EndPass(workerList, timerSlot);
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

    // 풀의 지속 워커를 쓴다.
    //
    // 처음에는 매 프레임 std::thread를 만들었는데 실측으로 그것이 손해였다:
    // 순차 1.3761 ms · 병렬 2.3419 ms(워커 4) — 1.7배 느렸다. 기록 자체가
    // 가벼운 씬에서는 스레드 생성 비용이 기록 비용을 넘는다.
    //
    // 워커가 하나면 풀이 깨우지 않고 그 자리에서 부른다 — 비교 기준에
    // 동기화 비용이 섞이지 않아야 한다.
    const std::function<void(uint32_t)> job = recordRange;
    pool.RunParallel(job, workers);

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
