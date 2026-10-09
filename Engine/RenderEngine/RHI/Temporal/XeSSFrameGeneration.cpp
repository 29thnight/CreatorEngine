#include "XeSSFrameGeneration.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
#include <dxgi1_6.h>
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xell/xell_d3d12.h>
#include "../DX12/DX12DeviceResources.h"
#include "../DX12/DX12Encoder.h"

namespace
{
TemporalResult FrameGenerationResult(xefg_swapchain_result_t result)
{
    if (result >= XEFG_SWAPCHAIN_RESULT_SUCCESS) return { TemporalStatus::Success, result };
    switch (result)
    {
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE:
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DRIVER:
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED:
    case XEFG_SWAPCHAIN_RESULT_ERROR_NOT_IMPLEMENTED:
    case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_UNSUPPORTED:
        return { TemporalStatus::FeatureUnsupported, result };
    case XEFG_SWAPCHAIN_RESULT_ERROR_CANT_LOAD_LIBRARY: return { TemporalStatus::RuntimeUnavailable, result };
    case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_FUNCTION_MISSING:
        return { TemporalStatus::SdkVersionMismatch, result };
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED:
    case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_CONTEXT: return { TemporalStatus::NotInitialized, result };
    case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT:
    case XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES:
    case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_OUTPUT_RESOURCES:
    case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_INPUT_RESOURCES: return { TemporalStatus::InvalidInput, result };
    default: return { TemporalStatus::SdkFailure, result };
    }
}

TemporalResult LatencyResult(xell_result_t result)
{
    if (result == XELL_RESULT_SUCCESS) return { TemporalStatus::Success, result };
    switch (result)
    {
    case XELL_RESULT_ERROR_UNSUPPORTED_DEVICE:
    case XELL_RESULT_ERROR_UNSUPPORTED_DRIVER:
    case XELL_RESULT_ERROR_UNSUPPORTED:
    case XELL_RESULT_ERROR_NOT_IMPLEMENTED: return { TemporalStatus::FeatureUnsupported, result };
    case XELL_RESULT_ERROR_UNINITIALIZED:
    case XELL_RESULT_ERROR_INVALID_CONTEXT: return { TemporalStatus::NotInitialized, result };
    case XELL_RESULT_ERROR_INVALID_ARGUMENT: return { TemporalStatus::InvalidInput, result };
    default: return { TemporalStatus::SdkFailure, result };
    }
}

bool ValidFrameId(uint64_t value) { return value != 0 && value <= UINT32_MAX; }

bool ValidCamera(const TemporalCamera& camera)
{
    const auto validMatrix = [](const auto& matrix)
    {
        bool nonzero = false;
        for (float value : matrix)
        {
            if (!std::isfinite(value)) return false;
            nonzero |= value != 0.0f;
        }
        return nonzero;
    };
    return camera.valid && validMatrix(camera.viewMatrix) && validMatrix(camera.projectionMatrix);
}

bool ValidResource(const XeSSDX12Texture& texture, TemporalExtent extent)
{
    if (!texture.resource || !extent.IsValid() || texture.state == UINT32_MAX) return false;
    const auto description = texture.resource->GetDesc();
    return description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && description.SampleDesc.Count == 1 &&
        description.DepthOrArraySize == 1 && texture.baseX <= description.Width &&
        extent.width <= description.Width - texture.baseX && texture.baseY <= description.Height &&
        extent.height <= description.Height - texture.baseY;
}

constexpr uint32_t MarkerBit(xell_latency_marker_type_t marker) { return 1u << static_cast<uint32_t>(marker); }
}
#endif

