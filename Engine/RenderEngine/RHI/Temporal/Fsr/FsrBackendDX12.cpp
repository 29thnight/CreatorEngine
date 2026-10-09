#include "FsrBackendDX12.h"
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
#include "../../DX12/DX12DeviceResources.h"
#include "../../DX12/DX12Encoder.h"
#include <exception>

namespace
{
TemporalResult Invalid() { return { TemporalStatus::InvalidInput, 0 }; }

FfxResource Resolve(DX12DeviceResources& resources, RHITextureHandle handle, FfxResourceStates state)
{
    if (!handle.IsValid()) return {};
    auto* resource = resources.Resolve(handle);
    if (!resource) return {};
    const auto native = resource->GetDesc();
    if (native.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || native.DepthOrArraySize != 1 ||
        native.SampleDesc.Count != 1 || (state == FFX_RESOURCE_STATE_UNORDERED_ACCESS &&
        !(native.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))) return {};
    const auto description = ffxGetResourceDescriptionDX12(resource);
    if (description.format == FFX_SURFACE_FORMAT_UNKNOWN) return {};
    return ffxGetResourceDX12(resource, description, nullptr, state);
}

FfxResourceStates ReadState(ID3D12GraphicsCommandList* commandList)
{
    return commandList->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE
        ? FFX_RESOURCE_STATE_COMPUTE_READ : FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
}

class FsrUpscalerDX12 final : public ITemporalUpscaler
{
public:
    explicit FsrUpscalerDX12(DX12DeviceResources& resources)
        : m_resources(resources), m_upscaler(std::make_unique<FsrUpscaler>()) {}
    ~FsrUpscalerDX12() override
    {
        // A failed drain must preserve the SDK callback owner and GPU resources.
        if (!Shutdown().IsSuccess()) m_upscaler.release();
    }
    TemporalResult Initialize(const FsrContextDescription& description, FsrGpuSynchronization synchronization)
    {
        return m_upscaler->Initialize(MakeFsrBackendDeviceDX12(m_resources.GetDevice()), description, synchronization);
    }
    TemporalCapabilities GetCapabilities() const override
    {
        auto result = QueryFsrBuildAvailability(TemporalBackend::DX12);
        result.upscaling = { m_upscaler->IsInitialized() ? TemporalStatus::Success : TemporalStatus::NotInitialized, 0 };
        result.upscalerImplementation = "AMD FSR 3.1.4 source";
        return result;
    }
    TemporalResult Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder) override
    {
        auto result = ValidateTemporalUpscaleInputs(inputs);
        if (!result.IsSuccess()) return result;
        if (inputs.responsiveMask.IsValid()) return { TemporalStatus::FeatureUnsupported, 0 };
        auto* native = dynamic_cast<DX12Encoder*>(&encoder);
        if (!native || !native->UsesResources(&m_resources) || !native->GetCommandList()) return Invalid();
        auto* commandList = native->GetCommandList();
        if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT &&
            commandList->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE) return Invalid();
        const auto readState = ReadState(commandList);
        FsrUpscaleResources resources;
        resources.commandList = ffxGetCommandListDX12(commandList);
        resources.color = Resolve(m_resources, inputs.color, readState);
        resources.depth = Resolve(m_resources, inputs.depth, readState);
        resources.motionVectors = Resolve(m_resources, inputs.motionVectors, readState);
        resources.exposure = Resolve(m_resources, inputs.exposure, readState);
        resources.reactiveMask = Resolve(m_resources, inputs.reactiveMask, readState);
        resources.transparencyMask = Resolve(m_resources, inputs.transparencyMask, readState);
        resources.output = Resolve(m_resources, inputs.output, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        if ((inputs.exposure.IsValid() && !resources.exposure.resource) ||
            (inputs.reactiveMask.IsValid() && !resources.reactiveMask.resource) ||
            (inputs.transparencyMask.IsValid() && !resources.transparencyMask.resource)) return Invalid();
        result = m_upscaler->Dispatch(inputs.frame, resources);
        native->ResetState(commandList);
        return result;
    }
    TemporalResult Shutdown() override { return m_upscaler->Shutdown(); }

private:
    DX12DeviceResources& m_resources;
    std::unique_ptr<FsrUpscaler> m_upscaler;
};
}

FsrBackendDevice MakeFsrBackendDeviceDX12(ID3D12Device* device)
{
    if (!device) return {};
    return { TemporalBackend::DX12, ffxGetDeviceDX12(device), ffxGetScratchMemorySizeDX12(1),
        ffxGetInterfaceDX12, ffxWaitForPresents };
}

