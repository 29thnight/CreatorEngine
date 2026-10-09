#include "PlayerTemporalDX12.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/Temporal/DlssTemporalAdapter.h"
#include "RHI/Temporal/AntiLag2DX12Adapter.h"
#include "RHI/Temporal/Fsr/FsrBackendDX12.h"
#include "RHI/Temporal/XeSSFrameGeneration.h"
#include "Render/Temporal/TemporalRuntimeControl.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <wrl/client.h>

namespace Player
{
namespace
{
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
constexpr auto kReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
bool Report(TemporalResult result, const char* operation, std::string& error)
{
    if (result.IsSuccess()) return true;
    error = std::string(operation) + ": temporal status=" +
        std::to_string(static_cast<unsigned>(result.status)) + " native=" + std::to_string(result.nativeCode);
    return false;
}
}

struct TemporalDX12::State
{
    explicit State(DX12DeviceResources& value) : resources(value) {}
    DX12DeviceResources& resources;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopped{false}, failed{false}, prepared{false}, readState{false};
    bool suspended{false}, renderSubmitEnded{false}, presentationReady{false}, pendingRecovery{false};
    bool sleepInProgress{false}, reflexSleepSupported{false}, reflexMarkersSupported{false};
    bool reflexFaulted{false}, reflexConflict{false};
    HWND window{nullptr};
    bool sdkPresented{false}, sdkConsumed{false}, markerFailure{false}, recordingSubmitted{false};
    uint64_t generation{UINT64_MAX}, latencyFrame{0}, lastPresented{0};
    uint64_t reportedDisplayFrames{0};
    TemporalMeasurementProvenance openedRealFrame;
    TemporalProvider selected{TemporalProvider::None};
    TemporalFrameGenerationConfig configuration;
    TemporalRuntimeSettings settings;
    TemporalResult requestedResult;
    std::vector<TemporalCapabilities> capabilities;
    std::unique_ptr<ITemporalFrameGenerator> generator;
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    FsrFrameGeneratorDX12* fsr{nullptr}; // Borrowed from generator.
#endif
    XeSSFrameGenerationDX12* xess{nullptr}; // Borrowed from generator.
    std::shared_ptr<DlssTemporalAdapter> dlss;
    AntiLag2DX12Adapter antiLag;
    bool antiLagActive{false};
    std::array<bool, 4> failedProviders{};
    std::string failureReason;
    ComPtr<IDXGISwapChain3> dlssSwapchain;
    HANDLE finalEvent{nullptr};
    struct Pins
    {
        RHITemporalDisplayPacket packet;
        TemporalProvider presenterProvider{ TemporalProvider::None };
        uint32_t presenterInterpolatedFrameCount{ 0 };
        uint64_t presenterGeneration{ 0 };
        std::array<ComPtr<ID3D12Resource>, 4> textures;
        std::array<RHITextureHandle, 4> handles{};
    };
    std::shared_ptr<Pins> pins;

    bool UsesDlssToken() const
    {
        return dlss && (selected == TemporalProvider::Dlss || reflexSleepSupported || reflexMarkersSupported);
    }

