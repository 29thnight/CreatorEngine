#pragma once


#include "../RHIPersistentHeapPolicy.h"
#include "../RHIDeviceMemoryBudgetCoordinator.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <unordered_map>

/// DEFAULT buffer용 ID3D12Heap + placed resource adapter.
/// resource object는 Allocation이 소유하고, heap block은 resource를 먼저 놓은 뒤
/// 공통 policy로 반환한다.
class DX12PersistentHeap
{
public:
    struct Allocation
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        RHIPersistentHeapAllocation block;
        uint64_t allocationBytes{ 0 };
        bool dedicated{ false };

        Allocation() = default;
        Allocation(Allocation&&) noexcept = default;
        Allocation& operator=(Allocation&&) noexcept = default;
        Allocation(const Allocation&) = delete;
        Allocation& operator=(const Allocation&) = delete;

        bool IsValid() const { return nullptr != resource.Get(); }
    };

    bool Initialize(ID3D12Device* device, IDXGIAdapter1* adapter,
        RHIDeviceMemoryBudgetCoordinator* budgetCoordinator,
        std::string& outError,
        const RHIPersistentHeapConfig& config = RHIPersistentHeapConfig{});
    void Shutdown();

    bool CreateBuffer(const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
        Allocation& outAllocation, std::string& outError);
    bool CreateTexture(const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
        Allocation& outAllocation, std::string& outError);
    void Release(Allocation& allocation);

    uint64_t TrimEmptySegments(bool force);
    void RefreshBudget();
    bool IsMemoryPressure() const { return m_memoryPressure; }
    RHIPersistentHeapStats GetStats() const;

    void SetBudgetForTesting(uint64_t softBudgetBytes, bool memoryPressure)
    {
        m_softBudgetBytes = softBudgetBytes;
        m_memoryPressure = memoryPressure;
        m_budgetOverrideForTesting = true;
    }
    void ClearBudgetOverrideForTesting()
    {
        m_softBudgetBytes = 0;
        m_memoryPressure = false;
        m_budgetOverrideForTesting = false;
        RefreshBudget();
    }

private:
    static uint64_t NativeKey(const RHIPersistentHeapSegmentHandle& handle)
    {
        return (static_cast<uint64_t>(handle.generation) << 32) | handle.slot;
    }

    bool CreateDedicated(const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
        uint64_t allocationBytes, bool fallback, Allocation& outAllocation,
        std::string& outError);
    bool CreateNativeSegment(uint64_t compatibilityKey,
        D3D12_HEAP_FLAGS heapFlags, uint64_t bytes,
        RHIPersistentHeapSegmentHandle& outHandle, std::string& outError);
    bool CreateResource(const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState, const wchar_t* debugName,
        uint64_t compatibilityKey, D3D12_HEAP_FLAGS heapFlags,
        Allocation& outAllocation, std::string& outError);

    ID3D12Device* m_device{ nullptr };
    Microsoft::WRL::ComPtr<IDXGIAdapter3> m_adapter;
    RHIPersistentHeapPolicy m_policy;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12Heap>> m_segments;
    RHIDeviceMemoryBudgetCoordinator* m_budgetCoordinator{ nullptr };
    RHIDeviceMemoryBudgetOwner m_budgetOwner{ 0 };
    uint64_t m_softBudgetBytes{ 0 };
    bool m_memoryPressure{ false };
    bool m_budgetOverrideForTesting{ false };
};

bool RunDX12PersistentHeapSelfTest(ID3D12Device* device,
    IDXGIAdapter1* adapter, std::string& outLog);