TemporalResult CreateFsrUpscalerDX12(DX12DeviceResources& resources, const FsrContextDescription& description,
    FsrGpuSynchronization synchronization, std::unique_ptr<ITemporalUpscaler>& output)
{
    if (output) return { TemporalStatus::AlreadyInitialized, 0 };
    auto candidate = std::make_unique<FsrUpscalerDX12>(resources);
    const auto result = candidate->Initialize(description, synchronization);
    if (result.IsSuccess()) output = std::move(candidate);
    return result;
}

FsrFrameGeneratorDX12::~FsrFrameGeneratorDX12()
{
    if (!Shutdown().IsSuccess())
    {
        m_generator.release();
        m_swapchain.Detach();
        m_lifetime.release();
    }
}

TemporalResult FsrFrameGeneratorDX12::Initialize(DX12DeviceResources& resources,
    const FsrContextDescription& description, const TemporalFrameGenerationConfig& configuration,
    const FsrPlayerSwapchainDescriptionDX12& native, FsrGpuSynchronization synchronization)
{
    if (m_swapchain || m_resources) return { TemporalStatus::AlreadyInitialized, 0 };
    auto result = ValidateTemporalFrameGenerationConfig(configuration);
    if (!result.IsSuccess()) return result;
    if (!native.window || !native.factory || !native.gameQueue || !resources.GetDevice() ||
        native.gameQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        native.description.Width != configuration.displayExtent.width ||
        native.description.Height != configuration.displayExtent.height || native.description.SampleDesc.Count != 1)
        return Invalid();
    if ((configuration.transferFunction == TemporalTransferFunction::PQ &&
        native.description.Format != DXGI_FORMAT_R10G10B10A2_UNORM) ||
        (configuration.transferFunction == TemporalTransferFunction::Linear &&
        native.description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)) return Invalid();
    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
    if (FAILED(native.gameQueue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != resources.GetDevice())
        return Invalid();
    result = m_generator->Initialize(MakeFsrBackendDeviceDX12(resources.GetDevice()), description, configuration,
        ffxGetSurfaceFormatDX12(native.description.Format), synchronization);
    if (!result.IsSuccess()) return result;
    FfxSwapchain swapchain = nullptr;
    result = FsrResult(ffxCreateFrameinterpolationSwapchainForHwndDX12(native.window, &native.description,
        nullptr, native.gameQueue, native.factory, swapchain));
    if (!result.IsSuccess() || !swapchain)
    {
        // Even failed SDK creation may return a partially-created COM object.
        if (swapchain) ffxGetDX12SwapchainPtr(swapchain)->Release();
        const auto cleanup = m_generator->Shutdown();
        return !cleanup.IsSuccess() ? cleanup : (result.IsSuccess() ? Invalid() : result);
    }
    m_swapchain.Attach(ffxGetDX12SwapchainPtr(swapchain));
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    if (configuration.transferFunction == TemporalTransferFunction::PQ)
    {
        colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }
    else if (configuration.transferFunction == TemporalTransferFunction::Linear)
    {
        colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    }
    UINT colorSupport = 0;
    HRESULT colorResult = m_swapchain->CheckColorSpaceSupport(colorSpace, &colorSupport);
    if (SUCCEEDED(colorResult) && (colorSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        colorResult = m_swapchain->SetColorSpace1(colorSpace);
    else if (SUCCEEDED(colorResult))
        colorResult = E_NOTIMPL;
    if (FAILED(colorResult))
    {
        const auto cleanup = Shutdown();
        return cleanup.IsSuccess() ? TemporalResult{ TemporalStatus::FeatureUnsupported, colorResult } : cleanup;
    }
    m_resources = &resources;
    m_uiFlags = native.uiPremultipliedAlpha ? FFX_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA : 0;
    return { TemporalStatus::Success, 0 };
}

TemporalCapabilities FsrFrameGeneratorDX12::GetCapabilities() const
{
    auto result = QueryFsrBuildAvailability(TemporalBackend::DX12);
    result.frameGeneration = { m_generator->IsInitialized() && m_swapchain ? TemporalStatus::Success
        : TemporalStatus::NotInitialized, 0 };
    result.maxInterpolatedFrames = result.frameGeneration.IsSuccess() ? 1 : 0;
    result.frameGeneratorImplementation = "AMD FSR 3.1.4 source / SDK DX12 replacement swapchain";
    return result;
}

TemporalResult FsrFrameGeneratorDX12::Prepare(const TemporalFrameGenerationInputs& inputs, RHIEncoder& encoder)
{
    if (!m_resources || !m_swapchain) return { TemporalStatus::NotInitialized, 0 };
    if (m_pending) return { TemporalStatus::IntegrationRequired, 0 };
    const auto validation = ValidateTemporalFrameGenerationInputs(inputs);
    if (!validation.IsSuccess()) return validation;
    auto* native = dynamic_cast<DX12Encoder*>(&encoder);
    if (!native || !native->UsesResources(m_resources) || !native->GetCommandList()) return Invalid();
    auto* commandList = native->GetCommandList();
    if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) return Invalid();
    const auto readState = ReadState(commandList);
    FsrFrameGenerationResources resources;
    resources.commandList = ffxGetCommandListDX12(commandList);
    resources.depth = Resolve(*m_resources, inputs.depth, readState);
    resources.motionVectors = Resolve(*m_resources, inputs.motionVectors, readState);
    resources.hudlessColor = Resolve(*m_resources, inputs.hudlessColor, readState);
    const auto ui = Resolve(*m_resources, inputs.uiColor, readState);
    if (inputs.uiColor.IsValid() && (!ui.resource || ui.description.width != inputs.frame.displayExtent.width ||
        ui.description.height != inputs.frame.displayExtent.height || ui.resource == resources.hudlessColor.resource ||
        ui.resource == resources.depth.resource || ui.resource == resources.motionVectors.resource)) return Invalid();
    m_lifetime = std::make_unique<FramePins>();
    m_lifetime->lifetimeToken = inputs.lifetimeToken;
    m_lifetime->resources[0] = static_cast<ID3D12Resource*>(resources.depth.resource);
    m_lifetime->resources[1] = static_cast<ID3D12Resource*>(resources.motionVectors.resource);
    m_lifetime->resources[2] = static_cast<ID3D12Resource*>(resources.hudlessColor.resource);
    m_lifetime->resources[3] = static_cast<ID3D12Resource*>(ui.resource);
    m_pending = true; // Also retain the lease after a partially-recorded SDK failure.
    m_canPresent = false;
    auto result = m_generator->Prepare(inputs.frame, resources);
    native->ResetState(commandList);
    if (!result.IsSuccess()) return result;
    result = FsrResult(ffxRegisterFrameinterpolationUiResourceDX12(ffxGetSwapchainDX12(m_swapchain.Get()), ui, m_uiFlags));
    if (result.IsSuccess()) result = m_generator->Configure(ffxGetSwapchainDX12(m_swapchain.Get()));
    m_canPresent = result.IsSuccess();
    return result;
}

TemporalResult FsrFrameGeneratorDX12::Present(uint32_t syncInterval, uint32_t flags)
{
    if (!m_swapchain || !m_pending || !m_canPresent) return { TemporalStatus::NotInitialized, 0 };
    if (syncInterval > 4 || (flags & ~DXGI_PRESENT_ALLOW_TEARING) ||
        ((flags & DXGI_PRESENT_ALLOW_TEARING) && syncInterval != 0)) return Invalid();
    m_canPresent = false;
    const auto present = m_swapchain->Present(syncInterval, flags);
    const auto complete = m_generator->FinishFrame();
    if (m_generator->HasPendingFrame()) return complete;
    m_lifetime.reset();
    m_pending = m_canPresent = false;
    if (FAILED(present)) return { TemporalStatus::SdkFailure, present };
    return complete;
}

TemporalResult FsrFrameGeneratorDX12::Shutdown()
{
    const auto result = m_generator->Shutdown();
    if (!result.IsSuccess()) return result;
    m_lifetime.reset();
    m_swapchain.Reset();
    m_resources = nullptr;
    m_pending = m_canPresent = false;
    return result;
}

TemporalResult CreateFsrFrameGeneratorDX12(DX12DeviceResources& resources, const FsrContextDescription& description,
    const TemporalFrameGenerationConfig& configuration, const FsrPlayerSwapchainDescriptionDX12& native,
    FsrGpuSynchronization synchronization, std::unique_ptr<FsrFrameGeneratorDX12>& output)
{
    if (output) return { TemporalStatus::AlreadyInitialized, 0 };
    auto candidate = std::make_unique<FsrFrameGeneratorDX12>();
    const auto result = candidate->Initialize(resources, description, configuration, native, synchronization);
    if (result.IsSuccess()) output = std::move(candidate);
    return result;
}
#endif