    void PublishLatency(TemporalResult result)
    {
        auto reflex = dlss ? dlss->QueryLatencyState() : TemporalLatencyState{};
        if (!dlss)
        {
#if CREATOR_ENABLE_DLSS_STREAMLINE
            const auto bootstrap = resources.GetTemporalBootstrapResult();
            const auto unavailable = !bootstrap.IsSuccess() && bootstrap.status != TemporalStatus::NotQueried
                ? bootstrap : TemporalResult{TemporalStatus::IntegrationRequired};
#else
            const TemporalResult unavailable{TemporalStatus::SdkNotBuilt};
#endif
            reflex.support = reflex.sleepSupport = reflex.markerSupport = reflex.optionsResult = unavailable;
        }
        reflex.requestedMode = settings.reflexMode;
        if (reflexConflict || reflexFaulted)
        {
            reflex.optionsResult = {TemporalStatus::IntegrationRequired};
        }
        TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot)
        {
            snapshot.reflex = reflex;
            snapshot.latencyProvider = UsesDlssToken() ? "reflex" : xess ? "xell" :
                antiLagActive ? "anti-lag-2" : "";
            snapshot.latencyResult = result;
        });
    }

    IDXGISwapChain3* Proxy() const
    {
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
        if (fsr) return fsr->GetSwapchain();
#endif
        if (xess) return xess->GetSwapChain();
        return dlssSwapchain.Get();
    }

    bool QueueIdle(std::string& error)
    {
        // This is a fresh lifecycle signal after proxy Present, executed by the
        // existing RHI submission owner. XeSS 3.0.2 submits interpolation on its
        // supplied game queue; FSR waits ffxWaitForPresents before this callback.
        // A pre-Present host fence would not cover either SDK's added queue work.
        // DrainForLifecycle also waits queued CPU submissions. Do not require
        // an old frame ticket: initialization has none and each successful drain
        // intentionally retires the preceding tickets.
        if (!resources.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error) ||
            resources.GetLastLifecycleResult().command == RHILifecycleCommand::UnrecoverableDeviceError ||
            !resources.GetLastLifecycleResult().IsClean() ||
            FAILED(resources.GetDevice()->GetDeviceRemovedReason())) return false;
        return true;
    }

    bool RestoreInputs(std::string& error)
    {
        if (!pins || !readState) return true;
        if (!recordingSubmitted)
        {
            // A failed EndFrame can be ambiguous. Never guess which barriers ran.
            pins->packet.consumerLease->m_completionLost.store(true, std::memory_order_release);
            return true;
        }
        if (!resources.BeginFrame(error)) return false;
        std::array<D3D12_RESOURCE_BARRIER, 4> barriers{};
        for (size_t index = 0; index < barriers.size(); ++index)
        {
            barriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[index].Transition = {pins->textures[index].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                kReadState, D3D12_RESOURCE_STATE_COPY_DEST};
        }
        resources.GetCommandList()->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        if (!resources.EndFrame(error) || !QueueIdle(error)) return false;
        readState = false;
        return true;
    }

    bool Drain(std::string& error)
    {
        // FSR's adapter waits ffxWaitForPresents before invoking QueueIdle.
        // Streamline exposes a distinct SDK input fence, which is mandatory.
        if (selected == TemporalProvider::Dlss && dlss && sdkPresented &&
            (!pins || !pins->packet.nativeGateActive))
        {
            DlssFrameGenerationState status;
            if (!Report(dlss->GetFrameGenerationState(0, status), "DLSS final input fence", error)) return false;
            reportedDisplayFrames += status.presentedFramesSinceLastQuery;
            auto* fence = static_cast<ID3D12Fence*>(status.inputsCompletionFence);
            if (!fence || !status.inputsCompletionValue)
            {
                error = "DLSS proxy did not provide its final input-consumption fence";
                return false;
            }
            if (!finalEvent) finalEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!finalEvent || FAILED(fence->SetEventOnCompletion(status.inputsCompletionValue, finalEvent)) ||
                WaitForSingleObject(finalEvent, 5000) != WAIT_OBJECT_0 ||
                fence->GetCompletedValue() == UINT64_MAX || fence->GetCompletedValue() < status.inputsCompletionValue)
            {
                error = "DLSS final input-consumption fence did not complete";
                return false;
            }
        }
        return QueueIdle(error);
    }

    static void PublishPresent(const TemporalMeasurementProvenance& source, uint64_t frameId,
        TemporalProvider provider, uint32_t interpolatedFrameCount, bool nativeGateActive,
        TemporalResult result, uint64_t presenterGeneration)
    {
        TemporalPresenterObservation observation;
        observation.realFrameId = frameId;
        observation.publicationFrameId = source.publicationFrameId;
        observation.viewId = source.viewId;
        observation.sceneEpoch = source.sceneEpoch;
        observation.requestGeneration = source.requestedGeneration;
        observation.playerObservedGeneration = presenterGeneration;
        observation.provider = provider;
        observation.interpolatedFrameCount = provider != TemporalProvider::None ? interpolatedFrameCount : 0;
        observation.faultMode = static_cast<TemporalTestFaultMode>(source.testFaultMode);
        observation.faultRevision = source.testFaultRevision;
        observation.nativeGateActive = nativeGateActive;
        observation.result = result;
        // Fault-armed provenance deliberately fails measurement IsValid(). It
        // still identifies real rendered pixels and must not block presentation.
        observation.valid = frameId != 0 && frameId == source.realFrameId &&
            source.publicationFrameId != 0 && source.viewId != 0 && source.sceneEpoch != 0 &&
            source.testFaultMode <= static_cast<uint8_t>(TemporalTestFaultMode::Dispatch);
        TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot)
        {
            snapshot.presenterObservation = observation;
            snapshot.cpuPresentReturnedFrameId = observation.valid && result.status != TemporalStatus::NotQueried
                ? frameId : 0;
            snapshot.activeFrameGenerator = observation.valid && result.IsSuccess()
                ? observation.provider : TemporalProvider::None;
            snapshot.activeInterpolatedFrameCount = snapshot.activeFrameGenerator != TemporalProvider::None
                ? observation.interpolatedFrameCount : 0;
        });
    }

    void Publish(TemporalResult result, const std::string& diagnostic = {})
    {
        TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot)
        {
            snapshot.playerObservedGeneration = generation;
            snapshot.presentationTarget = TemporalPresentationTarget::PlayerSwapchain;
            snapshot.requestedFrameGenerationResult = requestedResult;
            snapshot.selectedFrameGenerator = selected;
            snapshot.lastFrameGenerationResult = result;
            snapshot.activeFrameGenerator = sdkPresented && result.IsSuccess() &&
                (!pins || !pins->packet.nativeGateActive) ? selected : TemporalProvider::None;
            snapshot.configuredInterpolatedFrameCount = generator && selected != TemporalProvider::None
                ? configuration.interpolatedFrameCount : 0;
            snapshot.activeInterpolatedFrameCount = snapshot.activeFrameGenerator != TemporalProvider::None
                ? snapshot.configuredInterpolatedFrameCount : 0;
            for (const auto& candidate : capabilities)
            {
                auto found = std::find_if(snapshot.capabilities.begin(), snapshot.capabilities.end(),
                    [&](const auto& existing) { return existing.provider == candidate.provider &&
                        existing.backend == candidate.backend; });
                if (found == snapshot.capabilities.end()) snapshot.capabilities.push_back(candidate);
                else
                {
                    found->frameGeneration = candidate.frameGeneration;
                    found->maxInterpolatedFrames = candidate.maxInterpolatedFrames;
                    found->frameGeneratorImplementation = candidate.frameGeneratorImplementation;
                }
            }
            if (!diagnostic.empty()) snapshot.diagnostic = failureReason.empty() ? diagnostic : failureReason + "; " + diagnostic;
        });
    }

    bool CancelUnusedToken()
    {
        if (!latencyFrame || pins || prepared || sleepInProgress) { return false; }
        TemporalResult result{TemporalStatus::Success};
        if (UsesDlssToken()) { result = dlss->DiscardRealFrame(latencyFrame); }
        if (xess) result = xess->DiscardRealFrame(latencyFrame);
        if (!result.IsSuccess()) return false;
        latencyFrame = 0;
        wake.notify_all();
        return true;
    }

    bool Mark(uint64_t frameId, RHITemporalLatencyMarker marker)
    {
        if (!latencyFrame || frameId != latencyFrame) return false;
        TemporalResult result{TemporalStatus::Success};
        if (selected == TemporalProvider::Fsr && antiLagActive && marker == RHITemporalLatencyMarker::RenderSubmitEnd)
        {
            result = antiLag.EndRendering();
            TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot) { snapshot.latencyResult = result; });
            // Optional latency support cannot turn an otherwise valid FG frame
            // into a fabricated marker success or a fatal presentation failure.
            if (!result.IsSuccess()) antiLagActive = false;
        }
        if (reflexMarkersSupported)
        {
            DlssLatencyMarker native;
            switch (marker)
            {
            case RHITemporalLatencyMarker::InputSample: native = DlssLatencyMarker::InputSample; break;
            case RHITemporalLatencyMarker::SimulationStart: native = DlssLatencyMarker::SimulationStart; break;
            case RHITemporalLatencyMarker::SimulationEnd: native = DlssLatencyMarker::SimulationEnd; break;
            case RHITemporalLatencyMarker::RenderSubmitStart: native = DlssLatencyMarker::RenderSubmitStart; break;
            case RHITemporalLatencyMarker::RenderSubmitEnd: native = DlssLatencyMarker::RenderSubmitEnd; break;
            default: return false;
            }
            result = dlss->MarkLatency(frameId, native);
        }
        if (xess)
        {
            XeSSLatencyMarker native;
            switch (marker)
            {
            case RHITemporalLatencyMarker::InputSample: native = XeSSLatencyMarker::InputSample; break;
            case RHITemporalLatencyMarker::SimulationStart: native = XeSSLatencyMarker::SimulationStart; break;
            case RHITemporalLatencyMarker::SimulationEnd: native = XeSSLatencyMarker::SimulationEnd; break;
            case RHITemporalLatencyMarker::RenderSubmitStart: native = XeSSLatencyMarker::RenderSubmitStart; break;
            case RHITemporalLatencyMarker::RenderSubmitEnd: native = XeSSLatencyMarker::RenderSubmitEnd; break;
            default: return false;
            }
            result = xess->Mark(frameId, native);
        }
        markerFailure |= !result.IsSuccess() && (selected == TemporalProvider::Dlss || xess);
        PublishLatency(result);
        if (marker == RHITemporalLatencyMarker::RenderSubmitEnd)
        {
            renderSubmitEnded = true;
            if (suspended) CancelUnusedToken();
        }
        return result.IsSuccess();
    }
};

