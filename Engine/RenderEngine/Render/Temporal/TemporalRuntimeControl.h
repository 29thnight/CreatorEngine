#pragma once
#include "TemporalReconstruction.h"
#include <mutex>
#include <vector>

// Commands submit intent. Only a live renderer/presenter publishes observations;
// requesting a provider never manufactures support, activation or GPU completion.
struct TemporalRuntimeSettings
{
    TemporalProvider requestedUpscaler{ TemporalProvider::None };
    TemporalProvider requestedFrameGenerator{ TemporalProvider::None };
    TemporalQuality quality{ TemporalQuality::Quality };
    bool enabled{ true };
    uint32_t interpolatedFrameCount{ 1 };
    std::wstring runtimeDirectory;
    std::string dlssProjectId;
    bool operator==(const TemporalRuntimeSettings&) const = default;
};

struct TemporalRuntimeSnapshot
{
    TemporalRuntimeSettings settings;
    TemporalRuntimeSettings requestedSettings; // Unmodified user intent, including during a capture.
    bool nativeCaptureExclusionActive{ false };
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
    TemporalResult latencyResult;
    std::string latencyProvider;
    uint64_t renderSubmittedFrameId{ 0 }, renderGpuCompletedFrameId{ 0 };
    uint64_t sdkFinalConsumedFrameId{ 0 }, cpuPresentReturnedFrameId{ 0 }, latencyMarkerRealFrameId{ 0 };
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
    uint64_t RequestHistoryReset();
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
