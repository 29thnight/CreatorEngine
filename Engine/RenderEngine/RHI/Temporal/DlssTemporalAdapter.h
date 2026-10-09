#pragma once

#include "../../Render/Temporal/TemporalReconstruction.h"
#include "../../Render/Temporal/ITemporalUpscaler.h"
#include "../../Render/Temporal/ITemporalFrameGenerator.h"

#include <functional>
#include <memory>
#include <string>

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct IDXGISwapChain3;
class DX12DeviceResources;

// RHI-private SDK boundary. No Streamline types escape this file pair.
// Streamline 2.14.1: 2122257e0fce486f91b385aa63b9a09b0a34b363.
struct DlssInitialization
{
    TemporalBackend backend{ TemporalBackend::DX12 };
    std::wstring pluginDirectory; // Absolute, trusted deployment directory.
    uint32_t applicationId{ 0 }; // NVIDIA-issued identity, never a fabricated ID.
    std::string engineVersion;
    std::string projectId; // Project GUID; required with engineVersion if applicationId is zero.
    bool loadUpscaling{ true };
    bool loadFrameGeneration{ false };
};

struct DlssDX12Texture
{
    ID3D12Resource* resource{ nullptr }; // Borrowed; caller owns all GPU lifetime.
    uint32_t state{ UINT32_MAX }; // Actual D3D12_RESOURCE_STATES at SDK consumption.
};

struct DlssUpscaleResources
{
    ID3D12GraphicsCommandList* commandList{ nullptr }; // Native DIRECT/COMPUTE list, already recording.
    DlssDX12Texture color;
    DlssDX12Texture depth;
    DlssDX12Texture motionVectors;
    DlssDX12Texture exposure; // Optional 1x1; absent means SDK auto-exposure.
    DlssDX12Texture output;
};

struct DlssFrameGenerationResources
{
    DlssDX12Texture hudlessColor; // Display size, post-tone-map, no UI.
    DlssDX12Texture uiColor; // Display size, premultiplied color and alpha; optional.
    DlssDX12Texture depth;
    DlssDX12Texture motionVectors;
    std::shared_ptr<const void> lifetimeToken; // Existing graph/presentation owner, includes alias/pool lifetime.
};

struct DlssOptimalSettings
{
    TemporalExtent optimalRenderExtent;
    TemporalExtent minimumRenderExtent;
    TemporalExtent maximumRenderExtent;
};

enum class DlssLatencyMarker : uint8_t
{
    SimulationStart, SimulationEnd, RenderSubmitStart, RenderSubmitEnd, PresentStart, PresentEnd,
    InputSample, LatencyPing, TriggerFlash
};

enum class DlssDX12ProxyKind : uint8_t { Device, Factory, Swapchain };
enum class DlssPacingOwner : uint8_t { Unspecified, Streamline, ApplicationWaitableObject };

struct DlssFrameGenerationState
{
    uint32_t statusFlags{ 0 }; // sl::DLSSGStatus, retained for diagnostics.
    uint64_t presentedFramesSinceLastQuery{ 0 }; // Includes real frames; never simulation FPS.
    uint32_t maxInterpolatedFrames{ 0 };
    uint32_t minimumDimension{ 0 };
    bool vsyncSupported{ false };
    // Borrowed ID3D12Fence. Query on the PRESENT thread after Present. Keep the
    // runtime alive and wait on this fence/value before cross-queue input reuse.
    // A Present return or this CPU query is not GPU completion.
    void* inputsCompletionFence{ nullptr };
    uint64_t inputsCompletionValue{ 0 };
};

class DlssTemporalAdapter
{
public:
    DlssTemporalAdapter();
    ~DlssTemporalAdapter();
    DlssTemporalAdapter(const DlssTemporalAdapter&) = delete;
    DlssTemporalAdapter& operator=(const DlssTemporalAdapter&) = delete;

    // One Streamline runtime per process. Initialize before hooked DXGI/device
    // operations. Requires externally supplied, signed NVIDIA runtime files.
    TemporalResult Initialize(const DlssInitialization& initialization);
    TemporalResult BindDX12Device(ID3D12Device* nativeDevice);
    bool IsBoundToDX12Device(ID3D12Device* nativeDevice) const;
    TemporalCapabilities QueryCapabilities();