TemporalDX12::TemporalDX12(DX12DeviceResources& resources) : m_state(std::make_unique<State>(resources)) {}
TemporalDX12::~TemporalDX12()
{
    std::string error;
    if (!Shutdown(error)) (void)m_state.release(); // GPU/SDK work must never outlive its code/pins.
}
bool TemporalDX12::HasProxy() const { return m_state->Proxy() != nullptr; }
bool TemporalDX12::RequiresLatencyMarkers() const
{
    std::lock_guard lock(m_state->mutex);
    return m_state->reflexMarkersSupported || m_state->xess;
}
void TemporalDX12::CommitConfiguration()
{
    std::lock_guard lock(m_state->mutex);
    m_state->presentationReady = true;
    m_state->wake.notify_all();
}
IDXGISwapChain3* TemporalDX12::GetSwapchain() const { return m_state->Proxy(); }
bool TemporalDX12::HasPendingInputs() const { return !!m_state->pins; }
uint64_t TemporalDX12::RealFrameId() const { return m_state->pins ? m_state->pins->packet.frame.realFrameId : 0; }
ID3D12Resource* TemporalDX12::GetSdkComposedRealFrameColor() const
{
    std::lock_guard lock(m_state->mutex);
    // FSR UI texture composition overlays BOTH the real and generated image.
    // Its backbuffer must therefore contain HUD-less color. Other proxies take
    // the normal complete game image and use separate UI only for interpolation.
    return m_state->selected == TemporalProvider::Fsr && m_state->pins &&
        !m_state->pins->packet.nativeGateActive ? m_state->pins->textures[0].Get() : nullptr;
}

bool TemporalDX12::RequiresReconfigure()
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (state.latencyFrame || state.sleepInProgress) { return false; }
    const auto snapshot = TemporalRuntimeControl::Get().Snapshot();
    if (state.pendingRecovery) { return true; }
    if (snapshot.requestedGeneration == state.generation) { return false; }
    const auto& settings = snapshot.settings;
    const auto requested = settings.enabled ? settings.requestedFrameGenerator : TemporalProvider::None;
    const auto previous = state.settings.enabled ? state.settings.requestedFrameGenerator : TemporalProvider::None;
    if (requested != previous || settings.interpolatedFrameCount != state.settings.interpolatedFrameCount ||
        settings.runtimeDirectory != state.settings.runtimeDirectory || settings.dlssProjectId != state.settings.dlssProjectId)
    {
        return true;
    }
    // Options-only changes are serialized between real-frame tokens. They do
    // not rebuild the proxy, wait for GPU idle, or invalidate temporal history.
    state.settings = settings;
    state.generation = snapshot.requestedGeneration;
    const auto mode = state.reflexConflict || state.reflexFaulted ? TemporalLatencyMode::Off : settings.reflexMode;
    const auto result = state.dlss ? state.dlss->SetLatencyMode(mode) :
        TemporalResult{TemporalStatus::IntegrationRequired};
    state.PublishLatency(result);
    TemporalRuntimeControl::Get().PublishPlayer([&](auto& observed)
    {
        observed.playerObservedGeneration = state.generation;
    });
    state.wake.notify_all();
    return false;
}

bool TemporalDX12::Configure(HWND window, uint32_t width, uint32_t height, std::string& error)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    const auto snapshot = TemporalRuntimeControl::Get().Snapshot();
    if (state.generator || state.Proxy() || state.pins || state.latencyFrame || state.sleepInProgress)
    {
        error = "Temporal proxy configuration requires fully drained, released presentation ownership";
        return false;
    }
    if (state.generation != snapshot.requestedGeneration)
    {
        state.failedProviders = {};
        state.reflexFaulted = false;
        state.failureReason.clear();
    }
    state.window = window;
    state.settings = snapshot.settings;
    state.generation = snapshot.requestedGeneration;
    state.configuration.target = TemporalPresentationTarget::PlayerSwapchain;
    state.configuration.displayExtent = {width, height};
    state.configuration.transferFunction = TemporalTransferFunction::SRGB;
    state.configuration.minLuminance = 0.0f;
    state.configuration.maxLuminance = 100.0f;
    state.configuration.interpolatedFrameCount = state.settings.interpolatedFrameCount;
    state.stopped = state.failed = state.markerFailure = false;
    state.presentationReady = false;
    state.pendingRecovery = false;
    state.openedRealFrame = 0;
    state.reflexSleepSupported = state.reflexMarkersSupported = state.reflexConflict = false;
    state.capabilities.clear();
    state.dlss = state.resources.GetTemporalDlssSession();
    if (state.dlss && !state.dlss->IsBoundToDX12Device(state.resources.GetDevice()))
    {
        // Never send timing or tokens to a renderer on another D3D device.
        state.dlss.reset();
    }
    if (state.dlss)
    {
        const bool matches = state.dlss->MatchesRuntimeConfiguration(
            state.settings.runtimeDirectory, state.settings.dlssProjectId);
        state.dlss->SetLatencyMode(matches && !state.reflexFaulted
            ? state.settings.reflexMode : TemporalLatencyMode::Off);
        const auto latency = state.dlss->QueryLatencyState();
        state.reflexSleepSupported = latency.sleepSupport.IsSuccess() && !state.reflexFaulted;
        state.reflexMarkersSupported = latency.markerSupport.IsSuccess() && !state.reflexFaulted;
        if (!matches)
        {
            state.reflexConflict = true;
            state.failureReason = "Streamline runtime configuration changes require a fresh Player session";
        }
    }

    auto fsr = QueryFsrBuildAvailability(TemporalBackend::DX12);
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    FsrContextDescription fsrDescription;
    fsrDescription.maxRenderExtent = fsrDescription.displayExtent = {width, height};
    fsrDescription.highDynamicRange = true;
    auto fsrQueryConfiguration = state.configuration;
    fsrQueryConfiguration.interpolatedFrameCount = 1;
    fsr = QueryFsrCapabilities(MakeFsrBackendDeviceDX12(state.resources.GetDevice()), fsrDescription,
        fsrQueryConfiguration, FFX_SURFACE_FORMAT_R8G8B8A8_UNORM);
