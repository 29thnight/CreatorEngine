#pragma once
#include "TemporalReconstruction.h"
#include "SpatialPostEffects.h"
#include "TemporalProductSettings.h"
#include <mutex>
#include <vector>

// Commands submit intent. Only a live renderer/presenter publishes observations;
// requesting a provider never manufactures support, activation or GPU completion.
enum class TemporalTestFaultMode : uint8_t
{
    None = 0, Capability = 1, Dispatch = 2, FrameGenerationCapability = 3, FrameGenerationConfigure = 4,
    FrameGenerationPrepare = 5, FrameGenerationEvaluate = 6, FrameGenerationPresent = 7, FrameGenerationFinalConsumption = 8
};
inline const char* TemporalTestFaultModeName(TemporalTestFaultMode mode)
{
    switch (mode)
    {
    case TemporalTestFaultMode::None: return "none";
    case TemporalTestFaultMode::Capability: return "capability";
    case TemporalTestFaultMode::Dispatch: return "dispatch";
    case TemporalTestFaultMode::FrameGenerationCapability: return "fg-capability";
    case TemporalTestFaultMode::FrameGenerationConfigure: return "fg-configure";
    case TemporalTestFaultMode::FrameGenerationPrepare: return "fg-prepare";
    case TemporalTestFaultMode::FrameGenerationEvaluate: return "fg-evaluate";
    case TemporalTestFaultMode::FrameGenerationPresent: return "fg-present";
    case TemporalTestFaultMode::FrameGenerationFinalConsumption: return "fg-final-consumption";
    }
    return "unknown";
}
inline int64_t TemporalTestFaultNativeCode(TemporalTestFaultMode mode)
{
    return mode == TemporalTestFaultMode::None ? 0 : -45000 - static_cast<int64_t>(mode);
}
struct TemporalTestFaultSettings
{
    TemporalTestFaultMode mode{ TemporalTestFaultMode::None };
    TemporalProvider provider{ TemporalProvider::None };
    uint64_t revision{ 0 };
    uint64_t viewId{ 0 };
    bool operator==(const TemporalTestFaultSettings&) const = default;
};

struct TemporalRuntimeSettings
{
    TemporalProvider requestedUpscaler{ TemporalProvider::None };
    TemporalProvider requestedFrameGenerator{ TemporalProvider::None };
    TemporalQuality quality{ TemporalQuality::Quality };
    TemporalLatencyMode reflexMode{ TemporalLatencyMode::Off };
    SpatialPostSettings spatialPost;
    bool enabled{ true };
    uint32_t interpolatedFrameCount{ 1 };
    std::wstring runtimeDirectory;
    std::string dlssProjectId;
    // Developer-only transient control. Never copied to project/cooked DTOs.
    TemporalTestFaultSettings testFault;
    bool operator==(const TemporalRuntimeSettings&) const = default;
};

TemporalRuntimeSettings TemporalRuntimeSettingsFromProduct(const TemporalProductSettings& settings);
TemporalProductSettings TemporalProductSettingsFromRuntime(const TemporalRuntimeSettings& settings, bool fallbackAa);
// Zero means unknown/unsupported, not a guessed one-frame allowance.
uint32_t TemporalSupportedInterpolatedFrameCount(TemporalProvider provider, TemporalBackend backend,
    const std::vector<TemporalCapabilities>& capabilities);

// One identified native/SDK Present return. Publish the entire value under the
// existing runtime mutex. This is neither a GPU/SDK final-consumption proof nor
// an individual generated-frame identity, physical display event or timestamp.
struct TemporalPresenterObservation
{
    uint64_t observedQpc{ 0 }, sequence{ 0 }; // Owner-observed wrapper return, never display time.
    uint64_t realFrameId{ 0 }, publicationFrameId{ 0 }, viewId{ 0 }, sceneEpoch{ 0 };
    uint64_t requestGeneration{ 0 }, playerObservedGeneration{ 0 };
    TemporalProvider provider{ TemporalProvider::None };
    uint32_t interpolatedFrameCount{ 0 };
    TemporalTestFaultMode faultMode{ TemporalTestFaultMode::None };
    uint64_t faultRevision{ 0 };
    TemporalResult result;
    bool nativeGateActive{ false };
    bool valid{ false }; // Exact source identity is available; inspect result separately.
};

