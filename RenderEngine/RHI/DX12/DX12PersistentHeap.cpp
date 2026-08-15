#ifndef DYNAMICCPP_EXPORTS

#include "DX12PersistentHeap.h"

#include <algorithm>
#include <sstream>

namespace
{
    constexpr uint64_t kDx12PersistentBufferCompatibilityKey = 1;
    constexpr uint64_t kDx12PersistentSampledTextureCompatibilityKey = 2;

    std::string DX12PersistentHeapHr(HRESULT hr)
    {
        std::ostringstream stream;
        stream << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return stream.str();
    }
}

bool DX12PersistentHeap::Initialize(ID3D12Device* device, IDXGIAdapter1* adapter,
    RHIDeviceMemoryBudgetCoordinator* budgetCoordinator,
    std::string& outError,
    const RHIPersistentHeapConfig& config)
{
    Shutdown();
    if (nullptr == device)
    {
        outError = "DX12 persistent heap에 디바이스가 필요하다";
        return false;
    }
    if (0 == config.defaultSegmentBytes || 0 == config.dedicatedThresholdBytes)
    {
        outError = "DX12 persistent heap 설정 크기가 0이다";
        return false;
    }

    m_device = device;
    if (nullptr != adapter) adapter->QueryInterface(IID_PPV_ARGS(&m_adapter));
    m_budgetCoordinator = budgetCoordinator;
    if (nullptr != m_budgetCoordinator)
        m_budgetOwner = m_budgetCoordinator->RegisterOwner();
    m_policy.Reset(config);
    RefreshBudget();
    return true;
}

void DX12PersistentHeap::Shutdown()
{
    if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
    {
        for (const auto& segment : m_segments)
        {
            if (segment.second)
                m_budgetCoordinator->RecordRelease(m_budgetOwner,
                    kRHIDeviceLocalMemoryBudgetDomain,
                    segment.second->GetDesc().SizeInBytes);
        }
    }
    m_segments.clear();
    if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
        m_budgetCoordinator->UnregisterOwner(m_budgetOwner);
    m_budgetCoordinator = nullptr;
    m_budgetOwner = 0;
    m_adapter.Reset();
    m_policy.Reset();
    m_softBudgetBytes = 0;
    m_memoryPressure = false;
    m_budgetOverrideForTesting = false;
    m_device = nullptr;
}

bool DX12PersistentHeap::CreateNativeSegment(uint64_t compatibilityKey,
    D3D12_HEAP_FLAGS heapFlags, uint64_t bytes,
    RHIPersistentHeapSegmentHandle& outHandle, std::string& outError)
{
    outHandle = {};
    D3D12_HEAP_DESC heapDesc{};
    heapDesc.SizeInBytes = bytes;
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Flags = heapFlags;

    Microsoft::WRL::ComPtr<ID3D12Heap> heap;
    const HRESULT created = m_device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap));
    if (FAILED(created))
    {
        outError = "DX12 persistent ID3D12Heap 생성 실패 " + DX12PersistentHeapHr(created);
        return false;
    }

    outHandle = m_policy.AddSegment(compatibilityKey, bytes);
    if (!outHandle.IsValid())
    {
        outError = "DX12 persistent heap policy segment 등록 실패";
        return false;
    }
    m_segments.emplace(NativeKey(outHandle), std::move(heap));
    return true;
}

bool DX12PersistentHeap::CreateDedicated(const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
    uint64_t allocationBytes, bool fallback, Allocation& outAllocation,
    std::string& outError)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT created = m_device->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(created))
    {
        m_policy.RecordAllocationFailure();
        outError = "DX12 persistent committed fallback 실패 " +
            DX12PersistentHeapHr(created);
        return false;
    }

    if (nullptr != debugName) resource->SetName(debugName);
    outAllocation.resource = std::move(resource);
    outAllocation.allocationBytes = allocationBytes;
    outAllocation.dedicated = true;
    m_policy.RecordDedicatedAllocation(allocationBytes, fallback);
    if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
        m_budgetCoordinator->RecordAllocation(m_budgetOwner,
            kRHIDeviceLocalMemoryBudgetDomain, allocationBytes);
    return true;
}