#endif
    state.capabilities.push_back(fsr);
    TemporalCapabilities dlss;
    dlss.provider = TemporalProvider::Dlss;
    dlss.backend = TemporalBackend::DX12;
    if (state.dlss)
    {
        dlss = state.dlss->QueryCapabilities();
        if (!state.dlss->MatchesRuntimeConfiguration(state.settings.runtimeDirectory, state.settings.dlssProjectId))
            dlss.frameGeneration = {TemporalStatus::IntegrationRequired};
    }
    else
    {
#if CREATOR_ENABLE_DLSS_STREAMLINE
        const auto bootstrap = state.resources.GetTemporalBootstrapResult();
        dlss.frameGeneration = !bootstrap.IsSuccess() && bootstrap.status != TemporalStatus::NotQueried ?
            bootstrap : TemporalResult{TemporalStatus::IntegrationRequired};
#else
        dlss.frameGeneration = {TemporalStatus::SdkNotBuilt};
#endif
    }
    state.capabilities.push_back(dlss);
    TemporalCapabilities xess;
    xess.provider = TemporalProvider::XeSS;
    xess.backend = TemporalBackend::DX12;
    xess.frameGeneration = XeSSFrameGenerationDX12::QuerySupport(state.resources.GetDevice(),
        state.settings.runtimeDirectory.c_str(), xess.maxInterpolatedFrames);
    state.capabilities.push_back(xess);
    for (auto& candidate : state.capabilities)
    {
        if (state.failedProviders[static_cast<size_t>(candidate.provider)])
        {
            candidate.frameGeneration = {TemporalStatus::SdkFailure};
        }
    }
    auto eligibleCapabilities = state.capabilities;
    for (auto& candidate : eligibleCapabilities)
    {
        if (candidate.frameGeneration.IsSuccess() && (state.configuration.interpolatedFrameCount == 0 ||
            state.configuration.interpolatedFrameCount > TemporalSupportedInterpolatedFrameCount(
                candidate.provider, TemporalBackend::DX12, state.capabilities)))
        {
            candidate.frameGeneration = {TemporalStatus::FeatureUnsupported};
        }
    }
    const auto requested = state.settings.enabled ? state.settings.requestedFrameGenerator : TemporalProvider::None;
    const auto selection = SelectTemporalProviders(TemporalProvider::None, requested,
        TemporalBackend::DX12, eligibleCapabilities);
    state.selected = selection.frameGenerator;
    state.requestedResult = selection.requestedFrameGenerator;
    if (requested == TemporalProvider::XeSS && state.UsesDlssToken())
    {
        // The already-loaded Reflex plugin must keep receiving Sleep calls.
        // A fresh process can omit it when XeSS FG requests the XeLL pacer;
        // this live combination has no validated single-owner pacing route.
        state.selected = TemporalProvider::None;
        state.requestedResult = {TemporalStatus::IntegrationRequired};
        state.reflexConflict = true;
        state.dlss->SetLatencyMode(TemporalLatencyMode::Off);
        state.failureReason = "XeSS FG and a loaded Reflex timing runtime require a fresh Player session; no alternate FG selected";
    }
    else if (requested == TemporalProvider::XeSS && state.settings.reflexMode != TemporalLatencyMode::Off)
    {
        state.reflexConflict = true;
        state.failureReason = "XeSS FG owns XeLL pacing; independent Reflex is unavailable for this combination";
    }
    if (state.UsesDlssToken())
    {
        state.dlss->SetPlayerFrameTokenOwnership(true);
    }
    state.PublishLatency(state.dlss ? state.dlss->QueryLatencyState().optionsResult :
        TemporalResult{TemporalStatus::NotQueried});
    TemporalRuntimeControl::Get().PublishPlayer([](auto& snapshot)
    {
        snapshot.generatedRealFrameId = 0;
        snapshot.generatedOrdinal = 0;
    });
    if (state.selected == TemporalProvider::None)
    {
        state.Publish(state.requestedResult, "Player real-frame fallback; no eligible SDK proxy");
        return true;
    }
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = DX12DeviceResources::kFrameCount;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // SDK owns pacing. Never mix the application waitable-object admission path.
    description.Flags = 0;
    const auto drain = [&state]()
    {
        std::string ignored;
        return TemporalResult{state.Drain(ignored) ? TemporalStatus::Success : TemporalStatus::SdkFailure};
    };
    const auto create = [&](TemporalProvider provider) -> TemporalResult
    {
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
        if (provider == TemporalProvider::Fsr)
        {
            FsrPlayerSwapchainDescriptionDX12 native;
            native.window = window;
            native.factory = state.resources.GetFactory();
            native.gameQueue = state.resources.GetCommandQueue();
            native.description = description;
            native.uiPremultipliedAlpha = true;
            FsrGpuSynchronization synchronization;
            synchronization.context = &state;
            synchronization.waitForGpuIdle = [](void* context)
            {
                std::string ignored;
                return TemporalResult{static_cast<State*>(context)->QueueIdle(ignored) ?
                    TemporalStatus::Success : TemporalStatus::SdkFailure};
            };
            std::unique_ptr<FsrFrameGeneratorDX12> generator;
            const auto result = CreateFsrFrameGeneratorDX12(state.resources, fsrDescription,
                state.configuration, native, synchronization, generator);
            if (result.IsSuccess())
            {
                state.fsr = generator.get();
                state.generator = std::move(generator);
                auto latency = state.resources.HasTemporalSharedDevice() && !state.reflexSleepSupported ?
                    state.antiLag.Initialize(state.resources.GetDevice()) : TemporalResult{TemporalStatus::IntegrationRequired};
                if (latency.IsSuccess()) latency = state.antiLag.BindFsrSwapchain(state.fsr->GetSwapchain());
                state.antiLagActive = latency.IsSuccess();
                if (!state.reflexSleepSupported)
                {
                    TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot)
                    {
                        snapshot.latencyProvider = "anti-lag-2";
                        snapshot.latencyResult = latency;
                    });
                }
            }
            return result;
        }