struct XeSSFrameGenerationDX12State
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    // Module order is incidental: successful Shutdown destroys FG, then XeLL,
    // and only then allows either module to unload.
    XeSSModule m_fgModule;
    XeSSModule m_latencyModule;
    xefg_swapchain_handle_t m_context{ nullptr };
    xell_context_handle_t m_latency{ nullptr };
    IDXGISwapChain3* m_proxy{ nullptr };
    decltype(&xefgSwapChainGetVersion) m_getVersion{ nullptr };
    decltype(&xellGetVersion) m_getLatencyVersion{ nullptr };
    xefg_swapchain_version_t m_runtimeVersion{};
    xell_version_t m_latencyVersion{};
    decltype(&xefgSwapChainD3D12CreateContext) m_create{ nullptr };
    decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) m_initialize{ nullptr };
    decltype(&xefgSwapChainD3D12GetSwapChainPtr) m_getProxy{ nullptr };
    decltype(&xefgSwapChainGetProperties) m_getProperties{ nullptr };
    decltype(&xefgSwapChainSetLatencyReduction) m_setLatency{ nullptr };
    decltype(&xefgSwapChainSetEnabled) m_setEnabled{ nullptr };
    decltype(&xefgSwapChainSetNumInterpolatedFrames) m_setCount{ nullptr };
    decltype(&xefgSwapChainSetUiCompositionState) m_setUiComposition{ nullptr };
    decltype(&xefgSwapChainD3D12TagFrameResource) m_tagResource{ nullptr };
    decltype(&xefgSwapChainTagFrameConstants) m_tagConstants{ nullptr };
    decltype(&xefgSwapChainSetPresentId) m_setPresentId{ nullptr };
    decltype(&xefgSwapChainGetLastPresentStatus) m_getPresentStatus{ nullptr };
    decltype(&xefgSwapChainDestroy) m_destroy{ nullptr };
    decltype(&xellD3D12CreateContext) m_createLatency{ nullptr };
    decltype(&xellSetSleepMode) m_setSleepMode{ nullptr };
    decltype(&xellSleep) m_sleep{ nullptr };
    decltype(&xellAddMarkerData) m_marker{ nullptr };
    decltype(&xellDestroyContext) m_destroyLatency{ nullptr };
    TemporalFrameGenerationConfig m_config;
    DXGI_FORMAT m_format{ DXGI_FORMAT_UNKNOWN };
    uint32_t m_swapchainFlags{ 0 };
    uint32_t m_maximumFrames{ 0 };
    bool m_initialized{ false };
    bool m_enabled{ false };
    bool m_latencyEnabled{ false };
    bool m_depthInverted{ false };
    bool m_reinitializeRequired{ false };
    bool m_forceReset{ true };
    bool m_prepared{ false };
    uint64_t m_sleepFrameId{ 0 };
    uint64_t m_lastSleepId{ 0 };
    uint64_t m_lastPresentedId{ 0 };
    uint64_t m_historyRevision{ 0 };
    TemporalExtent m_renderExtent;
    TemporalFrame m_pendingFrame;
    uint32_t m_markers{ 0 };
    std::function<TemporalResult()> m_drain;
    std::shared_ptr<const void> m_lifetimeToken;

    TemporalResult Drain()
    {
        if (!m_drain) return { TemporalStatus::IntegrationRequired };
        const auto result = m_drain();
        if (result.IsSuccess()) m_lifetimeToken.reset();
        return result;
    }

    TemporalResult Load(ID3D12Device* device, const wchar_t* directory)
    {
        if (!device) return { TemporalStatus::InvalidInput };
        auto result = m_fgModule.Load(directory, L"libxess_fg.dll");
        if (!result.IsSuccess()) return result;
        result = m_latencyModule.Load(directory, L"libxell.dll");
        if (!result.IsSuccess()) return result;
#define XESS_FG_RESOLVE(member, name) if (!m_fgModule.Resolve(member, #name)) return XeSSMissingExport()
        XESS_FG_RESOLVE(m_getVersion, xefgSwapChainGetVersion);
        XESS_FG_RESOLVE(m_create, xefgSwapChainD3D12CreateContext);
        XESS_FG_RESOLVE(m_initialize, xefgSwapChainD3D12InitFromSwapChainDesc);
        XESS_FG_RESOLVE(m_getProxy, xefgSwapChainD3D12GetSwapChainPtr);
        XESS_FG_RESOLVE(m_getProperties, xefgSwapChainGetProperties);
        XESS_FG_RESOLVE(m_setLatency, xefgSwapChainSetLatencyReduction);
        XESS_FG_RESOLVE(m_setEnabled, xefgSwapChainSetEnabled);
        XESS_FG_RESOLVE(m_setCount, xefgSwapChainSetNumInterpolatedFrames);
        XESS_FG_RESOLVE(m_setUiComposition, xefgSwapChainSetUiCompositionState);
        XESS_FG_RESOLVE(m_tagResource, xefgSwapChainD3D12TagFrameResource);
        XESS_FG_RESOLVE(m_tagConstants, xefgSwapChainTagFrameConstants);
        XESS_FG_RESOLVE(m_setPresentId, xefgSwapChainSetPresentId);
        XESS_FG_RESOLVE(m_getPresentStatus, xefgSwapChainGetLastPresentStatus);
        XESS_FG_RESOLVE(m_destroy, xefgSwapChainDestroy);
#undef XESS_FG_RESOLVE
#define XESS_LL_RESOLVE(member, name) if (!m_latencyModule.Resolve(member, #name)) return XeSSMissingExport()
        XESS_LL_RESOLVE(m_getLatencyVersion, xellGetVersion);
        XESS_LL_RESOLVE(m_createLatency, xellD3D12CreateContext);
        XESS_LL_RESOLVE(m_setSleepMode, xellSetSleepMode);
        XESS_LL_RESOLVE(m_sleep, xellSleep);
        XESS_LL_RESOLVE(m_marker, xellAddMarkerData);
        XESS_LL_RESOLVE(m_destroyLatency, xellDestroyContext);
#undef XESS_LL_RESOLVE
        result = FrameGenerationResult(m_getVersion(&m_runtimeVersion));
        if (!result.IsSuccess()) return result;
        result = LatencyResult(m_getLatencyVersion(&m_latencyVersion));
        if (!result.IsSuccess()) return result;
        // FG API 1.3 adds the MFG contract; its guide requires XeLL 1.3 or later.
        // Preserve patch compatibility without mistaking package 3.0.2 for either API.
        if (m_runtimeVersion.major != 1 || m_runtimeVersion.minor < 3 || m_runtimeVersion.patch >= 90 ||
            m_latencyVersion.major != 1 || m_latencyVersion.minor < 3 || m_latencyVersion.patch >= 90)
            return { TemporalStatus::SdkVersionMismatch };
        result = FrameGenerationResult(m_create(device, &m_context));
        if (!result.IsSuccess()) return result;
        xefg_swapchain_properties_t properties{};
        result = FrameGenerationResult(m_getProperties(m_context, &properties));
        if (!result.IsSuccess()) return result;
        m_maximumFrames = properties.maxSupportedInterpolations;
        if (m_maximumFrames == 0) return { TemporalStatus::FeatureUnsupported };
        result = LatencyResult(m_createLatency(device, &m_latency));
        if (!result.IsSuccess()) return result;
        return FrameGenerationResult(m_setLatency(m_context, m_latency));
    }

    TemporalResult Tag(ID3D12CommandList* commands, uint32_t frameId, const XeSSDX12Texture& texture,
        TemporalExtent extent, xefg_swapchain_resource_type_t type, XeSSResourceLifetime lifetime)
    {
        xefg_swapchain_d3d12_resource_data_t data{};
        data.type = type;
        data.validity = lifetime == XeSSResourceLifetime::OnlyNow ? XEFG_SWAPCHAIN_RV_ONLY_NOW
                                                                 : XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
        data.resourceBase = { texture.baseX, texture.baseY };
        data.resourceSize = { extent.width, extent.height };
        data.pResource = texture.resource;
        data.incomingState = static_cast<D3D12_RESOURCE_STATES>(texture.state);
        return FrameGenerationResult(m_tagResource(m_context, commands, frameId, &data));
    }
