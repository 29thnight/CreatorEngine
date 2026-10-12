#pragma once
#include "DlssTemporalAdapter.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// All engine DX12 resource owners participating in Streamline use one native
// device and session. Queues and resource tables remain independently owned.
// This avoids binding a process-global SDK to two unrelated D3D12 devices.
struct TemporalDeviceDX12
{
    std::shared_ptr<DlssTemporalAdapter> session;
    Microsoft::WRL::ComPtr<IDXGIFactory6> nativeFactory, factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> nativeDevice, device;
    ~TemporalDeviceDX12();
};
std::shared_ptr<TemporalDeviceDX12> AcquireTemporalDeviceDX12(uint32_t factoryFlags,
    LUID adapterLuid, TemporalResult& result);
bool HasTemporalDeviceDX12();