#endif
        if (provider == TemporalProvider::XeSS)
        {
            auto result = CreateXeSSFrameGeneratorDX12(state.resources, state.resources.GetCommandQueue(),
                state.resources.GetFactory(), window, description, state.settings.runtimeDirectory.c_str(),
                state.configuration, false, drain, state.generator, state.xess);
            if (result.IsSuccess()) result = state.xess->SetEnabled(true);
            return result;
        }
        if (provider == TemporalProvider::Dlss && state.dlss)
        {
            ComPtr<IDXGISwapChain1> chain;
            HRESULT created = state.resources.GetFactory()->CreateSwapChainForHwnd(state.resources.GetCommandQueue(),
                window, &description, nullptr, nullptr, &chain);
            if (FAILED(created)) return {TemporalStatus::SdkFailure, created};
            if (FAILED(chain.As(&state.dlssSwapchain))) return {TemporalStatus::SdkFailure};
            auto result = state.dlss->BindPlayerSwapchain(state.dlssSwapchain.Get(), state.configuration,
                DlssPacingOwner::Streamline);
            if (result.IsSuccess()) result = CreateDlssDX12FrameGenerator(state.resources, state.dlss,
                state.configuration, drain, state.generator);
            if (result.IsSuccess()) result = state.dlss->SetFrameGenerationOptions(0, state.configuration, true);
            if (result.IsSuccess()) state.dlss->SetPlayerFrameTokenOwnership(true);
            return result;
        }
        return {TemporalStatus::SdkNotBuilt};
    };
    const auto created = create(state.selected);
    if (!created.IsSuccess())
    {
        if (state.selected == requested) state.requestedResult = created;
        // Context/partial proxy failure cannot be silently activated. A clean
        // fallback is safe only once the failed candidate has drained/destroyed.
        if (state.generator && !state.generator->Shutdown().IsSuccess())
        {
            state.failed = true;
            return Report(created, "Failed to drain partial temporal proxy", error);
        }
        state.generator.reset();
        state.xess = nullptr;
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
        state.fsr = nullptr;
#endif
        state.dlssSwapchain.Reset();
        if (state.selected != TemporalProvider::Fsr && fsr.frameGeneration.IsSuccess() &&
            state.configuration.interpolatedFrameCount == 1 &&
            !state.failedProviders[static_cast<size_t>(TemporalProvider::Fsr)])
        {
            state.selected = TemporalProvider::Fsr;
            const auto fallback = create(state.selected);
            if (fallback.IsSuccess())
            {
                state.Publish(created, "Requested FG context failed; initialized FSR fallback, awaiting live inputs");
                return true;
            }
        }
        if (state.generator && !state.generator->Shutdown().IsSuccess())
        {
            state.failed = true;
            error = "Failed FSR fallback still owns pending SDK work";
            return false;
        }
        state.generator.reset();
        state.xess = nullptr;
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
        state.fsr = nullptr;
#endif
        state.dlssSwapchain.Reset();
        state.selected = TemporalProvider::None;
        state.Publish(created, "Temporal proxy creation failed; real-frame fallback");
        return true;
    }
    state.Publish({TemporalStatus::NotInitialized}, "SDK proxy initialized; awaiting a complete real-frame input packet");
    return true;
}

bool TemporalDX12::BeginSimulationFrame(uint64_t realFrameId, std::string& error)
{
    auto& state = *m_state;
    std::unique_lock lock(state.mutex);
    RECT client{};
    const bool drawable = state.window && !IsIconic(state.window) && GetClientRect(state.window, &client) &&
        client.right > client.left && client.bottom > client.top;
    if (!drawable)
    {
        state.suspended = true;
        if (state.renderSubmitEnded) state.CancelUnusedToken();
        return true; // Simulation may continue; Player does not publish hidden render packets.
    }
    state.suspended = false;
    const auto requestedGeneration = TemporalRuntimeControl::Get().Snapshot().requestedGeneration;
    if ((!state.presentationReady || state.pendingRecovery || requestedGeneration > state.generation) &&
        !state.wake.wait_for(lock, std::chrono::milliseconds(5000),
            [&] { return (state.presentationReady && !state.pendingRecovery && requestedGeneration <= state.generation) ||
                state.stopped || state.failed; }))
    {
        error = "Player temporal settings were not observed by the presentation owner";
        return false;
    }
    if (state.stopped || state.failed)
    {
        error = "Player temporal admission is stopped after a lifecycle failure";
        return false;
    }
    if (!state.UsesDlssToken() && !state.xess && !state.antiLagActive) { return true; }
    // The serial SDK API has one pending latency-token set. A dropped/latest-wins
    // packet may not be relabeled as another frame. Timeout fails closed instead
    // of inventing simulation markers on PT or silently releasing live input.
    if (!state.wake.wait_for(lock, std::chrono::milliseconds(5000), [&]
            { return (state.presentationReady && !state.latencyFrame) || state.stopped || state.failed; }) || state.stopped || state.failed)
    {
        error = "Temporal game-loop admission did not retire the previous real frame";
        return false;
    }
    TemporalResult result{TemporalStatus::Success};
    if (state.UsesDlssToken())
    {
        result = state.dlss->EnsureRealFrame(realFrameId);
    }
    state.latencyFrame = realFrameId;
    state.sleepInProgress = true;
    // Pin the SDK owner but release the existing Player mutex while sleeping.
    // PT/RT must be able to finish prior work; teardown rejects this in-flight
    // admission and adapter retirement rejects a token whose sleep is running.
    const auto dlssOwner = state.dlss;
    auto* xessOwner = state.xess;
    const bool reflexSleep = state.reflexSleepSupported;
    const bool antiLagSleep = state.antiLagActive && !reflexSleep && !xessOwner;
    lock.unlock();
    if (result.IsSuccess())
    {
        if (reflexSleep) { result = dlssOwner->Sleep(realFrameId); }
        else if (xessOwner) { result = xessOwner->Sleep(realFrameId); }
        else if (antiLagSleep) { result = state.antiLag.BeforeInput(); }
    }
    lock.lock();
    state.sleepInProgress = false;
    state.wake.notify_all();
    state.PublishLatency(result);
    if (state.selected != TemporalProvider::Dlss && !state.xess && !result.IsSuccess())
    {
        if (state.UsesDlssToken())
        {
            // The same simulation may still use SR/NIS/DeepDVC on RT. Keep
            // its allocated token available to EnsureRealFrame(realFrameId);
            // render-only ownership retires it at the next frame or shutdown.
            // Discarding here would make this otherwise valid ID unreusable.
            state.dlss->SetLatencyMode(TemporalLatencyMode::Off);
            state.dlss->SetPlayerFrameTokenOwnership(false);
            state.reflexFaulted = true;
            state.reflexSleepSupported = state.reflexMarkersSupported = false;
        }
        if (antiLagSleep) { state.antiLagActive = false; }
        state.latencyFrame = 0;
        state.PublishLatency(result);
        state.wake.notify_all();
        return true; // Optional timing failure cannot take away native/FSR rendering.
    }
    const auto recoverLatency = [&](TemporalResult failure, const char* operation)
    {
        if (state.UsesDlssToken())
        {
            const auto discarded = state.dlss->DiscardRealFrame(realFrameId);
            if (!discarded.IsSuccess()) return Report(discarded, "Unretired failed latency token", error);
        }
        if (state.xess)
        {
            const auto discarded = state.xess->DiscardRealFrame(realFrameId);
            if (!discarded.IsSuccess()) return Report(discarded, "Unretired failed XeLL token", error);
        }
        state.latencyFrame = 0;
        state.failedProviders[static_cast<size_t>(state.selected)] = true;
        if (state.selected == state.settings.requestedFrameGenerator) state.requestedResult = failure;
        Report(failure, operation, error);
        state.failureReason = error;
        state.pendingRecovery = true;
        state.presentationReady = false;
        state.Publish(failure, error);
        return false;
    };
    if (!result.IsSuccess()) return recoverLatency(result, "Temporal frame-start sleep/token");
    state.renderSubmitEnded = false;
    state.markerFailure = false;
    const bool marked = state.Mark(realFrameId, RHITemporalLatencyMarker::SimulationStart);
    if (!marked && (state.selected == TemporalProvider::Dlss || state.xess))
    {
        return recoverLatency({TemporalStatus::SdkFailure}, "Temporal simulation-start marker failed");
    }
    if (marked)
    {
        TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot) { snapshot.latencyMarkerRealFrameId = realFrameId; });
    }
    return true;
}
void TemporalDX12::Mark(uint64_t realFrameId, RHITemporalLatencyMarker marker)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (state.latencyFrame == realFrameId) state.Mark(realFrameId, marker);
}