#endif
};

XeSSFrameGenerationDX12::XeSSFrameGenerationDX12() = default;
XeSSFrameGenerationDX12::~XeSSFrameGenerationDX12()
{
    // Preserve the context and both modules if shutdown cannot release a proxy
    // still referenced by the host. Unloading in that state is a use-after-free.
    if (!Shutdown().IsSuccess()) (void)m_state.release();
}

TemporalResult XeSSFrameGenerationDX12::QuerySupport(ID3D12Device* device, const wchar_t* runtimeDirectory,
    uint32_t& maximumInterpolatedFrames)
{
    maximumInterpolatedFrames = 0;
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    XeSSFrameGenerationDX12 probe;
    probe.m_state = std::make_unique<XeSSFrameGenerationDX12State>();
    const auto result = probe.m_state->Load(device, runtimeDirectory);
    const auto maximum = probe.m_state->m_maximumFrames;
    const auto destroyed = probe.Shutdown();
    if (!result.IsSuccess()) return result;
    if (!destroyed.IsSuccess()) return destroyed;
    maximumInterpolatedFrames = maximum;
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
    IDXGIFactory2* factory, void* window, const DXGI_SWAP_CHAIN_DESC1& description,
    const wchar_t* runtimeDirectory, const TemporalFrameGenerationConfig& config, bool depthInverted,
    std::function<TemporalResult()> drain, bool uiPremultiplied)
{
    const auto valid = ValidateTemporalFrameGenerationConfig(config);
    if (!valid.IsSuccess()) return valid;
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (m_state) return { TemporalStatus::AlreadyInitialized };
    if (!device || !queue || !factory || !window || !drain || description.Width != config.displayExtent.width ||
        description.Height != config.displayExtent.height || description.SampleDesc.Count != 1 ||
        queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return { TemporalStatus::InvalidInput };
    ID3D12Device* queueDevice = nullptr;
    const HRESULT queriedDevice = queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&queueDevice));
    const bool sameDevice = SUCCEEDED(queriedDevice) && queueDevice == device;
    if (queueDevice) queueDevice->Release();
    if (!sameDevice) return { TemporalStatus::InvalidInput, queriedDevice };
    m_state = std::make_unique<XeSSFrameGenerationDX12State>();
    auto& state = *m_state;
    state.m_drain = std::move(drain);
    auto result = state.Load(device, runtimeDirectory);
    if (result.IsSuccess() && config.interpolatedFrameCount > state.m_maximumFrames)
        result = { TemporalStatus::FeatureUnsupported };
    if (result.IsSuccess())
    {
        xefg_swapchain_d3d12_init_params_t params{};
        params.initFlags = depthInverted ? XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH : XEFG_SWAPCHAIN_INIT_FLAG_NONE;
        if (!uiPremultiplied) params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_UITEXTURE_NOT_PREMUL_ALPHA;
        params.maxInterpolatedFrames = state.m_maximumFrames;
        params.uiMode = XEFG_SWAPCHAIN_UI_MODE_HUDLESS_UITEXTURE;
        result = FrameGenerationResult(state.m_initialize(state.m_context, static_cast<HWND>(window),
            &description, nullptr, queue, factory, &params));
    }
    if (result.IsSuccess())
        result = FrameGenerationResult(state.m_getProxy(state.m_context, __uuidof(IDXGISwapChain3),
            reinterpret_cast<void**>(&state.m_proxy)));
    if (result.IsSuccess())
    {
        DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        if (config.transferFunction == TemporalTransferFunction::PQ)
            colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        else if (config.transferFunction == TemporalTransferFunction::Linear)
            colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        UINT support = 0;
        HRESULT native = state.m_proxy->CheckColorSpaceSupport(colorSpace, &support);
        if (SUCCEEDED(native) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            native = state.m_proxy->SetColorSpace1(colorSpace);
        else if (SUCCEEDED(native)) native = DXGI_ERROR_UNSUPPORTED;
        if (FAILED(native)) result = { TemporalStatus::FeatureUnsupported, native };
    }
    if (result.IsSuccess())
        result = FrameGenerationResult(state.m_setUiComposition(state.m_context,
            XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED));
    if (result.IsSuccess())
        result = FrameGenerationResult(state.m_setCount(state.m_context, config.interpolatedFrameCount));
    if (result.IsSuccess())
    {
        state.m_config = config;
        state.m_depthInverted = depthInverted;
        state.m_format = description.Format;
        state.m_swapchainFlags = description.Flags;
        state.m_initialized = true;
    }
    else (void)Shutdown();
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::SetEnabled(bool enabled)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    auto& state = *m_state;
    if (state.m_reinitializeRequired) return { TemporalStatus::IntegrationRequired };
    if (state.m_prepared) return { TemporalStatus::InvalidInput };
    if (state.m_enabled == enabled && state.m_latencyEnabled == enabled) return { TemporalStatus::Success };
    const auto drained = state.Drain();
    if (!drained.IsSuccess()) return drained;
    if (!enabled)
    {
        const auto result = FrameGenerationResult(state.m_setEnabled(state.m_context, 0));
        if (!result.IsSuccess()) return result;
        state.m_enabled = false;
    }
    xell_sleep_params_t sleep{};
    sleep.bLowLatencyMode = enabled ? 1u : 0u;
    auto result = LatencyResult(state.m_setSleepMode(state.m_latency, &sleep));
    if (!result.IsSuccess()) return result;
    state.m_latencyEnabled = enabled;
    if (enabled)
    {
        result = FrameGenerationResult(state.m_setEnabled(state.m_context, 1));
        if (!result.IsSuccess())
        {
            sleep.bLowLatencyMode = 0;
            if (state.m_setSleepMode(state.m_latency, &sleep) == XELL_RESULT_SUCCESS)
                state.m_latencyEnabled = false;
            return result;
        }
        state.m_enabled = true;
    }
    state.m_forceReset = true;
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::SetInterpolatedFrames(uint32_t count)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    auto& state = *m_state;
    if (state.m_reinitializeRequired) return { TemporalStatus::IntegrationRequired };
    if (count == 0 || count > state.m_maximumFrames || state.m_sleepFrameId)
        return { TemporalStatus::InvalidInput };
    if (count == state.m_config.interpolatedFrameCount) return { TemporalStatus::Success };
    const auto drained = state.Drain();
    if (!drained.IsSuccess()) return drained;
    const auto native = state.m_setCount(state.m_context, count);
    const auto result = FrameGenerationResult(native);
    if (result.IsSuccess())
    {
        state.m_config.interpolatedFrameCount = count;
        state.m_forceReset = true;
    }
    else if (native != XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT)
    {
        // SDK 3.0.2 explicitly requires reinitialization after other failures.
        state.m_reinitializeRequired = true;
        state.m_enabled = false;
    }
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::Sleep(uint64_t realFrameId)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    auto& state = *m_state;
    if (state.m_reinitializeRequired) return { TemporalStatus::IntegrationRequired };
    if (!ValidFrameId(realFrameId) || state.m_sleepFrameId || realFrameId <= state.m_lastSleepId)
        return { TemporalStatus::InvalidInput };
    const auto result = LatencyResult(state.m_sleep(state.m_latency, static_cast<uint32_t>(realFrameId)));
    if (result.IsSuccess())
    {
        state.m_sleepFrameId = realFrameId;
        state.m_lastSleepId = realFrameId;
        state.m_markers = 0;
        state.m_prepared = false;
    }
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::ReleaseInputsAfterGpuIdle()
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return {TemporalStatus::NotInitialized};
    if (m_state->m_prepared) return {TemporalStatus::IntegrationRequired};
    return m_state->Drain();
#else
    return {TemporalStatus::SdkNotBuilt};
#endif
}