    // Immediately after native creation, upgrade DEVICE and FACTORY, then use
    // the proxy device for CreateCommandQueue and the proxy factory for all
    // Player swapchain creation. The caller owns the returned COM reference.
    // Pass a raw pointer copy while retaining the original native COM owner;
    // do not overwrite the sole owning ComPtr with the proxy.
    // Use a proxy swapchain for every hooked call (Present[1], GetBuffer,
    // GetCurrentBackBufferIndex, ResizeBuffers[1], SetFullscreenState).
    TemporalResult UpgradeDX12Interface(void** interfacePointer, DlssDX12ProxyKind kind);
    // Verifies an actual proxy and explicit pacing ownership. Do not pass an
    // Editor viewport. The caller must have routed creation through the proxies.
    TemporalResult BindPlayerSwapchain(IDXGISwapChain3* proxySwapchain,
        const TemporalFrameGenerationConfig& configuration, DlssPacingOwner pacingOwner);
    TemporalResult ValidatePlayerSwapchainBinding(const TemporalFrameGenerationConfig& configuration) const;

    TemporalResult GetOptimalSettings(TemporalQuality quality, TemporalExtent displayExtent,
        DlssOptimalSettings& settings);
    // Begin before input/simulation. SR, FG and all latency markers share the
    // same token for this REAL frame. At most six unreleased tokens may exist.
    TemporalResult BeginRealFrame(uint64_t realFrameId);
    TemporalResult SetFrameConstants(uint32_t viewportId, const TemporalFrame& frame);
    TemporalResult DispatchUpscaling(uint32_t viewportId, uint64_t realFrameId,
        TemporalQuality quality, const DlssUpscaleResources& resources);

    // PRESENT thread only. Disable before pause/loading/resize/fullscreen and
    // shutdown. Turning FG off does not recreate the proxy swapchain; the shell
    // must recreate it to eliminate interposition overhead when appropriate.
    TemporalResult SetFrameGenerationOptions(uint32_t viewportId,
        const TemporalFrameGenerationConfig& configuration, bool enabled,
        TemporalExtent dynamicResolutionTarget = {});
    // Tags are immutable through final FG consumption, not just CPU Present.
    // FG runs in the proxy Present hook, never through slEvaluateFeature.
    TemporalResult PrepareFrameGeneration(uint32_t viewportId, uint64_t realFrameId,
        const DlssFrameGenerationResources& resources);
    TemporalResult GetFrameGenerationState(uint32_t viewportId, DlssFrameGenerationState& state);
    // Invoke on the game-loop thread at the SDK-defined frame-start point, and
    // invoke markers at their real simulation/submission/presentation boundaries.
    // Sleep remains required while the Reflex feature is supported, even Off.
    TemporalResult Sleep(uint64_t realFrameId);
    TemporalResult MarkLatency(uint64_t realFrameId, DlssLatencyMarker marker);
    TemporalResult EndRealFrame(uint64_t realFrameId); // After final CPU consumer/PresentEnd, not a GPU release.
    TemporalResult TakePresentationError(); // Native asynchronous DXGI error, when supplied by SDK.

    // Caller must quiesce all CPU entry points AND drain every GPU/FG queue.
    // SDK write/dispatch failures latch their axis until a successful drained
    // free; continuing with a new frame ID alone cannot recover damaged history.
    TemporalResult FreeViewportAfterGpuIdle(uint32_t viewportId);
    TemporalResult FreeUpscalingAfterGpuIdle(uint32_t viewportId);
    TemporalResult FreeFrameGenerationAfterGpuIdle(uint32_t viewportId);
    // Bounded serial baseline: release the previous frame owner only after the
    // caller's final-consumption drain, before preparing the next FG input set.
    TemporalResult ReleaseFrameGenerationInputsAfterGpuIdle();
    // SDK shutdown precedes native device/factory destruction. DLL unloading is
    // deliberately separate: its proxy vtables must survive COM object release.
    TemporalResult ShutdownAfterGpuIdle();
    TemporalResult UnloadAfterNativeObjectsDestroyed();

private:
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

// Backend-private factories: the supplied session/device must outlive all SDK
// GPU work, and the caller still owns BeginRealFrame/Reflex/Present/EndRealFrame.
// Constants are shared idempotently between TU and FG for each real frame.
// A nonempty drain callback must stop new work and wait for actual final SDK
// consumption, including FG fences if TU/FG share resources. It may fail; no
// resources are freed on failure. A failed destructor drain quarantines the
// shared session rather than unloading live code or freeing active GPU memory.
using DlssFinalConsumptionDrain = std::function<TemporalResult()>;
TemporalResult CreateDlssDX12Upscaler(DX12DeviceResources& resources,
    std::shared_ptr<DlssTemporalAdapter> session, uint32_t viewportId, TemporalQuality quality,
    DlssFinalConsumptionDrain drain, std::unique_ptr<ITemporalUpscaler>& upscaler);
TemporalResult CreateDlssDX12FrameGenerator(DX12DeviceResources& resources,
    std::shared_ptr<DlssTemporalAdapter> session, const TemporalFrameGenerationConfig& configuration,
    DlssFinalConsumptionDrain drain, std::unique_ptr<ITemporalFrameGenerator>& frameGenerator);