void TemporalDX12::OpenRealFrame(const TemporalMeasurementProvenance& provenance)
{
    std::lock_guard lock(m_state->mutex);
    m_state->openedRealFrame = provenance;
}

bool TemporalDX12::PresentRealFrame(std::string& error, bool sourceComposed)
{
    auto& state = *m_state;
    std::unique_lock lock(state.mutex);
    if (state.Proxy())
    {
        error = "Native presentation cannot bypass an active frame-generation proxy";
        return false;
    }
    const auto source = sourceComposed ? state.openedRealFrame : TemporalMeasurementProvenance{};
    const uint64_t frameId = source.realFrameId;
    const uint64_t presenterGeneration = state.generation;
    state.openedRealFrame = {};
    const bool token = state.UsesDlssToken() && frameId != 0 && frameId == state.latencyFrame &&
        state.renderSubmitEnded && !state.sleepInProgress;
    if (!token)
    {
        lock.unlock();
        HRESULT native = E_PENDING;
        const bool presented = state.resources.Present(error, &native);
        state.PublishPresent(source, frameId, TemporalProvider::None, 0, source.nativeGateActive,
            {native == E_PENDING ? TemporalStatus::NotQueried : native == S_OK ? TemporalStatus::Success
                : presented ? TemporalStatus::NotInitialized
                : TemporalStatus::SdkFailure, native}, presenterGeneration);
        return presented;
    }
    TemporalResult marker{TemporalStatus::Success};
    if (token && state.reflexMarkersSupported)
    {
        marker = state.dlss->MarkLatency(frameId, DlssLatencyMarker::PresentStart);
    }
    // A stale/native placeholder image may still be presented; never label it
    // with the current GT token or retire a newer simulation's admission.
    HRESULT native = E_PENDING;
    const bool presented = state.resources.Present(error, &native);
    state.PublishPresent(source, frameId, TemporalProvider::None, 0, source.nativeGateActive,
        {native == E_PENDING ? TemporalStatus::NotQueried : native == S_OK ? TemporalStatus::Success
            : presented ? TemporalStatus::NotInitialized
            : TemporalStatus::SdkFailure, native}, presenterGeneration);
    if (token)
    {
        if (state.reflexMarkersSupported)
        {
            const auto ended = state.dlss->MarkLatency(frameId, DlssLatencyMarker::PresentEnd);
            if (marker.IsSuccess()) { marker = ended; }
        }
        const auto retired = state.dlss->EndRealFrame(frameId);
        if (!retired.IsSuccess())
        {
            state.failed = true;
            state.PublishLatency(retired);
            state.wake.notify_all();
            return Report(retired, "Native Reflex token retirement", error);
        }
        state.latencyFrame = 0;
        if (presented && frameId > state.lastPresented)
        {
            state.lastPresented = frameId;
        }
        state.PublishLatency(marker);
        state.wake.notify_all();
    }
    return presented;
}
void TemporalDX12::Discard(uint64_t frameId)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (!frameId || state.latencyFrame != frameId || state.pins || state.prepared || state.sleepInProgress) { return; }
    TemporalResult discarded{TemporalStatus::Success};
    if (state.UsesDlssToken()) { discarded = state.dlss->DiscardRealFrame(frameId); }
    if (state.xess) discarded = state.xess->DiscardRealFrame(frameId);
    if (!discarded.IsSuccess()) { state.failed = true; state.Publish(discarded, "Temporal token discard refused"); }
    else state.latencyFrame = 0;
    state.wake.notify_all();
}
bool TemporalDX12::SuspendProxy(std::string& error)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    state.suspended = true;
    if (!state.Proxy())
    {
        if (state.renderSubmitEnded) { state.CancelUnusedToken(); }
        return true;
    }
    if (state.pins || state.prepared)
    {
        error = "Cannot suspend an SDK proxy with unfinished temporal inputs";
        return false;
    }
    TemporalResult result{TemporalStatus::Success};
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    if (state.fsr) result = state.fsr->DisableGeneration();
#endif
    if (state.xess) result = state.xess->SetEnabled(false);
    if (state.selected == TemporalProvider::Dlss)
        result = state.dlss->SetFrameGenerationOptions(0, state.configuration, false);
    if (state.renderSubmitEnded) state.CancelUnusedToken();
    return Report(result, "Suspend SDK generation for minimized Player", error);
}

void TemporalDX12::StopSimulationFrames()
{
    std::lock_guard lock(m_state->mutex);
    m_state->stopped = true;
    m_state->wake.notify_all();
}