TemporalResult XeSSFrameGenerationDX12::DiscardRealFrame(uint64_t realFrameId)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return {TemporalStatus::NotInitialized};
    auto& state = *m_state;
    if (!state.m_sleepFrameId) return {TemporalStatus::Success};
    if (realFrameId != state.m_sleepFrameId || state.m_prepared || state.m_lifetimeToken)
        return {TemporalStatus::IntegrationRequired};
    state.m_sleepFrameId = 0;
    state.m_markers = 0;
    state.m_forceReset = true;
    return {TemporalStatus::Success};
#else
    return {TemporalStatus::SdkNotBuilt};
#endif
}

TemporalResult XeSSFrameGenerationDX12::Mark(uint64_t realFrameId, XeSSLatencyMarker marker)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    auto& state = *m_state;
    if (!ValidFrameId(realFrameId) || realFrameId != state.m_sleepFrameId)
        return { TemporalStatus::InvalidInput };
    xell_latency_marker_type_t native{};
    switch (marker)
    {
    case XeSSLatencyMarker::SimulationStart: native = XELL_SIMULATION_START; break;
    case XeSSLatencyMarker::SimulationEnd: native = XELL_SIMULATION_END; break;
    case XeSSLatencyMarker::RenderSubmitStart: native = XELL_RENDERSUBMIT_START; break;
    case XeSSLatencyMarker::RenderSubmitEnd: native = XELL_RENDERSUBMIT_END; break;
    case XeSSLatencyMarker::InputSample: native = XELL_INPUT_SAMPLE; break;
    default: return { TemporalStatus::InvalidInput };
    }
    if ((state.m_markers & MarkerBit(native)) ||
        (native != XELL_SIMULATION_START && !(state.m_markers & MarkerBit(XELL_SIMULATION_START))) ||
        (native == XELL_RENDERSUBMIT_END && !(state.m_markers & MarkerBit(XELL_RENDERSUBMIT_START))))
        return { TemporalStatus::InvalidInput };
    const auto result = LatencyResult(state.m_marker(state.m_latency, static_cast<uint32_t>(realFrameId), native));
    if (result.IsSuccess()) state.m_markers |= MarkerBit(native);
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::Prepare(ID3D12CommandList* commands, const TemporalFrame& frame,
    const XeSSFrameGenerationDX12Bindings& bindings, std::shared_ptr<const void> lifetimeToken)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    auto& state = *m_state;
    if (state.m_reinitializeRequired) return { TemporalStatus::IntegrationRequired };
    if (!lifetimeToken || !ValidFrameId(frame.realFrameId) || frame.realFrameId != state.m_sleepFrameId)
        return { TemporalStatus::InvalidInput };
    if (!state.m_enabled) return { TemporalStatus::Success }; // Ordinary real-frame passthrough needs no temporal data.
    const auto valid = ValidateTemporalFrame(frame);
    if (!valid.IsSuccess()) return valid;
    const auto motionExtent = frame.motionVectorsAtDisplayResolution ? frame.displayExtent : frame.renderExtent;
    if (!ValidFrameId(frame.realFrameId) || frame.realFrameId != state.m_sleepFrameId || state.m_prepared ||
        frame.realFrameId <= state.m_lastPresentedId || !ValidCamera(frame.camera) ||
        frame.displayExtent != state.m_config.displayExtent || frame.depthInverted != state.m_depthInverted ||
        (frame.motionVectorsAtDisplayResolution && !frame.motionVectorsDilated) ||
        (bindings.lifetime != XeSSResourceLifetime::OnlyNow && bindings.lifetime != XeSSResourceLifetime::UntilPresent) ||
        (bindings.lifetime == XeSSResourceLifetime::OnlyNow && !commands) ||
        (commands && commands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) ||
        !ValidResource(bindings.depth, motionExtent) || !ValidResource(bindings.motionVectors, motionExtent) ||
        !ValidResource(bindings.hudlessColor, frame.displayExtent) || !ValidResource(bindings.uiColor, frame.displayExtent))
        return { TemporalStatus::InvalidInput };
    // Explicit HUDless+UI composition requires matching display format and nonaliasing textures.
    const ID3D12Resource* resources[] = { bindings.depth.resource, bindings.motionVectors.resource,
        bindings.hudlessColor.resource, bindings.uiColor.resource };
    for (std::size_t left = 0; left < 4; ++left)
        for (std::size_t right = left + 1; right < 4; ++right)
            if (resources[left] == resources[right]) return { TemporalStatus::InvalidInput };
    if (bindings.hudlessColor.resource->GetDesc().Format != state.m_format ||
        bindings.uiColor.resource->GetDesc().Format != state.m_format)
        return { TemporalStatus::InvalidInput };
    // Deliberately serial baseline: the prior owner survives until actual SDK
    // final consumption and tagging-copy completion, not CPU tagging/Present.
    if (state.m_lifetimeToken)
    {
        const auto drained = state.Drain();
        if (!drained.IsSuccess()) return drained;
    }
    state.m_lifetimeToken = std::move(lifetimeToken);
    const auto frameId = static_cast<uint32_t>(frame.realFrameId);
    auto result = state.Tag(commands, frameId, bindings.depth, motionExtent,
        XEFG_SWAPCHAIN_RES_DEPTH, bindings.lifetime);
    if (result.IsSuccess()) result = state.Tag(commands, frameId, bindings.motionVectors, motionExtent,
        XEFG_SWAPCHAIN_RES_MOTION_VECTOR, bindings.lifetime);
    if (result.IsSuccess()) result = state.Tag(commands, frameId, bindings.hudlessColor, frame.displayExtent,
        XEFG_SWAPCHAIN_RES_HUDLESS_COLOR, bindings.lifetime);
    if (result.IsSuccess()) result = state.Tag(commands, frameId, bindings.uiColor, frame.displayExtent,
        XEFG_SWAPCHAIN_RES_UI, bindings.lifetime);
    if (result.IsSuccess())
    {
        xefg_swapchain_frame_constant_data_t constants{};
        std::copy(frame.camera.viewMatrix.begin(), frame.camera.viewMatrix.end(), constants.viewMatrix);
        std::copy(frame.camera.projectionMatrix.begin(), frame.camera.projectionMatrix.end(), constants.projectionMatrix);
        constants.jitterOffsetX = frame.jitterX;
        constants.jitterOffsetY = frame.jitterY;
        constants.motionVectorScaleX = frame.motionVectorScaleX;
        constants.motionVectorScaleY = frame.motionVectorScaleY;
        constants.resetHistory = state.m_forceReset || frame.reset ||
            frame.historyRevision != state.m_historyRevision || frame.renderExtent != state.m_renderExtent;
        constants.frameRenderTime = frame.frameTimeMilliseconds;
        result = FrameGenerationResult(state.m_tagConstants(state.m_context, frameId, &constants));
    }
    if (result.IsSuccess())
    {
        state.m_pendingFrame = frame;
        state.m_prepared = true;
    }
    else state.m_forceReset = true; // A partial tag is not committed history.
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::Present(uint64_t realFrameId, uint32_t syncInterval, uint32_t flags,
    TemporalResult* presentObservation)
{
    if (presentObservation)
    {
        *presentObservation = {};
    }
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized)
    {
        return { TemporalStatus::NotInitialized };
    }
    auto& state = *m_state;
    const uint32_t required = MarkerBit(XELL_SIMULATION_START) | MarkerBit(XELL_SIMULATION_END) |
        MarkerBit(XELL_RENDERSUBMIT_START) | MarkerBit(XELL_RENDERSUBMIT_END);
    if (!ValidFrameId(realFrameId) || realFrameId != state.m_sleepFrameId || (state.m_enabled && !state.m_prepared) ||
        (state.m_markers & required) != required || (flags & DXGI_PRESENT_TEST) != 0 || syncInterval > 4)
    {
        return { TemporalStatus::InvalidInput };
    }
    const auto frameId = static_cast<uint32_t>(realFrameId);
    auto result = FrameGenerationResult(state.m_setPresentId(state.m_context, frameId));
    if (!result.IsSuccess())
    {
        return result;
    }
    result = LatencyResult(state.m_marker(state.m_latency, frameId, XELL_PRESENT_START));
    if (!result.IsSuccess())
    {
        return result;
    }
    const HRESULT presented = state.m_proxy->Present(syncInterval, flags);
    if (presentObservation)
    {
        *presentObservation = { presented == S_OK ? TemporalStatus::Success : SUCCEEDED(presented)
            ? TemporalStatus::NotInitialized : TemporalStatus::SdkFailure, presented };
    }
    const auto ended = LatencyResult(state.m_marker(state.m_latency, frameId, XELL_PRESENT_END));
    const bool hadPreparedFrame = state.m_prepared;
    state.m_prepared = false;
    state.m_sleepFrameId = 0;
    if (SUCCEEDED(presented))
    {
        state.m_lastPresentedId = realFrameId;
        if (hadPreparedFrame)
        {
            state.m_historyRevision = state.m_pendingFrame.historyRevision;
            state.m_renderExtent = state.m_pendingFrame.renderExtent;
        }
    }
    state.m_forceReset = !hadPreparedFrame || presented != S_OK || !ended.IsSuccess();
    if (FAILED(presented))
    {
        return { TemporalStatus::SdkFailure, presented };
    }
    return ended.IsSuccess() ? TemporalResult{ TemporalStatus::Success, presented } : ended;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::GetLastPresentStatus(XeSSPresentStatus& status) const
{
    status = {};
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    xefg_swapchain_present_status_t native{};
    const auto result = FrameGenerationResult(m_state->m_getPresentStatus(m_state->m_context, &native));
    if (result.IsSuccess() && result.nativeCode != XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS)
    {
        status.framesPresented = native.framesPresented;
        status.enabled = native.isFrameGenEnabled != 0;
        status.interpolation = FrameGenerationResult(native.frameGenResult);
    }
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSFrameGenerationDX12::Resize(TemporalExtent displayExtent)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state || !m_state->m_initialized) return { TemporalStatus::NotInitialized };
    if (!displayExtent.IsValid() || m_state->m_sleepFrameId) return { TemporalStatus::InvalidInput };
    const auto drained = m_state->Drain();
    if (!drained.IsSuccess()) return drained;
    const HRESULT result = m_state->m_proxy->ResizeBuffers(0, displayExtent.width, displayExtent.height,
        DXGI_FORMAT_UNKNOWN, m_state->m_swapchainFlags);
    if (FAILED(result)) return { TemporalStatus::SdkFailure, result };
    m_state->m_config.displayExtent = displayExtent;
    m_state->m_forceReset = true;
    return { TemporalStatus::Success, result };
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

