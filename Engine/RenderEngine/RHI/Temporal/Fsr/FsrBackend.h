#pragma once
#if CE_DEVELOPMENT && !CE_SHIPPING
#include <functional>
#endif

#include "../../../Render/Temporal/TemporalReconstruction.h"

#ifndef CREATOR_ENABLE_FSR_SDK
#define CREATOR_ENABLE_FSR_SDK 0
#endif
#ifndef CREATOR_ENABLE_FSR_DX12_SDK
#define CREATOR_ENABLE_FSR_DX12_SDK 0
#endif
#ifndef CREATOR_ENABLE_FSR_VULKAN_SDK
#define CREATOR_ENABLE_FSR_VULKAN_SDK 0
#endif
#if CREATOR_ENABLE_FSR_DX12_SDK && CREATOR_ENABLE_FSR_VULKAN_SDK
#error FidelityFX SDK 1.1.4 static backends export colliding symbols; select one backend per module.
#endif

// Build availability is deliberately not a device capability query.
TemporalCapabilities QueryFsrBuildAvailability(TemporalBackend backend);

#if CREATOR_ENABLE_FSR_SDK
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

static_assert(FFX_FSR3UPSCALER_VERSION_MAJOR == 3 && FFX_FSR3UPSCALER_VERSION_MINOR == 1 &&
    FFX_FSR3UPSCALER_VERSION_PATCH == 4, "Use the pinned FidelityFX SDK 1.1.4 headers");

// Private RHI integration surface. The SDK, native handles, synchronization and
// presentation ownership never appear in Render/Temporal's shared contract.
struct FsrGpuSynchronization
{
    // Must wait for ALL queues consuming this adapter's resources, including
    // SDK interpolation work. The callback and its context outlive the adapter.
    TemporalResult (*waitForGpuIdle)(void* context){ nullptr };
    void* context{ nullptr };
};

struct FsrBackendDevice
{
    TemporalBackend backend{ TemporalBackend::DX12 };
    FfxDevice device{ nullptr };
    size_t scratchBytes{ 0 }; // SDK query for one effect context.
    FfxErrorCode (*createInterface)(FfxInterface*, FfxDevice, void*, size_t, size_t){ nullptr };
    FfxErrorCode (*waitForPresents)(FfxSwapchain){ nullptr };
};

struct FsrContextDescription
{
    TemporalExtent maxRenderExtent;
    TemporalExtent displayExtent;
    bool depthInverted{ false };
    bool depthInfinite{ false };
    bool highDynamicRange{ true };
    bool motionVectorsAtDisplayResolution{ false };
    bool autoExposure{ true };
};

struct FsrUpscaleResources
{
    FfxCommandList commandList{ nullptr };
    FfxResource color{};
    FfxResource depth{};
    FfxResource motionVectors{};
    FfxResource exposure{};
    FfxResource reactiveMask{};
    FfxResource transparencyMask{};
    FfxResource output{};
};

// One SDK backend context per allocation makes partial-creation cleanup precise.
// SDK callbacks copy FfxInterface; the aligned scratch prefix retains the owner.
class FsrBackendStorage
{
public:
    FsrBackendStorage() = default;
    ~FsrBackendStorage();
    FsrBackendStorage(const FsrBackendStorage&) = delete;
    FsrBackendStorage& operator=(const FsrBackendStorage&) = delete;
    TemporalResult Initialize(const FsrBackendDevice& device);
    TemporalResult CreateSharedContext();
    TemporalResult DestroySharedContext();
    TemporalResult QueryDeviceCapabilities(TemporalBackend backend);
    FfxInterface& GetInterface() { return m_interface; }
    bool HasContext() const { return m_hasContext; }
    FfxUInt32 GetContextID() const { return m_contextID; }

private:
    static FfxErrorCode CreateContext(FfxInterface*, FfxEffect, FfxEffectBindlessConfig*, FfxUInt32*);
    static FfxErrorCode DestroyContext(FfxInterface*, FfxUInt32);
    static FsrBackendStorage* GetOwner(FfxInterface*);
    std::vector<std::max_align_t> m_scratch;
    FfxInterface m_interface{};
    FfxCreateBackendContextFunc m_createContext{ nullptr };
    FfxDestroyBackendContextFunc m_destroyContext{ nullptr };
    FfxUInt32 m_contextID{ 0 };
    bool m_hasContext{ false };
};

