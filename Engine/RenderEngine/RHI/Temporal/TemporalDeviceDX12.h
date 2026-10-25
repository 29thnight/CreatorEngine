#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.ComSupport.h"
#else
import winrt_base;
#endif
#include "DlssTemporalAdapter.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.GraphicsSDK.h"
#else
import "Creator.GraphicsSDK.h";
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

// All engine DX12 resource owners participating in Streamline use one native
// device and session. Queues and resource tables remain independently owned.
// This avoids binding a process-global SDK to two unrelated D3D12 devices.
struct TemporalDeviceDX12
{
    own::shared_owner<DlssTemporalAdapter> session;
    winrt::com_ptr<IDXGIFactory6> nativeFactory, factory;
    winrt::com_ptr<IDXGIAdapter1> adapter;
    winrt::com_ptr<ID3D12Device> nativeDevice, device;
    ~TemporalDeviceDX12();
};
own::shared_owner<TemporalDeviceDX12> AcquireTemporalDeviceDX12(uint32_t factoryFlags,
    LUID adapterLuid, TemporalResult& result);
bool HasTemporalDeviceDX12();
