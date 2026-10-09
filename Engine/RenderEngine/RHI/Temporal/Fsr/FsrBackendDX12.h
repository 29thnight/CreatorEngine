#pragma once
#include "FsrBackend.h"
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
#include "../../../Render/Temporal/ITemporalUpscaler.h"
#include "../../../Render/Temporal/ITemporalFrameGenerator.h"
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <wrl/client.h>

class DX12DeviceResources;

FsrBackendDevice MakeFsrBackendDeviceDX12(ID3D12Device* device);
TemporalResult CreateFsrUpscalerDX12(DX12DeviceResources& resources, const FsrContextDescription& description,
    FsrGpuSynchronization synchronization, std::unique_ptr<ITemporalUpscaler>& output);

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
    IDXGISwapChain4* GetSwapchain() const { return m_swapchain.Get(); } // Borrowed.
    uint32_t GetCompletedGeneratedFrameCount() const { return m_generator->GetCompletedGeneratedFrameCount(); }
#if CE_DEVELOPMENT && !CE_SHIPPING
    void SetDevelopmentEvaluationFailure(std::function<bool()> reject)
    {
        m_generator->SetDevelopmentEvaluationFailure(std::move(reject));
    }
#endif

private:
    DX12DeviceResources* m_resources{ nullptr };
    std::unique_ptr<FsrFrameGenerator> m_generator{ std::make_unique<FsrFrameGenerator>() };
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapchain;
    struct FramePins
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resources[4];
        std::shared_ptr<const void> lifetimeToken;
    };
    std::unique_ptr<FramePins> m_lifetime;
    bool m_canPresent{ false };
    uint32_t m_uiFlags{ 0 };
    bool m_pending{ false };
};

// Shell retains the concrete owner for Present; renderer only sees its neutral
// ITemporalFrameGenerator base. Creation does not replace a live engine swapchain.
TemporalResult CreateFsrFrameGeneratorDX12(DX12DeviceResources&, const FsrContextDescription&,
    const TemporalFrameGenerationConfig&, const FsrPlayerSwapchainDescriptionDX12&, FsrGpuSynchronization,
    std::unique_ptr<FsrFrameGeneratorDX12>& output);
#endif