class FsrUpscaler
{
public:
    FsrUpscaler() = default;
    ~FsrUpscaler();
    FsrUpscaler(const FsrUpscaler&) = delete;
    FsrUpscaler& operator=(const FsrUpscaler&) = delete;
    TemporalResult Initialize(const FsrBackendDevice&, const FsrContextDescription&, FsrGpuSynchronization);
    TemporalResult Dispatch(const TemporalFrame&, const FsrUpscaleResources&, float sharpness = 0.0f);
    TemporalResult Shutdown(); // Drains GPU before releasing context/resources.
    bool IsInitialized() const { return m_initialized; }
    static TemporalResult GetRenderExtent(TemporalExtent display, TemporalQuality quality, TemporalExtent& render);

private:
    FsrBackendStorage m_effectBackend;
    FsrBackendStorage m_sharedBackend;
    std::unique_ptr<FfxFsr3UpscalerContext> m_context;
    FfxResourceInternal m_shared[3]{};
    uint32_t m_sharedCount{ 0 };
    FsrContextDescription m_description;
    FsrGpuSynchronization m_synchronization;
    TemporalFrame m_lastFrame;
    bool m_hasHistory{ false };
    bool m_hasSuccessfulFrame{ false };
    bool m_initialized{ false };
};

struct FsrFrameGenerationResources
{
    FfxCommandList commandList{ nullptr };
    FfxResource depth{};
    FfxResource motionVectors{};
    FfxResource hudlessColor{};
};

class FsrFrameGenerator
{
public:
    FsrFrameGenerator() = default;
    ~FsrFrameGenerator();
    FsrFrameGenerator(const FsrFrameGenerator&) = delete;
    FsrFrameGenerator& operator=(const FsrFrameGenerator&) = delete;
    TemporalResult Initialize(const FsrBackendDevice&, const FsrContextDescription&,
        const TemporalFrameGenerationConfig&, FfxSurfaceFormat backBufferFormat, FsrGpuSynchronization);
    TemporalResult Prepare(const TemporalFrame&, const FsrFrameGenerationResources&);
    // The binding must be a vendor replacement PLAYER swapchain, created by the
    // matching native helper. Caller registers separate UI before native Present.
    TemporalResult Configure(FfxSwapchain swapchain);
    // Serial baseline: wait for SDK presentation AND GPU before overwriting the
    // one prepared resource set. This is also the graph/lease release boundary.
    TemporalResult FinishFrame();
    TemporalResult DisableGeneration(FfxSwapchain swapchain);
    TemporalResult Shutdown();
    bool IsInitialized() const { return m_initialized; }
    uint64_t GetPreparedFrameID() const { return m_sdkFrameID; }
    bool HasPendingFrame() const { return m_framePending; }
    // Published only after callback/presentation drain. Counts SDK generation
    // output, not physical display scan-out or a requested interpolation count.
    uint32_t GetCompletedGeneratedFrameCount() const { return m_completedGeneratedFrameCount; }
#if CE_DEVELOPMENT && !CE_SHIPPING
    // Set only between drained frames. No callback/global lookup in normal use.
    void SetDevelopmentEvaluationFailure(std::function<bool()> reject) { m_developmentEvaluationFailure = std::move(reject); }
#endif

private:
    static FfxErrorCode Generate(const FfxFrameGenerationDispatchDescription*, void*);
    FfxErrorCode DispatchGeneratedFrame(const FfxFrameGenerationDispatchDescription&);
    TemporalResult Drain();
    FsrBackendStorage m_opticalFlowBackend;
    FsrBackendStorage m_interpolationBackend;
    FsrBackendStorage m_sharedBackend;
    std::unique_ptr<FfxOpticalflowContext> m_opticalFlow;
    std::unique_ptr<FfxFrameInterpolationContext> m_interpolation;
    FfxResourceInternal m_shared[5]{};
    uint32_t m_sharedCount{ 0 };
    FsrBackendDevice m_device;
    FsrContextDescription m_description;
    TemporalFrameGenerationConfig m_configuration;
    FfxSurfaceFormat m_backBufferFormat{ FFX_SURFACE_FORMAT_UNKNOWN };
    FsrGpuSynchronization m_synchronization;
    TemporalFrame m_preparedFrame;
    TemporalFrame m_lastFrame;
    FfxResource m_hudlessColor{};
    FfxSwapchain m_swapchain{ nullptr };
    uint64_t m_sdkFrameID{ 0 };
    bool m_hasHistory{ false };
    bool m_hasSuccessfulFrame{ false };
    bool m_framePending{ false };
    bool m_configured{ false };
    bool m_generated{ false };
    uint32_t m_completedGeneratedFrameCount{ 0 };
    FfxErrorCode m_generationError{ FFX_OK };
#if CE_DEVELOPMENT && !CE_SHIPPING
    std::function<bool()> m_developmentEvaluationFailure;
#endif
    bool m_reset{ true };
    bool m_initialized{ false };
    std::mutex m_mutex;
};

// Device eligibility is queried through AMD's backend, then each independent
// effect is really created. No dispatch/present occurs during this probe.
TemporalCapabilities QueryFsrCapabilities(const FsrBackendDevice&, const FsrContextDescription&,
    const TemporalFrameGenerationConfig&, FfxSurfaceFormat backBufferFormat);
TemporalResult FsrResult(FfxErrorCode error);
#endif