IDXGISwapChain3* XeSSFrameGenerationDX12::GetSwapChain() const
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    return m_state ? m_state->m_proxy : nullptr;
#else
    return nullptr;
#endif
}

uint32_t XeSSFrameGenerationDX12::GetMaximumInterpolatedFrames() const
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    return m_state ? m_state->m_maximumFrames : 0;
#else
    return 0;
#endif
}

std::string XeSSFrameGenerationDX12::GetRuntimeVersion() const
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (m_state)
    {
        const auto& frameGeneration = m_state->m_runtimeVersion;
        const auto& latency = m_state->m_latencyVersion;
        return XeSSComponentVersion("XeSS-FG", frameGeneration.major, frameGeneration.minor, frameGeneration.patch) +
            "; " + XeSSComponentVersion("XeLL", latency.major, latency.minor, latency.patch);
    }
#endif
    return {};
}

TemporalResult XeSSFrameGenerationDX12::Shutdown()
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (m_state)
    {
        auto& state = *m_state;
        if (state.m_initialized || state.m_lifetimeToken)
        {
            const auto drained = state.Drain();
            if (!drained.IsSuccess()) return drained;
        }
        if (state.m_proxy)
        {
            state.m_initialized = false;
            state.m_proxy->Release();
            state.m_proxy = nullptr;
        }
        if (state.m_context)
        {
            const auto result = FrameGenerationResult(state.m_destroy(state.m_context));
            if (!result.IsSuccess()) return result;
            state.m_context = nullptr;
            state.m_initialized = false;
        }
        if (state.m_latency)
        {
            const auto result = LatencyResult(state.m_destroyLatency(state.m_latency));
            if (!result.IsSuccess()) return result;
            state.m_latency = nullptr;
        }
    }