bool DX12PersistentHeap::CreateBuffer(const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
    Allocation& outAllocation, std::string& outError)
{
    if (D3D12_RESOURCE_DIMENSION_BUFFER != desc.Dimension || 0 == desc.Width)
    {
        outError = "DX12 persistent heap는 0이 아닌 buffer만 받는다";
        return false;
    }
    return CreateResource(desc, initialState, debugName,
        kDx12PersistentBufferCompatibilityKey,
        D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, outAllocation, outError);
}

bool DX12PersistentHeap::CreateTexture(const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
    Allocation& outAllocation, std::string& outError)
{
    if (D3D12_RESOURCE_DIMENSION_BUFFER == desc.Dimension || 0 == desc.Width ||
        0 == desc.Height || 1 != desc.SampleDesc.Count ||
        0 != (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
    {
        outError = "DX12 texture pool은 sample 1 non-RT/DS texture만 받는다";
        return false;
    }
    return CreateResource(desc, initialState, debugName,
        kDx12PersistentSampledTextureCompatibilityKey,
        D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES,
        outAllocation, outError);
}

bool DX12PersistentHeap::CreateResource(const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
    uint64_t compatibilityKey, D3D12_HEAP_FLAGS heapFlags,
    Allocation& outAllocation, std::string& outError)
{
    outAllocation = {};
    if (nullptr == m_device)
    {
        outError = "DX12 persistent heap가 초기화되지 않았다";
        return false;
    }

    const D3D12_RESOURCE_ALLOCATION_INFO info =
        m_device->GetResourceAllocationInfo(0, 1, &desc);
    if (0 == info.SizeInBytes || 0 == info.Alignment ||
        UINT64_MAX == info.SizeInBytes)
    {
        m_policy.RecordAllocationFailure();
        outError = "DX12 buffer allocation info가 무효다";
        return false;
    }

    if (m_policy.ShouldUseDedicated(info.SizeInBytes))
        return CreateDedicated(desc, initialState, debugName, info.SizeInBytes,
            false, outAllocation, outError);

    RHIPersistentHeapAllocation block = m_policy.Allocate(
        compatibilityKey, info.SizeInBytes, info.Alignment);
    bool fallback = false;
    if (!block.IsValid())
    {
        RefreshBudget();
        if (m_memoryPressure) TrimEmptySegments(true);
        const uint64_t segmentBytes = m_policy.ChooseSegmentBytes(
            info.SizeInBytes, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        const RHIPersistentHeapStats stats = m_policy.GetStats();
        const bool overBudget = 0 != m_softBudgetBytes &&
            (segmentBytes > m_softBudgetBytes ||
                stats.segmentBytes > m_softBudgetBytes - segmentBytes);
        if (0 == segmentBytes || overBudget)
        {
            fallback = true;
        }
        else
        {
            RHIDeviceMemoryGrowthTicket ticket{};
            if (nullptr != m_budgetCoordinator)
            {
                ticket = m_budgetCoordinator->TryReserveGrowth(m_budgetOwner,
                    kRHIDeviceLocalMemoryBudgetDomain, segmentBytes);
                if (!ticket.IsValid()) fallback = true;
            }
            RHIPersistentHeapSegmentHandle segment;
            std::string segmentError;
            if (!fallback && !CreateNativeSegment(compatibilityKey, heapFlags,
                segmentBytes, segment, segmentError))
            {
                if (nullptr != m_budgetCoordinator)
                    m_budgetCoordinator->CancelGrowth(ticket);
                fallback = true;
            }
            else if (!fallback)
            {
                if (nullptr != m_budgetCoordinator)
                    m_budgetCoordinator->CommitGrowth(ticket);
                block = m_policy.Allocate(compatibilityKey,
                    info.SizeInBytes, info.Alignment);
                if (!block.IsValid()) fallback = true;
            }
        }
    }

    if (fallback || !block.IsValid())
        return CreateDedicated(desc, initialState, debugName, info.SizeInBytes,
            true, outAllocation, outError);

    const auto native = m_segments.find(NativeKey(block.segment));
    if (native == m_segments.end())
    {
        m_policy.Release(block);
        m_policy.RecordAllocationFailure();
        outError = "DX12 persistent heap native segment를 찾지 못했다";
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT placed = m_device->CreatePlacedResource(native->second.Get(),
        block.offset, &desc, initialState, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(placed))
    {
        m_policy.Release(block);
        return CreateDedicated(desc, initialState, debugName, info.SizeInBytes,
            true, outAllocation, outError);
    }

    if (nullptr != debugName) resource->SetName(debugName);
    outAllocation.resource = std::move(resource);
    outAllocation.block = block;
    outAllocation.allocationBytes = info.SizeInBytes;
    return true;
}

void DX12PersistentHeap::RefreshBudget()
{
    if (m_budgetOverrideForTesting) return;

    RHIPersistentHeapBudget budget{};
    bool coordinatorPressure = false;
    if (nullptr != m_budgetCoordinator)
    {
        const RHIDeviceMemoryBudgetDecision decision =
            m_budgetCoordinator->GetDecision(kRHIDeviceLocalMemoryBudgetDomain);
        budget = decision.budget;
        coordinatorPressure = decision.memoryPressure;
    }
    else if (m_adapter)
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0,
            DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) && 0 != memory.Budget)
        {
            budget.usageBytes = memory.CurrentUsage;
            budget.budgetBytes = memory.Budget;
        }
    }

    const RHIPersistentHeapBudgetDecision decision = m_policy.UpdateBudget(budget);
    m_softBudgetBytes = decision.softBudgetBytes;
    m_memoryPressure = nullptr != m_budgetCoordinator
        ? coordinatorPressure : decision.memoryPressure;
}

RHIPersistentHeapStats DX12PersistentHeap::GetStats() const
{
    RHIPersistentHeapStats stats = m_policy.GetStats();
    if (m_budgetOverrideForTesting)
    {
        stats.softBudgetBytes = m_softBudgetBytes;
        stats.memoryPressure = m_memoryPressure;
    }
    return stats;
}

void DX12PersistentHeap::Release(Allocation& allocation)
{
    if (!allocation.IsValid()) return;
    allocation.resource.Reset();
    if (allocation.dedicated)
    {
        m_policy.RecordDedicatedRelease();
        if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
            m_budgetCoordinator->RecordRelease(m_budgetOwner,
                kRHIDeviceLocalMemoryBudgetDomain, allocation.allocationBytes);
    }
    else
        m_policy.Release(allocation.block);
    allocation = {};
}

uint64_t DX12PersistentHeap::TrimEmptySegments(bool force)
{
    const RHIPersistentHeapConfig config = m_policy.GetConfig();
    const auto removed = m_policy.TrimEmptySegments(
        force ? 0u : config.standbySegmentCountPerKey);
    uint64_t bytes = 0;
    for (const auto& handle : removed)
    {
        const auto native = m_segments.find(NativeKey(handle));
        if (native == m_segments.end()) continue;
        const D3D12_HEAP_DESC desc = native->second->GetDesc();
        bytes += desc.SizeInBytes;
        if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
            m_budgetCoordinator->RecordRelease(m_budgetOwner,
                kRHIDeviceLocalMemoryBudgetDomain, desc.SizeInBytes);
        m_segments.erase(native);
    }
    return bytes;
}

bool RunDX12PersistentHeapSelfTest(ID3D12Device* device,
    IDXGIAdapter1* adapter, std::string& outLog)
{
    RHIPersistentHeapConfig config{};
    config.defaultSegmentBytes = 4ull * 1024ull * 1024ull;
    config.dedicatedThresholdBytes = 2ull * 1024ull * 1024ull;
    config.standbySegmentCountPerKey = 1;

    DX12PersistentHeap heap;
    std::string error;
    RHIDeviceMemoryBudgetCoordinator coordinator;
    RHIPersistentHeapBudget snapshot{};
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    if (nullptr != adapter && SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))))
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,
            DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
        {
            snapshot.usageBytes = memory.CurrentUsage;
            snapshot.budgetBytes = memory.Budget;
        }
    }
    coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, snapshot);

    if (!heap.Initialize(device, adapter, &coordinator, error, config))
    {
        outLog += "[DX12 persistent heap] 초기화 실패: " + error + "\n";
        return false;
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 64 * 1024;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    DX12PersistentHeap::Allocation first;
    DX12PersistentHeap::Allocation second;
    bool passed = heap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
        L"PersistentHeapTest.First", first, error) &&
        heap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
            L"PersistentHeapTest.Second", second, error);
    passed = passed && !first.dedicated && !second.dedicated &&
        first.block.segment == second.block.segment &&
        first.block.offset != second.block.offset;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = 64;
    textureDesc.Height = 64;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    DX12PersistentHeap::Allocation textureA;
    DX12PersistentHeap::Allocation textureB;
    passed = passed && heap.CreateTexture(textureDesc, D3D12_RESOURCE_STATE_COMMON,
        L"PersistentHeapTest.TextureA", textureA, error) &&
        heap.CreateTexture(textureDesc, D3D12_RESOURCE_STATE_COMMON,
            L"PersistentHeapTest.TextureB", textureB, error) &&
        !textureA.dedicated && !textureB.dedicated &&
        textureA.block.segment == textureB.block.segment &&
        textureA.block.segment != first.block.segment;

    // 실제 mesh/texture cache처럼 별도 allocator가 같은 device coordinator를
    // 공유한다. 두 번째 allocator의 segment도 합산 성장 ticket을 받아야 한다.
    DX12PersistentHeap peerHeap;
    DX12PersistentHeap::Allocation peerBuffer;
    const uint64_t grantsBeforePeer = coordinator.GetStats().growthGrants;
    const bool peerInitialized = peerHeap.Initialize(device, adapter, &coordinator,
        error, config);
    const bool peerAllocated = peerInitialized && peerHeap.CreateBuffer(desc,
        D3D12_RESOURCE_STATE_COMMON, L"PersistentHeapTest.Peer", peerBuffer, error);
    const RHIDeviceMemoryBudgetCoordinatorStats sharedStats = coordinator.GetStats();
    passed = passed && peerAllocated && !peerBuffer.dedicated &&
        sharedStats.registeredOwners >= 2 &&
        sharedStats.growthGrants >= grantsBeforePeer + 1;
    peerHeap.Release(peerBuffer);
    peerHeap.TrimEmptySegments(true);
    peerHeap.Shutdown();

    heap.Release(first);
    heap.Release(second);
    heap.Release(textureA);
    heap.Release(textureB);
    const uint64_t trimmed = heap.TrimEmptySegments(true);
    passed = passed && trimmed >= config.defaultSegmentBytes * 2 &&
        0 == heap.GetStats().activeSegments;

    heap.SetBudgetForTesting(1, true);
    DX12PersistentHeap::Allocation fallback;
    passed = passed && heap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
        L"PersistentHeapTest.Fallback", fallback, error) && fallback.dedicated;
    heap.Release(fallback);
    const RHIPersistentHeapStats stats = heap.GetStats();
    passed = passed && 1 <= stats.segmentCreates && 1 <= stats.coalesces &&
        1 <= stats.trimmedSegments && 1 <= stats.dedicatedFallbacks &&
        0 == stats.livePooledAllocations && 0 == stats.liveDedicatedAllocations;

    heap.Shutdown();
    outLog += passed
        ? "[DX12 persistent heap] buffer/texture compatibility·placed·병합·empty trim·DXGI 단일 snapshot·multi-owner ticket·committed fallback 검증 통과\n"
        : "[DX12 persistent heap] backend 검증 실패: " + error + "\n";
    return passed;
}

#endif
