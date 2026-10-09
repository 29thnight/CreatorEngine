#include "TemporalUpscalerHost.h"
#include "../../RHI/IRHIDeviceResources.h"
#include "../../RHI/DX12/DX12DeviceResources.h"
#include "../../RHI/Vulkan/VulkanDeviceResources.h"
#include "../../RHI/Temporal/Fsr/FsrBackendDX12.h"
#include "../../RHI/Temporal/Fsr/FsrBackendVulkan.h"
#include "../../RHI/Temporal/DlssTemporalAdapter.h"
#include "../../RHI/Temporal/XeSSUpscaler.h"
#include <atomic>
#include <array>

namespace
{
    std::atomic<uint32_t> g_nextViewport{ 1 };
    TemporalResult DrainTemporalRenderer(IRHIDeviceResources* resources)
    {
        std::string error;
        bool drained = false;
        if (auto* dx = dynamic_cast<DX12DeviceResources*>(resources))
            drained = dx->DrainForLifecycle(RHILifecycleCommand::PipelineRebuild, error) &&
                dx->GetLastLifecycleResult().command != RHILifecycleCommand::UnrecoverableDeviceError &&
                dx->GetLastLifecycleResult().IsClean() && dx->GetDevice() &&
                SUCCEEDED(dx->GetDevice()->GetDeviceRemovedReason());
        else if (auto* vk = dynamic_cast<VulkanDeviceResources*>(resources))
            drained = vk->DrainForLifecycle(RHILifecycleCommand::PipelineRebuild, error) &&
                vk->GetLastLifecycleResult().command != RHILifecycleCommand::UnrecoverableDeviceError &&
                vk->GetLastLifecycleResult().IsClean();
        return { drained ? TemporalStatus::Success : TemporalStatus::SdkFailure };
    }
#if CREATOR_ENABLE_FSR_SDK
    TemporalResult DrainFsrRenderer(void* context)
    { return DrainTemporalRenderer(static_cast<IRHIDeviceResources*>(context)); }
#endif
}
struct TemporalUpscalerHost::State
{
    IRHIDeviceResources* resources{ nullptr };
    TemporalBackend backend{ TemporalBackend::DX12 };
    TemporalRuntimeSettings settings;
    uint64_t generation{ UINT64_MAX };
    uint64_t viewId{ 0 }, sceneEpoch{ 0 };
    TemporalExtent display, render;
    TemporalProvider provider{ TemporalProvider::None };
    TemporalResult result{ TemporalStatus::NotQueried }, requestedResult{ TemporalStatus::NotQueried };
    std::vector<TemporalCapabilities> capabilities;
    std::unique_ptr<ITemporalUpscaler> adapter;
    std::shared_ptr<DlssTemporalAdapter> dlss;
    std::array<bool, 4> failed{};
    std::array<TemporalResult, 4> failures{};
    uint32_t viewportId{ g_nextViewport.fetch_add(1) };
    bool dirty{ false }, depthInverted{ false }, orthographic{ false };
};
TemporalUpscalerHost::TemporalUpscalerHost() : m_state(std::make_unique<State>()) {}
TemporalUpscalerHost::~TemporalUpscalerHost()
{
    // Do not release a context after unproven GPU retirement.
    if (!Shutdown().IsSuccess()) (void)m_state.release();
}
TemporalExtent TemporalUpscalerHost::RenderExtent() const { return m_state->render; }
TemporalProvider TemporalUpscalerHost::Provider() const { return m_state->provider; }
TemporalResult TemporalUpscalerHost::LastResult() const { return m_state->result; }
TemporalResult TemporalUpscalerHost::RequestedResult() const { return m_state->requestedResult; }
const std::vector<TemporalCapabilities>& TemporalUpscalerHost::Capabilities() const { return m_state->capabilities; }
TemporalResult TemporalUpscalerHost::Shutdown()
{
    auto& state = *m_state;
    if (state.adapter)
    {
        const auto result = state.adapter->Shutdown();
        if (!result.IsSuccess()) return result;
        state.adapter.reset();
    }
    state.dlss.reset();
    state.provider = TemporalProvider::None;
    state.dirty = true; // Explicit lifecycle shutdown requires reconfiguration even with identical settings.
    return { TemporalStatus::Success };
}
TemporalResult TemporalUpscalerHost::Configure(IRHIDeviceResources& resources, TemporalBackend backend,
    const TemporalRuntimeSettings& settings, uint64_t generation, TemporalExtent display, bool depthInverted,
    bool orthographic, uint64_t viewId, uint64_t sceneEpoch)
{
    auto& state = *m_state;
    state.viewId = viewId;
    state.sceneEpoch = sceneEpoch;
    if (!display.IsValid() || !resources.IsInitialized()) return { TemporalStatus::InvalidInput };
    const bool changed = state.resources != &resources || state.backend != backend ||
        !SameTemporalReconstructionSettings(state.settings, settings) || state.display != display ||
        state.depthInverted != depthInverted || state.orthographic != orthographic;
    if (!changed && !state.dirty)
    {
        state.generation = generation;
        state.settings = settings;
        return { TemporalStatus::Success };
    }
    const auto shutdown = Shutdown();
    if (!shutdown.IsSuccess()) return shutdown;
    // Only a reconstruction change may retry. Unrelated UI scalar updates must
    // neither drain this context nor revive a damaged provider automatically.
    if (changed)
    {
        state.failed = {};
        state.failures = {};
    }
    state.resources = &resources;
    state.backend = backend;
    state.settings = settings;
    state.generation = generation;
    state.display = state.render = display;
    state.depthInverted = depthInverted;
    state.orthographic = orthographic;
    state.dirty = false;
    state.capabilities.clear();
    auto fsr = QueryFsrBuildAvailability(backend);
    state.capabilities.push_back(fsr);
    TemporalCapabilities dlss;
    dlss.provider = TemporalProvider::Dlss;
    dlss.backend = backend;
    dlss.sdkVersion = "Streamline 2.14.1";
    dlss.sdkRevision = "2122257e0fce486f91b385aa63b9a09b0a34b363";
#if CREATOR_ENABLE_DLSS_STREAMLINE
    dlss.upscaling = dlss.frameGeneration = { backend == TemporalBackend::DX12 ?
        TemporalStatus::IntegrationRequired : TemporalStatus::BackendUnsupported };
#else
    dlss.upscaling = dlss.frameGeneration = { TemporalStatus::SdkNotBuilt };
#endif
    auto* dx = dynamic_cast<DX12DeviceResources*>(&resources);
    if (dx && dx->GetTemporalDlssSession()) dlss = dx->GetTemporalDlssSession()->QueryCapabilities();
    else if (dx && settings.requestedUpscaler == TemporalProvider::Dlss)
    {
        const auto bootstrap = dx->GetTemporalBootstrapResult();
        if (!bootstrap.IsSuccess() && bootstrap.status != TemporalStatus::NotQueried)
            dlss.upscaling = dlss.frameGeneration = bootstrap;
    }
    state.capabilities.push_back(dlss);
    TemporalCapabilities xess;
    xess.provider = TemporalProvider::XeSS;
    xess.backend = backend;
    xess.sdkVersion = kXeSSSdkVersion;
    xess.sdkRevision = kXeSSSdkRevision;
    xess.upscaling = { backend == TemporalBackend::DX12 ?
        (CREATOR_ENABLE_XESS_SDK ? TemporalStatus::NotQueried : TemporalStatus::SdkNotBuilt) :
        (CREATOR_ENABLE_XESS_VULKAN_SDK ? TemporalStatus::IntegrationRequired : TemporalStatus::SdkNotBuilt) };
    xess.frameGeneration = { backend == TemporalBackend::Vulkan ? TemporalStatus::BackendUnsupported :
        (CREATOR_ENABLE_XESS_SDK ? TemporalStatus::NotQueried : TemporalStatus::SdkNotBuilt) };
    if (auto* vk = dynamic_cast<VulkanDeviceResources*>(&resources))
    {
        const auto bootstrap = vk->GetTemporalXeSSBootstrapResult();
        if (!bootstrap.IsSuccess() && bootstrap.status != TemporalStatus::NotQueried)
            xess.upscaling = bootstrap;
    }
    state.capabilities.push_back(xess);
    state.result = state.requestedResult = { TemporalStatus::Success };
    if (!settings.enabled || settings.requestedUpscaler == TemporalProvider::None)
        return { TemporalStatus::Success };

    if (orthographic)
    {
        state.result = state.requestedResult = { TemporalStatus::ProjectionUnsupported };
        return { TemporalStatus::Success }; // Native rendering remains valid.
    }
    const auto create = [&](TemporalProvider provider) -> TemporalResult
    {
        if (state.failed[static_cast<size_t>(provider)]) return state.failures[static_cast<size_t>(provider)];
        if (provider == TemporalProvider::Fsr)
        {
#if CREATOR_ENABLE_FSR_SDK
            FsrContextDescription description;
            description.maxRenderExtent = display;
            description.displayExtent = display;
            description.depthInverted = depthInverted;
            const FsrGpuSynchronization sync{ &DrainFsrRenderer, &resources };
#if CREATOR_ENABLE_FSR_DX12_SDK
            if (backend == TemporalBackend::DX12 && dx)
                return CreateFsrUpscalerDX12(*dx, description, sync, state.adapter);
#endif
#if CREATOR_ENABLE_FSR_VULKAN_SDK
            if (auto* vk = dynamic_cast<VulkanDeviceResources*>(&resources); backend == TemporalBackend::Vulkan && vk)
                return CreateFsrUpscalerVulkan(*vk, description, sync, state.adapter);
#endif
#endif
            return QueryFsrBuildAvailability(backend).upscaling;
        }
        if (provider == TemporalProvider::Dlss)
        {
            if (!dx || backend != TemporalBackend::DX12) return { TemporalStatus::BackendUnsupported };
            state.dlss = dx->GetTemporalDlssSession();
            if (!state.dlss) return dlss.upscaling;
            if (!state.dlss->MatchesRuntimeConfiguration(settings.runtimeDirectory, settings.dlssProjectId))
                return { TemporalStatus::IntegrationRequired };
            return CreateDlssDX12Upscaler(*dx, state.dlss, state.viewportId, settings.quality,
                [&resources] { return DrainTemporalRenderer(&resources); }, state.adapter);
        }
        if (provider == TemporalProvider::XeSS)
        {
            XeSSUpscalerConfig config;
            config.displayExtent = display;
            config.quality = settings.quality;
            config.depthInverted = depthInverted;
            config.autoExposure = true;
            config.responsiveMask = true;
            if (dx && backend == TemporalBackend::DX12)
                return CreateXeSSUpscalerDX12(*dx, settings.runtimeDirectory.c_str(), config,
                    [&resources] { return DrainTemporalRenderer(&resources); }, state.adapter);
#if CREATOR_ENABLE_XESS_VULKAN_SDK
            auto* vk = dynamic_cast<VulkanDeviceResources*>(&resources);
            if (vk && vk->SupportsTemporalXeSS(settings.runtimeDirectory))
            {
                auto prepared = std::make_unique<XeSSUpscalerVulkan>();
                auto loaded = prepared->Load(settings.runtimeDirectory.c_str());
                if (!loaded.IsSuccess()) return loaded;
                return CreateXeSSUpscalerVulkan(*vk, prepared, config,
                    [&resources] { return DrainTemporalRenderer(&resources); }, state.adapter);
            }
#endif
            return xess.upscaling;
        }
        return { TemporalStatus::InvalidInput };
    };
    const std::array candidates{ settings.requestedUpscaler, TemporalProvider::Fsr };
    for (size_t i = 0; i != candidates.size(); ++i)
    {
        const auto provider = candidates[i];
        if (i && provider == settings.requestedUpscaler) continue;
        auto result = create(provider);
        if (result.IsSuccess() && !state.adapter) result = { TemporalStatus::SdkFailure };
        if (result.IsSuccess() && state.adapter)
        {
            TemporalExtent render;
            result = state.adapter->QueryRenderExtent(settings.quality, display, render);
            if (result.IsSuccess() && (!render.IsValid() || render.width > display.width || render.height > display.height))
            {
                result = { TemporalStatus::InvalidInput };
            }
            // Native AA is a full-resolution contract for every provider and
            // every fallback candidate, not just a label on an SR preset.
            if (result.IsSuccess() && settings.quality == TemporalQuality::NativeAA && render != display)
            {
                result = { TemporalStatus::InvalidInput };
            }
            if (result.IsSuccess()) state.render = render;
        }
#if CE_DEVELOPMENT && !CE_SHIPPING
        if (result.IsSuccess() && state.adapter && TemporalRuntimeControl::Get().ConsumeTestFault(
            settings.testFault, TemporalTestFaultMode::Capability, provider, 0, viewId, sceneEpoch))
        {
            // Reject actual SDK readiness, then traverse the same retirement,
            // failed-provider ledger and FSR/native selection as a real failure.
            result = { TemporalStatus::SdkFailure, -45001 };
        }
#endif
        for (auto& capability : state.capabilities)
            if (capability.provider == provider)
            {
                if (state.adapter && result.IsSuccess()) capability = state.adapter->GetCapabilities();
                capability.upscaling = result;
            }
        if (i == 0) state.result = state.requestedResult = result;
        if (result.IsSuccess() && state.adapter)
        {
            state.provider = provider;
            state.dirty = false;
            return { TemporalStatus::Success };
        }
        const auto retired = Shutdown();
        if (!retired.IsSuccess()) return retired;
        state.failed[static_cast<size_t>(provider)] = true;
        state.failures[static_cast<size_t>(provider)] = result;
    }
    // Native fallback reallocates the graph at display size before rendering.
    state.render = display;
    state.dirty = false;
    return { TemporalStatus::Success };
}
TemporalResult TemporalUpscalerHost::Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder)
{
    auto& state = *m_state;
    if (!state.adapter || inputs.frame.renderExtent != state.render || inputs.frame.displayExtent != state.display)
        return { TemporalStatus::InvalidInput };
    TemporalResult result{ TemporalStatus::Success };
    if (state.dlss) result = state.dlss->EnsureRealFrame(inputs.frame.realFrameId);
    auto providerInputs = inputs;
    if (state.provider != TemporalProvider::Fsr)
    {
        providerInputs.reactiveMask = {};
        providerInputs.transparencyMask = {};
    }
    if (state.provider != TemporalProvider::XeSS) providerInputs.responsiveMask = {};
    if (result.IsSuccess())
    {
#if CE_DEVELOPMENT && !CE_SHIPPING
        if (TemporalRuntimeControl::Get().ConsumeTestFault(state.settings.testFault,
            TemporalTestFaultMode::Dispatch, state.provider, inputs.frame.realFrameId, state.viewId, state.sceneEpoch))
        {
            // No SDK work is fabricated. The renderer discards this failed frame
            // and the next Configure enters the normal failed-provider fallback.
            result = { TemporalStatus::SdkFailure, -45002 };
        }
        else
#endif
        {
            result = state.adapter->Evaluate(providerInputs, encoder);
        }
    }
    state.result = result;
    if (!result.IsSuccess())
    {
        if (state.provider == state.settings.requestedUpscaler) state.requestedResult = result;
        for (auto& capability : state.capabilities)
            if (capability.provider == state.provider) capability.upscaling = result;
        state.failed[static_cast<size_t>(state.provider)] = true;
        state.failures[static_cast<size_t>(state.provider)] = result;
        state.dirty = true;
    }
    return result;
}