bool TemporalDX12::Open(const RHITemporalDisplayPacket& packet, std::string& error)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (!state.Proxy()) return true;
    if (packet.frame.realFrameId <= state.lastPresented ||
        ((state.selected != TemporalProvider::Fsr || state.UsesDlssToken()) &&
            packet.frame.realFrameId != state.latencyFrame)) { return true; }
    if (!packet.valid || !packet.consumerLease || !packet.lifetimeToken || state.pins ||
        packet.frame.displayExtent != state.configuration.displayExtent ||
        !ValidateTemporalFrame(packet.frame).IsSuccess())
    {
        error = "Temporal display packet is incomplete, stale, or belongs to another display generation";
        return false;
    }
    auto pins = std::make_shared<State::Pins>();
    pins->packet = packet;
    pins->presenterProvider = packet.nativeGateActive ? TemporalProvider::None : state.selected;
    pins->presenterInterpolatedFrameCount = pins->presenterProvider != TemporalProvider::None
        ? state.configuration.interpolatedFrameCount : 0;
    pins->presenterGeneration = state.generation;
    constexpr DXGI_FORMAT formats[]{DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT};
    for (size_t index = 0; index < pins->textures.size(); ++index)
    {
        const auto extent = index < 2 ? packet.frame.displayExtent : packet.frame.renderExtent;
        if (!packet.sharedHandles[index] || FAILED(state.resources.GetDevice()->OpenSharedHandle(
            packet.sharedHandles[index], IID_PPV_ARGS(&pins->textures[index]))))
        {
            error = "Could not open all temporal display resources on the Player device";
            return false;
        }
        const auto description = pins->textures[index]->GetDesc();
        if (description.Width != extent.width || description.Height != extent.height ||
            description.Format != formats[index] || description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            description.MipLevels != 1 || description.DepthOrArraySize != 1 || description.SampleDesc.Count != 1)
        {
            error = "Temporal shared resource description does not match its real-frame packet";
            return false;
        }
    }
    for (size_t index = 0; index < pins->handles.size(); ++index)
    {
        pins->handles[index] = state.resources.RegisterExternalTexture(pins->textures[index].Get());
        if (!pins->handles[index].IsValid())
        {
            for (const auto handle : pins->handles) if (handle.IsValid()) state.resources.ReleaseTexture(handle);
            error = "Could not register Player temporal resource imports";
            return false;
        }
    }
    state.pins = std::move(pins);
    state.sdkPresented = state.sdkConsumed = state.recordingSubmitted = false;
    state.reportedDisplayFrames = 0;
    return true;
}

bool TemporalDX12::Prepare(std::string& error)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (!state.Proxy()) return true;
    if (!state.pins || state.markerFailure)
    {
        error = "FG requires full live temporal inputs and correctly timed real-frame markers";
        return false;
    }
    const bool nativeGate = state.pins->packet.nativeGateActive;
    TemporalResult mode{TemporalStatus::Success};
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    if (state.fsr && nativeGate) mode = state.fsr->DisableGeneration();
#endif
    if (state.xess) mode = state.xess->SetEnabled(!nativeGate);
    if (state.selected == TemporalProvider::Dlss)
        mode = state.dlss->SetFrameGenerationOptions(0, state.configuration, !nativeGate);
    if (!Report(mode, "Set real-frame native-gate mode", error)) return false;
    if (nativeGate)
    {
        state.prepared = true;
        return true;
    }
    auto* commands = state.resources.GetCommandList();
    std::array<D3D12_RESOURCE_BARRIER, 4> barriers{};
    for (size_t index = 0; index < barriers.size(); ++index)
    {
        barriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[index].Transition = {state.pins->textures[index].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COPY_DEST, kReadState};
    }
    commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    state.readState = true;
    TemporalFrameGenerationInputs inputs;
    inputs.frame = state.pins->packet.frame;
    inputs.hudlessColor = state.pins->handles[0];
    inputs.uiColor = state.pins->handles[1];
    inputs.depth = state.pins->handles[2];
    inputs.motionVectors = state.pins->handles[3];
    inputs.lifetimeToken = state.pins;
    DX12Encoder encoder(commands, &state.resources);
    const auto result = state.generator->Prepare(inputs, encoder);
    state.prepared = result.IsSuccess();
    state.Publish(result);
    return Report(result, "Prepare actual temporal display inputs", error);
}

bool TemporalDX12::Present(std::string& error)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (!state.Proxy() || !state.prepared || !state.pins)
    {
        error = "Temporal Present requires a prepared real frame";
        return false;
    }
    state.recordingSubmitted = true; // EndFrame succeeded before this entry point.
    if (!state.resources.WaitForLastFrameSubmission(error)) return false;
    const auto frameId = state.pins->packet.frame.realFrameId;
    TemporalResult result{TemporalStatus::NotInitialized};
    TemporalResult presentObservation;
    TemporalResult latency{TemporalStatus::Success};
    if (state.reflexMarkersSupported)
    {
        latency = state.dlss->MarkLatency(frameId, DlssLatencyMarker::PresentStart);
        if (!latency.IsSuccess() && state.selected == TemporalProvider::Dlss)
        {
            state.PublishLatency(latency);
            return Report(latency, "DLSS Present-start marker", error);
        }
    }
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    if (state.fsr)
    {
        result = state.pins->packet.nativeGateActive ? state.fsr->PresentRealFrame(1, 0, &presentObservation)
            : state.fsr->Present(1, 0, &presentObservation);
    }
#endif
    if (state.xess)
    {
        result = state.xess->Present(frameId, 1, 0, &presentObservation);
    }
    if (state.selected == TemporalProvider::Dlss)
    {
        const HRESULT native = state.dlssSwapchain->Present(0, 0);
        state.sdkPresented = true; // Even a failing call may have submitted SDK work.
        result = {SUCCEEDED(native) ? TemporalStatus::Success : TemporalStatus::SdkFailure, native};
        presentObservation = {native == S_OK ? TemporalStatus::Success : SUCCEEDED(native)
            ? TemporalStatus::NotInitialized : TemporalStatus::SdkFailure, native};
    }
    // Publish the exact input and raw native-call result, independently of any
    // later SDK marker/drain failure folded into the adapter's return value.
    // Neither observation is a physical display timestamp.
    const auto& packet = state.pins->packet;
    state.PublishPresent(packet.provenance, frameId, state.pins->presenterProvider,
        state.pins->presenterInterpolatedFrameCount, packet.nativeGateActive, presentObservation,
        state.pins->presenterGeneration);
    if (state.reflexMarkersSupported)
    {
        const auto ended = state.dlss->MarkLatency(frameId, DlssLatencyMarker::PresentEnd);
        if (latency.IsSuccess()) { latency = ended; }
        if (result.IsSuccess() && state.selected == TemporalProvider::Dlss) { result = latency; }
        state.PublishLatency(latency);
    }
    state.sdkPresented = true;
    state.Publish(result);
    if (!Report(result, "SDK proxy Present", error) || !state.Drain(error))
    {
        return false; // PT attempts drained fallback before releasing GT admission.
    }
    state.sdkConsumed = true;
    state.lastPresented = frameId;
    uint64_t generated = 0;
    // FSR reports successful generation callback output; the other SDKs report
    // proxy-present counts. None of these counts claim physical scan-out.
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    const uint64_t generatedSubmissions = state.fsr && !state.pins->packet.nativeGateActive ?
        state.fsr->GetCompletedGeneratedFrameCount() : 0;
#else
    const uint64_t generatedSubmissions = 0;