#endif
    m_state.reset();
    return { TemporalStatus::Success };
}

#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
namespace
{
class XeSSFrameGeneratorBridgeDX12 final : public ITemporalFrameGenerator
{
public:
    XeSSFrameGeneratorBridgeDX12(DX12DeviceResources& resources, std::function<TemporalResult()> drain)
        : m_resources(resources), m_drain(std::move(drain)), m_adapter(std::make_unique<XeSSFrameGenerationDX12>()) {}
    ~XeSSFrameGeneratorBridgeDX12() override
    {
        if (!Shutdown().IsSuccess()) (void)m_adapter.release();
    }
    XeSSFrameGenerationDX12& GetAdapter() { return *m_adapter; }
    const std::function<TemporalResult()>& GetDrain() const { return m_drain; }
    TemporalCapabilities GetCapabilities() const override
    {
        TemporalCapabilities capabilities;
        capabilities.provider = TemporalProvider::XeSS;
        capabilities.backend = TemporalBackend::DX12;
        capabilities.sdkVersion = kXeSSSdkVersion;
        capabilities.sdkRevision = kXeSSSdkRevision;
        capabilities.frameGeneration = { m_adapter && m_adapter->GetSwapChain()
            ? TemporalStatus::Success : TemporalStatus::NotInitialized };
        capabilities.maxInterpolatedFrames = m_adapter ? m_adapter->GetMaximumInterpolatedFrames() : 0;
        if (m_adapter) capabilities.frameGeneratorImplementation = m_adapter->GetRuntimeVersion();
        return capabilities;
    }
    TemporalResult Prepare(const TemporalFrameGenerationInputs& inputs, RHIEncoder& encoder) override
    {
        if (!m_adapter) return { TemporalStatus::NotInitialized };
        const auto valid = ValidateTemporalFrameGenerationInputs(inputs);
        if (!valid.IsSuccess()) return valid;
        auto* native = dynamic_cast<DX12Encoder*>(&encoder);
        if (!native || !native->UsesResources(&m_resources) || !native->GetCommandList() ||
            native->GetCommandList()->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return { TemporalStatus::InvalidInput };
        XeSSFrameGenerationDX12Bindings bindings;
        const auto texture = [this](RHITextureHandle handle)
        {
            return XeSSDX12Texture{ m_resources.Resolve(handle), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
        };
        bindings.depth = texture(inputs.depth);
        bindings.motionVectors = texture(inputs.motionVectors);
        bindings.hudlessColor = texture(inputs.hudlessColor);
        bindings.uiColor = texture(inputs.uiColor);
        // ONLY_NOW asks the SDK to record copies into this graph callback's list.
        // Graph resource leases still last through queue completion; no cross-queue
        // or producer reuse is authorized by tagging or by Present returning.
        bindings.lifetime = XeSSResourceLifetime::OnlyNow;
        const auto result = m_adapter->Prepare(native->GetCommandList(), inputs.frame, bindings, inputs.lifetimeToken);
        native->ResetState(native->GetCommandList());
        return result;
    }
    TemporalResult Shutdown() override
    {
        if (!m_adapter) return { TemporalStatus::Success };
        const auto result = m_adapter->Shutdown();
        if (result.IsSuccess()) m_adapter.reset();
        return result;
    }
private:
    DX12DeviceResources& m_resources;
    std::function<TemporalResult()> m_drain;
    std::unique_ptr<XeSSFrameGenerationDX12> m_adapter;
};
}
#endif

TemporalResult CreateXeSSFrameGeneratorDX12(DX12DeviceResources& resources,
    ID3D12CommandQueue* queue, IDXGIFactory2* factory, void* window,
    const DXGI_SWAP_CHAIN_DESC1& description, const wchar_t* runtimeDirectory,
    const TemporalFrameGenerationConfig& config, bool depthInverted,
    std::function<TemporalResult()> drain, std::unique_ptr<ITemporalFrameGenerator>& frameGenerator,
    XeSSFrameGenerationDX12*& presentation)
{
    presentation = nullptr;
    if (frameGenerator) return { TemporalStatus::AlreadyInitialized };
    if (!drain) return { TemporalStatus::InvalidInput };
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    auto bridge = std::make_unique<XeSSFrameGeneratorBridgeDX12>(resources, std::move(drain));
    const auto result = bridge->GetAdapter().Initialize(resources.GetDevice(), queue, factory, window, description,
        runtimeDirectory, config, depthInverted, bridge->GetDrain());
    if (!result.IsSuccess()) return result;
    presentation = &bridge->GetAdapter();
    frameGenerator = std::move(bridge);
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}