struct TemporalRuntimeSnapshot
{
    TemporalRuntimeSettings settings;
    TemporalRuntimeSettings requestedSettings; // Unmodified user intent, including during a capture.
    uint64_t testFaultConsumedRevision{ 0 }, testFaultConsumedCount{ 0 }, testFaultRealFrameId{ 0 };
    uint64_t testFaultViewId{ 0 }, testFaultSceneEpoch{ 0 };
    TemporalTestFaultMode testFaultConsumedMode{ TemporalTestFaultMode::None };
    TemporalResult testFaultResult; // Deliberate rejection, never an SDK/native return code.
    bool nativeCaptureExclusionActive{ false };
    bool playerLatencyHostRegistered{ false }; // Explicit host capability, not SDK/hardware support.
    uint64_t viewId{ 0 }, sceneEpoch{ 0 };
    TemporalPresentationTarget presentationTarget{ TemporalPresentationTarget::Unbound };
    uint64_t requestedGeneration{ 0 }, observedGeneration{ 0 }, playerObservedGeneration{ 0 };
    uint64_t lastRealFrameId{ 0 }, historyResetGeneration{ 0 };
    TemporalFrame frame;
    std::vector<TemporalCapabilities> capabilities;
    TemporalProvider selectedUpscaler{ TemporalProvider::None }, selectedFrameGenerator{ TemporalProvider::None };
    TemporalProvider activeUpscaler{ TemporalProvider::None }, activeFrameGenerator{ TemporalProvider::None };
    // Belongs to lastRealFrameId, not the latest requested settings. Recorded
    // dispatches are published only after graph submission; GPU completion is
    // still reported separately through renderGpuCompletedFrameId.
    TemporalQuality observedUpscaleQuality{ TemporalQuality::Quality };
    bool aaObserved{ false }, temporalAaApplied{ false }, fxaaRequested{ true }, fxaaApplied{ false };
    TemporalResult lastUpscaleResult, lastFrameGenerationResult;
    TemporalResult requestedUpscaleResult, requestedFrameGenerationResult;
    uint64_t generatedPresentationCount{ 0 }, realPresentationCount{ 0 };
    // SDK-generated output completion, separate from a native presentation result.
    uint64_t generatedSubmissionCount{ 0 }, generatedRealFrameId{ 0 };
    uint32_t generatedOrdinal{ 0 };
    uint32_t configuredInterpolatedFrameCount{ 0 }, activeInterpolatedFrameCount{ 0 };
    TemporalResult latencyResult;
    std::string latencyProvider;
    TemporalLatencyState reflex;
    SpatialPostSnapshot spatialPost;
    uint64_t renderSubmittedFrameId{ 0 }, renderGpuCompletedFrameId{ 0 };
    uint64_t sdkFinalConsumedFrameId{ 0 }, cpuPresentReturnedFrameId{ 0 }, latencyMarkerRealFrameId{ 0 };
    TemporalPresenterObservation presenterObservation;
    bool motionVectorsValid{ false }, rendererObserved{ false }, playerObserved{ false };
    bool motionStaticValid{ false }, motionSkinnedValid{ false }, motionInstancedValid{ false };
    bool motionDecalValid{ false }, motionAlphaValid{ false };
    std::string diagnostic;
};

class TemporalRuntimeControl
{
public:
    static TemporalRuntimeControl& Get();
    TemporalRuntimeSnapshot Snapshot() const;
    uint64_t Request(const TemporalRuntimeSettings& settings);
    // Once at startup, before device creation. Later CLI requests are transient.
    void InitializeProjectDefaults(const TemporalProductSettings& settings);
    uint64_t RequestHistoryReset();
    uint64_t RequestTestFault(TemporalTestFaultMode mode, TemporalProvider provider, uint64_t viewId = 0);
    // Called only after actual successful SDK readiness. A missing provider
    // cannot consume an injection or masquerade as a fault-induced fallback.
    bool ConsumeTestFault(const TemporalTestFaultSettings&, TemporalTestFaultMode,
        TemporalProvider, uint64_t realFrameId, uint64_t viewId, uint64_t sceneEpoch);
    // Player calls once before any graphics-device creation. Editor/default
    // hosts never opt in to plugins requiring a game-loop timing owner.
    void RegisterPlayerLatencyHost();
    void AcquireNativeCaptureExclusion();
    void ReleaseNativeCaptureExclusion();
    // Updates execute under a short CPU mutex. Do not call SDKs/GPU waits in them.
    template<class F> void PublishDiagnostic(F&& update)
    {
        std::lock_guard lock(m_mutex);
        update(m_snapshot);
    }
    template<class F> void PublishRenderer(F&& update)
    {
        std::lock_guard lock(m_mutex);
        update(m_snapshot);
        m_snapshot.rendererObserved = true;
    }
    template<class F> void PublishPlayer(F&& update)
    {
        std::lock_guard lock(m_mutex);
        update(m_snapshot);
        m_snapshot.playerObserved = true;
    }
private:
    TemporalRuntimeControl();
    mutable std::mutex m_mutex;
    TemporalRuntimeSnapshot m_snapshot;
    uint32_t m_nativeCaptureExclusions{ 0 };
    bool m_projectDefaultsInitialized{ false };
};

// Holds an effective native-only override without overwriting user intent.
// Concurrent requests are preserved and become effective when the last capture exits.
class TemporalNativeCaptureExclusion
{
public:
    TemporalNativeCaptureExclusion() { TemporalRuntimeControl::Get().AcquireNativeCaptureExclusion(); }
    ~TemporalNativeCaptureExclusion() { TemporalRuntimeControl::Get().ReleaseNativeCaptureExclusion(); }
    TemporalNativeCaptureExclusion(const TemporalNativeCaptureExclusion&) = delete;
    TemporalNativeCaptureExclusion& operator=(const TemporalNativeCaptureExclusion&) = delete;
};
