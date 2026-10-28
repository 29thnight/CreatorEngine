#pragma once
#include "../../../../Utility_Framework/Foundation/EngineFunc.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../../Tools/build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../../../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Platform/WindowsSDK/Creator.ComSupport.h"
#else
import winrt_base;
#endif
#include "FsrBackend.h"
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
#include "../../../Render/Temporal/ITemporalUpscaler.h"
#include "../../../Render/Temporal/ITemporalFrameGenerator.h"
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

class DX12DeviceResources;

FsrBackendDevice MakeFsrBackendDeviceDX12(ID3D12Device* device);
TemporalResult CreateFsrUpscalerDX12(DX12DeviceResources& resources, const FsrContextDescription& description,
    FsrGpuSynchronization synchronization, own::unique_owner<ITemporalUpscaler>& output);

struct FsrPlayerSwapchainDescriptionDX12
{
    HWND window{ nullptr };
    IDXGIFactory* factory{ nullptr };
    ID3D12CommandQueue* gameQueue{ nullptr };
    DXGI_SWAP_CHAIN_DESC1 description{};
    bool uiPremultipliedAlpha{ false };
};

// Private shell adapter. The neutral Prepare interface and native presenter are
// one object, so callbacks cannot outlive their contexts. Not used by Editor.
class FsrFrameGeneratorDX12 final : public ITemporalFrameGenerator
{
public:
    ~FsrFrameGeneratorDX12() override;
    TemporalResult Initialize(DX12DeviceResources&, const FsrContextDescription&,
        const TemporalFrameGenerationConfig&, const FsrPlayerSwapchainDescriptionDX12&, FsrGpuSynchronization);
    TemporalCapabilities GetCapabilities() const override;
    TemporalResult Prepare(const TemporalFrameGenerationInputs&, RHIEncoder&) override;
    // Caller submits the graph's real-frame commands before this method. The
    // serial baseline drains after Present, preserving pinned inputs until then.
    // Optional raw native-call observation, separate from the returned SDK drain result.
    TemporalResult Present(uint32_t syncInterval, uint32_t flags, TemporalResult* presentObservation = nullptr);
    TemporalResult DisableGeneration();
    TemporalResult PresentRealFrame(uint32_t syncInterval, uint32_t flags,
        TemporalResult* presentObservation = nullptr);
    TemporalResult Shutdown() override;
    IDXGISwapChain4* GetSwapchain() const { return m_swapchain.get(); } // Borrowed.
    uint32_t GetCompletedGeneratedFrameCount() const { return m_generator->GetCompletedGeneratedFrameCount(); }
#if CE_DEVELOPMENT && !CE_SHIPPING
    void SetDevelopmentEvaluationFailure(ce::owned_func<bool()> reject)
    {
        m_generator->SetDevelopmentEvaluationFailure(std::move(reject));
    }
#endif

private:
    DX12DeviceResources* m_resources{ nullptr };
    own::unique_owner<FsrFrameGenerator> m_generator{ own::make_unique<FsrFrameGenerator>() };
    winrt::com_ptr<IDXGISwapChain4> m_swapchain;
    struct FramePins
    {
        winrt::com_ptr<ID3D12Resource> resources[4];
        ce::lifetime_token lifetimeToken;
    };
    own::unique_owner<FramePins> m_lifetime;
    bool m_canPresent{ false };
    uint32_t m_uiFlags{ 0 };
    bool m_pending{ false };
};

// Shell retains the concrete owner for Present; renderer only sees its neutral
// ITemporalFrameGenerator base. Creation does not replace a live engine swapchain.
TemporalResult CreateFsrFrameGeneratorDX12(DX12DeviceResources&, const FsrContextDescription&,
    const TemporalFrameGenerationConfig&, const FsrPlayerSwapchainDescriptionDX12&, FsrGpuSynchronization,
    own::unique_owner<FsrFrameGeneratorDX12>& output);
#endif