#endif
    if (state.xess)
    {
        XeSSPresentStatus status;
        if (!state.pins->packet.nativeGateActive && state.xess->GetLastPresentStatus(status).IsSuccess() &&
            status.interpolation.IsSuccess() && status.enabled && status.framesPresented > 1) generated = status.framesPresented - 1;
    }
    if (state.selected == TemporalProvider::Dlss)
    {
        if (!state.pins->packet.nativeGateActive && state.reportedDisplayFrames > 1) generated = state.reportedDisplayFrames - 1;
    }
    if (state.UsesDlssToken() && !Report(state.dlss->EndRealFrame(frameId), "Streamline real-frame retirement", error))
    {
        return false;
    }
    TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot)
    {
        ++snapshot.realPresentationCount;
        snapshot.generatedPresentationCount += generated;
        snapshot.generatedSubmissionCount += generatedSubmissions;
        // Streamline's aggregate actually-presented counter does not identify
        // individual generated outputs. Zero keeps unavailable attribution explicit.
        snapshot.generatedRealFrameId = 0;
        snapshot.generatedOrdinal = 0;
        if (generated && state.selected == TemporalProvider::XeSS)
        {
            snapshot.generatedRealFrameId = frameId; // Serial GetLastPresentStatus corresponds to this Present.
            snapshot.generatedOrdinal = static_cast<uint32_t>(generated);
        }
        if (generatedSubmissions)
        {
            snapshot.generatedRealFrameId = frameId;
            snapshot.generatedOrdinal = static_cast<uint32_t>(generatedSubmissions);
        }
        snapshot.sdkFinalConsumedFrameId = frameId;
        snapshot.activeFrameGenerator = state.pins->packet.nativeGateActive ? TemporalProvider::None : state.selected;
        snapshot.activeInterpolatedFrameCount = snapshot.activeFrameGenerator != TemporalProvider::None
            ? state.configuration.interpolatedFrameCount : 0;
    });
    return true;
}

bool TemporalDX12::RestoreAndRelease(std::string& error)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (!state.pins) return true;
    if (!state.sdkConsumed)
    {
        error = "Cannot release temporal display inputs before SDK final consumption";
        return false;
    }
    if (!state.RestoreInputs(error)) return false;
    if (state.xess && !Report(state.xess->ReleaseInputsAfterGpuIdle(), "Release XeSS final input owner", error)) return false;
    if (state.selected == TemporalProvider::Dlss &&
        !Report(state.dlss->ReleaseFrameGenerationInputsAfterGpuIdle(), "Release DLSS final input owner", error)) return false;
    for (const auto handle : state.pins->handles) state.resources.ReleaseTexture(handle);
    state.pins.reset();
    state.prepared = false;
    state.latencyFrame = 0;
    state.wake.notify_all();
    return true;
}
bool TemporalDX12::Drain(std::string& error)
{
    std::lock_guard lock(m_state->mutex);
    return m_state->Drain(error);
}
void TemporalDX12::LatchFailure(const std::string& reason, bool recordingAborted)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    state.failedProviders[static_cast<size_t>(state.selected)] = true;
    if (state.selected == state.settings.requestedFrameGenerator) state.requestedResult = {TemporalStatus::SdkFailure};
    state.failureReason = reason;
    // Aborted-before-enqueue is distinct from a submission failure: only the
    // former proves that the recorded COPY_DEST->SRV barriers never executed.
    if (recordingAborted) state.readState = false;
    state.Publish({TemporalStatus::SdkFailure}, reason);
}

bool TemporalDX12::Shutdown(std::string& error, bool stopGameLoop)
{
    if (!m_state) return true;
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (state.sleepInProgress)
    {
        error = "Temporal shutdown must join the game-loop sleep before releasing SDK ownership";
        return false;
    }
    state.presentationReady = false;
    if (stopGameLoop) { state.stopped = true; state.wake.notify_all(); }
    if (state.UsesDlssToken() && state.latencyFrame && !state.prepared && !state.pins)
    {
        if (!Report(state.dlss->DiscardRealFrame(state.latencyFrame), "DLSS unused token discard", error)) return false;
        state.latencyFrame = 0;
    }
    if (state.selected == TemporalProvider::Dlss && state.dlss && state.latencyFrame)
    {
        if (!Report(state.dlss->SetFrameGenerationOptions(0, state.configuration, false),
                "Disable DLSS before token/context teardown", error) || !state.Drain(error) ||
            !Report(state.dlss->EndRealFrame(state.latencyFrame), "Retire drained DLSS token", error)) return false;
        state.latencyFrame = 0;
    }
    if (state.generator)
    {
        if (!Report(state.generator->Shutdown(), "SDK proxy shutdown", error))
        {
            if (state.pins) state.pins->packet.consumerLease->m_completionLost.store(true, std::memory_order_release);
            return false;
        }
        state.generator.reset();
    }
    if (!Report(state.antiLag.Shutdown(), "Anti-Lag proxy unbind", error)) return false;
    state.antiLagActive = false;
    if (state.pins)
    {
        // Shutdown above proved SDK retirement. Restore only when submitted
        // barriers have a known final state; ambiguous submissions poison reuse.
        if (!state.RestoreInputs(error)) return false;
        for (const auto handle : state.pins->handles) if (handle.IsValid()) state.resources.ReleaseTexture(handle);
        state.pins.reset();
    }
    if (state.UsesDlssToken() && state.latencyFrame)
    {
        if (!Report(state.dlss->EndRealFrame(state.latencyFrame), "Retire drained Streamline token", error))
        {
            return false;
        }
    }
    state.latencyFrame = 0;
    state.dlssSwapchain.Reset();
    if (state.UsesDlssToken())
    {
        state.dlss->SetPlayerFrameTokenOwnership(false);
        if (stopGameLoop)
        {
            state.dlss->SetLatencyMode(TemporalLatencyMode::Off);
            state.PublishLatency({TemporalStatus::NotInitialized});
        }
    }
    state.dlss.reset();
    state.xess = nullptr;
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    state.fsr = nullptr;
#endif
    if (state.finalEvent) { CloseHandle(state.finalEvent); state.finalEvent = nullptr; }
    state.readState = state.prepared = state.sdkPresented = false;
    state.reflexSleepSupported = state.reflexMarkersSupported = false;
    state.openedRealFrame = {};
    state.selected = TemporalProvider::None;
    TemporalRuntimeControl::Get().PublishPlayer([](auto& snapshot)
    {
        snapshot.configuredInterpolatedFrameCount = 0;
        snapshot.activeInterpolatedFrameCount = 0;
        snapshot.activeFrameGenerator = TemporalProvider::None;
    });
    return true;
}
}
