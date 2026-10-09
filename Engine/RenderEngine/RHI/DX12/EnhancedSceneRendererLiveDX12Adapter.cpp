#include "../../Render/Scene/EnhancedSceneRendererLiveDX12Adapter.h"

#include "../../GpuDiagnostics.h"
#include "../IDisplayPresentationSink.h"
#include "../../RHI/IRHIDeviceResources.h"
#include "../../RHI/RHIAssetEvictionPolicy.h"
#include "../../RHI/RHISubmissionThread.h"
#include "../../RHI/DX12/DX12CommandListPool.h"
#include "../../RHI/DX12/DX12DeviceResources.h"
#include "../../RHI/DX12/DX12GpuProfiler.h"
#include "../../RHI/DX12/DX12MeshCache.h"
#include "../../RHI/DX12/DX12PSOManager.h"
#include "../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../RHI/DX12/DX12TextureCache.h"
#include "../../RHI/DX12/DX12QueueRecorder.h"
#include "../../Render/Graph/EnhancedRenderGraph.h"
#include "../../Render/Graph/EnhancedGpuMeasurementHistory.h"
#include "PathFinder.h"

#include <cstdio>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <sstream>
#include <optional>
#include <utility>
#include <vector>
#include <wrl/client.h>

namespace
{
    void LiveDx12WriteJsonString(std::ostream& output, const std::string& value)
    {
        constexpr char hex[] = "0123456789abcdef";
        output << '"';
        for (const unsigned char byte : value)
        {
            if (byte == '"' || byte == '\\')
            {
                output << '\\' << static_cast<char>(byte);
            }
            else if (byte < 0x20)
            {
                output << "\\u00" << hex[byte >> 4] << hex[byte & 0x0f];
            }
            else
            {
                output << static_cast<char>(byte);
            }
        }
        output << '"';
    }

    uint32_t LiveDx12RequestedQueueExecutionMode()
    {
        char value[8]{};
        size_t bytes = 0;
        if (getenv_s(&bytes, value, sizeof(value), "CREATOR_RENDERGRAPH_QUEUE_EXECUTION") != 0)
        {
            return 0;
        }
        return std::strcmp(value, "2") == 0 ? 2u : std::strcmp(value, "1") == 0 ? 1u : 0u;
    }
}

struct EnhancedSceneRendererLiveDX12Adapter::Impl
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct DisplayResource
    {
        DisplayToken token{ kInvalidDisplayToken };
        ComPtr<ID3D12Resource> texture;
        HANDLE sharedHandle{ nullptr };
        uint64_t producerCompletion{};
        bool producerComplete{};
        bool producerCompletionLost{};
        std::shared_ptr<RHIDisplayConsumerLease> consumerLease{
            std::make_shared<RHIDisplayConsumerLease>() };
    };

    DX12DeviceResources resources;
    DX12CommandListPool commandPool;
    DX12PSOManager pipelines;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    DX12GpuProfiler profiler;
    DX12GpuProfiler computeProfiler;
    EnhancedRenderGraph::QueueEndpoint compute;
    bool asyncCompute{false};
    uint64_t computeSubmissions{0};
    DX12GpuProfiler::FrameTimings computeTimings;
    std::vector<DX12GpuProfiler::PassTiming> computeMerged;
    EnhancedRenderGraph::QueueEndpoint graphics;
    bool ownedQueueExecution{false};
    std::optional<uint32_t> queueExecutionModeOverride;
    uint32_t profilerPassCapacity{256};
    uint64_t queueSubmissions{0};
    uint64_t queueBatches{0};
    uint64_t queueBarriers{0};
    uint32_t lastQueueBarriers{0};
    uint64_t computeSubmittedFrames{0};
    uint64_t measuredOverlapSubmissions{0};
    bool evidenceTelemetry{false};
    GpuFrameToken currentProfilerToken;
    std::array<GpuFrameToken, kFrameCount> reportedProfilerTokens{};
    uint64_t minimumMeasurementSubmitTick{0};
    uint64_t predictedGainNanoseconds{0};
    // 프레임마다 재사용하는 수집물. 문자열 버퍼를 지켜 할당을 없앤다.
    DX12GpuProfiler::FrameTimings profilerTimings;
    std::vector<DX12GpuProfiler::PassTiming> profilerMerged;

    static constexpr size_t kMaxTimingSignatureBytes = 1024 * 1024;
    EnhancedGpuMeasurementHistory gpuMeasurements;
    struct MeasurementSubmission
    {
        GpuFrameToken token;
        RGMeasurementDomain domain{RGMeasurementDomain::Normal};
        uint32_t mode{0};
        GpuFrameToken computeToken;
        bool collectMeasurements{false};
    };
    std::array<MeasurementSubmission, kFrameCount> measurementSubmissions{};

    const MeasurementSubmission* FindMeasurementSubmission(const GpuFrameToken& token) const
    {
        if (!token.IsValid() || token.ringSlot >= measurementSubmissions.size())
        {
            return nullptr;
        }
        const auto& submission = measurementSubmissions[token.ringSlot];
        if (!submission.token.IsValid() || submission.token.submissionId != token.submissionId ||
            submission.token.renderViewId != token.renderViewId ||
            submission.token.engineFrameId != token.engineFrameId ||
            submission.token.cpuSubmitTick != token.cpuSubmitTick)
        {
            return nullptr;
        }
        return &submission;
    }

    void ClearMeasurements()
    {
        gpuMeasurements.Clear();
        measurementSubmissions = {};
    }

    // 최신 제출의 실패가 예전의 정상 표본을 되살려 쓰게 해서는 안 된다.
    void DiscardMeasurements(const GpuFrameToken& token)
    {
        const auto* submission = FindMeasurementSubmission(token);
        if (!submission || token.cpuSubmitTick < minimumMeasurementSubmitTick)
        {
            return;
        }
        if (!submission->collectMeasurements && !gpuMeasurements.Find(submission->domain, token.renderViewId))
        {
            return;
        }
        gpuMeasurements.Discard(submission->domain, token);
    }

    void StoreMeasurements(const GpuFrameToken& token)
    {
        const auto* submission = FindMeasurementSubmission(token);
        if (!submission || !submission->collectMeasurements || token.cpuSubmitTick < minimumMeasurementSubmitTick)
        {
            return;
        }
        const auto* previous = gpuMeasurements.Find(submission->domain, token.renderViewId);
        if (previous && previous->token.submissionId >= token.submissionId)
        {
            return;
        }
        EnhancedGpuMeasurementHistory::Sample sample;
        sample.token = token;
        const auto append = [&](const DX12GpuProfiler::FrameTimings& timings)
        {
            if (timings.token.renderViewId != token.renderViewId ||
                timings.token.engineFrameId != token.engineFrameId ||
                timings.token.submissionId != token.submissionId ||
                timings.ticksPerSecond == 0 || timings.overflowedPasses != 0 || timings.droppedSlices != 0)
            {
                return false;
            }
            for (const auto& slice : timings.slices)
            {
                const auto& identity = slice.identity;
                if (!identity.IsValid())
                {
                    continue; // 그래프 밖의 진단 scope에는 스케줄 비용이 없다.
                }
                if (identity.passIndex >= RGPassId::kInvalid ||
                    identity.graphSignature->size() > kMaxTimingSignatureBytes ||
                    slice.endTicks < slice.beginTicks)
                {
                    return false;
                }
                if (!sample.graphSignature)
                {
                    sample.graphSignature = identity.graphSignature;
                }
                else if (sample.graphSignature != identity.graphSignature &&
                    *sample.graphSignature != *identity.graphSignature)
                {
                    return false;
                }
                if (sample.nanoseconds.size() <= identity.passIndex)
                {
                    sample.nanoseconds.resize(static_cast<size_t>(identity.passIndex) + 1);
                }
                auto& cost = sample.nanoseconds[identity.passIndex];
                const long double nanoseconds = static_cast<long double>(slice.endTicks - slice.beginTicks) *
                    1'000'000'000.0L / static_cast<long double>(timings.ticksPerSecond);
                // MSVC의 long double도 double이므로 상한과 같을 때까지 보수적으로 버린다.
                if (nanoseconds >= static_cast<long double>(UINT64_MAX - cost.value_or(0)))
                {
                    return false;
                }
                // A measured zero remains present. Never fabricate a positive
                // duration to make an empty pass eligible for queue placement.
                cost = cost.value_or(0) + static_cast<uint64_t>(nanoseconds);
            }
            return true;
        };
        if (!append(profilerTimings) ||
            (submission->computeToken.IsValid() && !append(computeTimings)) || !sample.graphSignature)
        {
            DiscardMeasurements(token);
            return;
        }
        gpuMeasurements.Store(submission->domain, std::move(sample));
    }

    bool ConfigureQueueExecution(uint32_t mode, std::string& error)
    {
        if (mode != 0 && !graphics.queue)
        {
            if (!resources.GetPrimaryGraphicsQueue(graphics.queue, error))
            {
                return false;
            }
            graphics.profiler = &profiler;
            std::printf("[rg8.live] enabled=true profilerQueue=graphics primary=true\n");
        }
        if (mode == 2 && !computeProfiler.IsInitialized())
        {
            if (!compute.queue && !resources.CreateQueue(RHIQueueKind::Compute, compute.queue, error))
            {
                return false;
            }
            if (!computeProfiler.Initialize(resources.GetDevice(),
                DX12QueueService::NativeQueue(compute.queue), profilerPassCapacity, kFrameCount, error))
            {
                // Query-heap creation can succeed before readback allocation
                // fails. Do not mistake that partial initialization for success
                // on the next runtime mode-switch attempt.
                computeProfiler.Shutdown();
                return false;
            }
            compute.profiler = &computeProfiler;
            std::printf("[rg8.compute] enabled=true clocks=per-queue minimumNs=1000\n");
        }
        ownedQueueExecution = mode != 0;
        asyncCompute = mode == 2;
        return true;
    }

    ComPtr<ID3D12Resource> fogCloudNeutral;
    std::vector<DisplayResource> activeDisplays;
    std::vector<DisplayResource> retiredDisplays;
    DisplayToken nextDisplayToken{ 1 };
    uint32_t commandPoolFrame{ 0 };
};

EnhancedSceneRendererLiveDX12Adapter::EnhancedSceneRendererLiveDX12Adapter()
    : m_impl(std::make_unique<Impl>())
{
}

EnhancedSceneRendererLiveDX12Adapter::~EnhancedSceneRendererLiveDX12Adapter()
{
    ShutdownPipeline();
    ShutdownInterop();
}

bool EnhancedSceneRendererLiveDX12Adapter::Initialize(
    uint32_t width, uint32_t height, std::string& outError)
{
    Impl& impl = *m_impl;
    if (!impl.resources.Initialize(width, height, outError)) return false;
    impl.asyncCompute = false;
    impl.ownedQueueExecution = false;
    impl.compute = {};
    impl.computeSubmissions = 0;
    impl.ClearMeasurements();
    impl.currentProfilerToken = {};
    impl.reportedProfilerTokens = {};
    impl.minimumMeasurementSubmitTick = 0;
    impl.graphics = {};
    impl.queueSubmissions = impl.queueBatches = 0;
    impl.queueBarriers = impl.lastQueueBarriers = 0;
    impl.computeSubmittedFrames = impl.measuredOverlapSubmissions = impl.predictedGainNanoseconds = 0;
    char evidenceFlag[8]{};
    size_t evidenceFlagBytes = 0;
    impl.evidenceTelemetry = getenv_s(&evidenceFlagBytes, evidenceFlag, sizeof(evidenceFlag),
        "CREATOR_RG8_EVIDENCE") == 0 && std::strcmp(evidenceFlag, "1") == 0;
    // Every execution mode uses the primary graphics queue, so switching mode
    // preserves both the graphics clock and the warm pass-measurement history.
    ID3D12CommandQueue* profilerQueue = impl.resources.GetCommandQueue();
    constexpr uint32_t defaultProfilerPassCapacity = 256;
    uint32_t profilerPassCapacity = defaultProfilerPassCapacity;
#if defined(_DEBUG)
    // 회귀 검사용으로만 질의 슬롯을 좁힌다. 실제 pass 실행은 그대로 두고
    // 빠진 timestamp가 렌더러와 Collector 양쪽에 계상되는지 자극한다.
    char queryLimit[16]{};
    size_t queryLimitBytes = 0;
    if (0 == getenv_s(&queryLimitBytes, queryLimit, sizeof(queryLimit),
            "CREATOR_DX12_GPU_QUERY_LIMIT") && queryLimitBytes > 1 &&
        queryLimitBytes <= sizeof(queryLimit))
    {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(queryLimit, &end, 10);
        if (end != queryLimit && *end == '\0' && parsed >= 1 && parsed <= defaultProfilerPassCapacity)
            profilerPassCapacity = static_cast<uint32_t>(parsed);
    }
    if (profilerPassCapacity != defaultProfilerPassCapacity)
        std::printf("[GPU profiler] query capacity %u (Debug validation)\n",
            profilerPassCapacity);
#endif
    if (!impl.commandPool.Initialize(impl.resources, 4, kFrameCount, outError) ||
        !impl.pipelines.Initialize(&impl.resources,
            PathFinder::CachePath("RHI/DX12/dx12_live.cache").wstring(), outError) ||
        !impl.rootSignatures.Initialize(&impl.resources, outError) ||
        !impl.meshCache.Initialize(&impl.resources, outError) ||
        !impl.textureCache.Initialize(&impl.resources, outError) ||
        !impl.profiler.Initialize(impl.resources.GetDevice(),
            profilerQueue, profilerPassCapacity, kFrameCount, outError))
    {
        return false;
    }
    impl.profilerPassCapacity = profilerPassCapacity;
    if (!impl.ConfigureQueueExecution(impl.queueExecutionModeOverride.value_or(LiveDx12RequestedQueueExecutionMode()), outError))
    {
        return false;
    }
    impl.commandPoolFrame = 0;

    SetDiagnosticsDeviceResources(&impl.resources);
    return true;
}

bool EnhancedSceneRendererLiveDX12Adapter::Resize(
    uint32_t width, uint32_t height, std::string& outError)
{
    if (!m_impl->resources.Resize(width, height, outError))
    {
        return false;
    }
    m_impl->ClearMeasurements();
    LARGE_INTEGER resizeTick{};
    QueryPerformanceCounter(&resizeTick);
    m_impl->minimumMeasurementSubmitTick = static_cast<uint64_t>(resizeTick.QuadPart);
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::ShutdownPipeline()
{
    Impl& impl = *m_impl;
    if (impl.resources.IsInitialized() &&
        GetRHISubmissionThread().GetOwnerStats(&impl.resources).registered)
    {
        std::string lifecycleError;
        bool drained = impl.resources.DrainForLifecycle(
            RHILifecycleCommand::BackendShutdown, lifecycleError);
        if (!drained && GetRHISubmissionThread().GetOwnerStats(&impl.resources).faulted)
        {
            drained = impl.resources.DrainForLifecycle(RHILifecycleCommand::UnrecoverableDeviceError, lifecycleError);
        }
        if (drained)
        {
            drained = impl.resources.ShutdownQueueService(lifecycleError);
        }
        if (!drained)
        {
            OutputDebugStringA(("[DX12 live] Fatal: releasing pipeline before verified GPU idle/device loss: " +
                lifecycleError + "\n").c_str());
            std::fprintf(stderr, "Fatal DX12 pipeline teardown invariant: GPU idle/device loss unproven: %s\n",
                lifecycleError.c_str());
            std::fflush(stderr);
            std::terminate();
        }
    }

    // active는 공용 파이프라인 해체가 RetireDisplayTexture로 비워야 한다.
    // 부분 초기화 실패에서도 누락되지 않도록 여기서 남은 항목을 격리한다.
    for (Impl::DisplayResource& display : impl.activeDisplays)
    {
        display.producerCompletion = impl.resources.GetLastSignaledFenceValue();
        impl.retiredDisplays.push_back(std::move(display));
    }
    impl.activeDisplays.clear();
    if (!GetRHISubmissionThread().GetOwnerStats(&impl.resources).faulted)
    {
        for (auto& display : impl.retiredDisplays)
        {
            display.producerComplete = true;
        }
    }
    else
    {
        for (auto& display : impl.retiredDisplays)
        {
            display.producerCompletionLost = !display.producerComplete;
        }
    }
    impl.fogCloudNeutral.Reset();

    if (impl.resources.IsInitialized())
    {
        GpuDiagnostics::LogCensus("라이브 파이프라인 해체 직전", true);
    }
    SetDiagnosticsDeviceResources(nullptr);

    impl.computeProfiler.Shutdown();
    if (impl.graphics.queue)
    {
        const auto graphicsPool = DX12QueueService::QueryRecordingPool(impl.graphics.queue);
        const auto computePool = DX12QueueService::QueryRecordingPool(impl.compute.queue);
        std::printf("[rg8.pool] created=%llu reused=%llu leased=%llu cached=%llu\n",
            static_cast<unsigned long long>(graphicsPool.created + computePool.created),
            static_cast<unsigned long long>(graphicsPool.reused + computePool.reused),
            static_cast<unsigned long long>(graphicsPool.leased + computePool.leased),
            static_cast<unsigned long long>(graphicsPool.cached + computePool.cached));
    }
    if (impl.compute.queue)
    {
        std::printf("[rg8.compute] submissions=%llu\n", static_cast<unsigned long long>(impl.computeSubmissions));
    }
    impl.compute = {};
    impl.profiler.Shutdown();
    if (impl.graphics.queue)
    {
        std::printf("[rg8.live] submissions=%llu batches=%llu profilerQueue=graphics\n",
            static_cast<unsigned long long>(impl.queueSubmissions), static_cast<unsigned long long>(impl.queueBatches));
        std::printf("[rg8.barriers] total=%llu last=%u\n",
            static_cast<unsigned long long>(impl.queueBarriers), impl.lastQueueBarriers);
        std::printf("[rg8.schedule] placement=overlap computeSubmittedFrames=%llu measuredOverlapSubmissions=%llu predictedGainNs=%llu predictionCalibrated=false\n",
            static_cast<unsigned long long>(impl.computeSubmittedFrames),
            static_cast<unsigned long long>(impl.measuredOverlapSubmissions),
            static_cast<unsigned long long>(impl.predictedGainNanoseconds));
    }
    impl.graphics = {};
    impl.commandPool.Shutdown();
    impl.textureCache.Shutdown();
    impl.meshCache.Shutdown();
    impl.rootSignatures.Shutdown();
    impl.pipelines.Shutdown();
    impl.resources.Shutdown();
    impl.ClearMeasurements();
    impl.currentProfilerToken = {};
    impl.ownedQueueExecution = false;
    impl.asyncCompute = false;
}

void EnhancedSceneRendererLiveDX12Adapter::ShutdownInterop()
{
    Impl& impl = *m_impl;
    const auto closeAll = [](std::vector<Impl::DisplayResource>& displays)
    {
        for (Impl::DisplayResource& display : displays)
        {
            if (nullptr != display.sharedHandle) ::CloseHandle(display.sharedHandle);
            display.sharedHandle = nullptr;
            display.texture.Reset();
        }
        displays.clear();
    };
    closeAll(impl.activeDisplays);
    closeAll(impl.retiredDisplays);
}

bool EnhancedSceneRendererLiveDX12Adapter::IsInitialized() const
{
    return m_impl->resources.IsInitialized();
}

RHIVideoMemoryInfo EnhancedSceneRendererLiveDX12Adapter::QueryVideoMemory() const
{
    if (!IsInitialized())
    {
        return {};
    }
    return m_impl->resources.QueryVideoMemory();
}

bool EnhancedSceneRendererLiveDX12Adapter::QueryVideoMemory(uint64_t& usedMB,
    uint64_t& budgetMB) const
{
    if (!IsInitialized()) return false;
    const RHIVideoMemoryInfo memory = m_impl->resources.QueryVideoMemory();
    if (memory.budgetMB == 0) return false;
    usedMB = memory.usedMB;
    budgetMB = memory.budgetMB;
    return true;
}

EnhancedSceneRendererLiveDX12Adapter::CounterSnapshot
EnhancedSceneRendererLiveDX12Adapter::GetCounterSnapshot() const
{
    CounterSnapshot out{};
    if (!IsInitialized()) return out;
    const RHIUploadStats upload = m_impl->resources.GetUploadStats();
    const auto descriptor = m_impl->resources.GetDescriptorRecycler().GetStats();
    out.uploadBytes = upload.bytesAllocated;
    out.uploadOverflows = upload.batchRollbacks;
    out.descriptorAllocations = descriptor.allocations;
    out.descriptorOverflows = descriptor.overflows;
    return out;
}

bool EnhancedSceneRendererLiveDX12Adapter::BeginFrame(std::string& outError)
{
    Impl& impl = *m_impl;
    impl.currentProfilerToken = {};
    // Apply mode changes at frame admission, never to an in-flight submission.
    // Environment changes and the explicit override both retain warm caches.
    if (!impl.ConfigureQueueExecution(impl.queueExecutionModeOverride.value_or(LiveDx12RequestedQueueExecutionMode()), outError))
    {
        return false;
    }
    if (impl.graphics.queue)
    {
        impl.graphics.queue->CollectCompleted();
    }
    if (impl.compute.queue)
    {
        impl.compute.queue->CollectCompleted();
    }
    if (!impl.resources.BeginFrame(outError)) return false;
    impl.commandPool.BeginFrame(impl.commandPoolFrame);
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::AbortFrame()
{
    m_impl->DiscardMeasurements(m_impl->currentProfilerToken);
    m_impl->resources.AbortFrame();
}

bool EnhancedSceneRendererLiveDX12Adapter::EndFrame(std::string& outError)
{
    Impl& impl = *m_impl;
    if (!impl.resources.EndFrame(outError)) return false;
    impl.commandPoolFrame = (impl.commandPoolFrame + 1u) % kFrameCount;
    if (impl.ownedQueueExecution && !impl.resources.WaitForLastFrameSubmission(outError))
    {
        return false;
    }
    return true;
}

bool EnhancedSceneRendererLiveDX12Adapter::UsesOwnedQueueExecution() const
{
    return m_impl->ownedQueueExecution;
}

uint32_t EnhancedSceneRendererLiveDX12Adapter::GetQueueExecutionMode() const
{
    return m_impl->asyncCompute ? 2u : m_impl->ownedQueueExecution ? 1u : 0u;
}

bool EnhancedSceneRendererLiveDX12Adapter::SetQueueExecutionMode(uint32_t mode, std::string& outError)
{
    if (mode > 2)
    {
        outError = "Queue execution mode must be 0, 1 or 2";
        return false;
    }
    m_impl->queueExecutionModeOverride = mode;
    outError.clear();
    return true;
}

bool EnhancedSceneRendererLiveDX12Adapter::BeginOwnedQueueRecording(std::string& outError)
{
    return m_impl->resources.BeginQueueFrame(m_impl->graphics.queue, outError);
}

bool EnhancedSceneRendererLiveDX12Adapter::SubmitOwnedGraph(
    const std::shared_ptr<EnhancedRenderGraph>& graph, std::shared_ptr<const void> owner,
    double& recordingMilliseconds, std::string& outError)
{
    Impl& impl = *m_impl;
    DX12QueueRecorder recorder(impl.resources);
    EnhancedRenderGraph::QueueExecution execution;
    const auto domain = graph->GetMeasurementDomain();
    if (impl.currentProfilerToken.IsValid() &&
        impl.currentProfilerToken.ringSlot < impl.measurementSubmissions.size())
    {
        impl.measurementSubmissions[impl.currentProfilerToken.ringSlot].domain = domain;
    }
    const auto* view = impl.gpuMeasurements.FindFresh(domain, impl.currentProfilerToken,
        impl.profiler.Calibration().cpuTicksPerSecond);
    bool checkedSignature = false;
    bool signatureMatches = false;
    const auto hints = graph->MeasuredQueueHints([&](const GpuPassTimingIdentity& identity) -> std::optional<uint64_t>
    {
        if (!view || !identity.IsValid())
        {
            return std::nullopt;
        }
        // 한 번의 조회는 한 그래프의 패스만 방문한다. 큰 선언 원문은 한 번만 비교한다.
        if (!checkedSignature)
        {
            signatureMatches = view->graphSignature == identity.graphSignature ||
                *view->graphSignature == *identity.graphSignature;
            checkedSignature = true;
        }
        const auto& costs = view->nanoseconds;
        return signatureMatches && identity.passIndex < costs.size() ? costs[identity.passIndex] : std::nullopt;
    });
    if (!graph->SubmitQueues(graph, recorder, impl.graphics, impl.asyncCompute ? &impl.compute : nullptr,
        hints, impl.asyncCompute ? 1000 : 0,
        std::move(owner), execution, outError))
    {
        return false;
    }
    recordingMilliseconds = execution.recordingMilliseconds;
    ++impl.queueSubmissions;
    impl.computeSubmissions += execution.computeBatches;
    impl.queueBatches += execution.submittedBatches;
    impl.queueBarriers += execution.plannedBarriers;
    impl.lastQueueBarriers = execution.plannedBarriers;
    if (execution.computeBatches != 0)
    {
        ++impl.computeSubmittedFrames;
        impl.predictedGainNanoseconds += execution.predictedSerialNanoseconds - execution.predictedNanoseconds;
    }
    if (impl.evidenceTelemetry)
    {
        const auto& diagnostics = graph->GetQueueDiagnostics();
        const auto& token = impl.currentProfilerToken;
        std::ostringstream line;
        line.precision(17);
        line << std::boolalpha << "[rg8.execution] {\"schemaVersion\":2,\"mode\":" << GetQueueExecutionMode()
             << ",\"requestedExecutionMode\":" << diagnostics.requestedExecutionMode
             << ",\"effectiveExecutionMode\":" << diagnostics.effectiveExecutionMode
             << ",\"measurementDomain\":\"" << RGMeasurementDomainName(domain) << '"'
             << ",\"fallbackReason\":\"" << RGQueueFallbackReasonName(diagnostics.schedule.fallbackReason) << '"'
             << ",\"backendGeneration\":" << GetBackendGeneration()
             << ",\"frameId\":" << token.engineFrameId << ",\"viewId\":" << token.renderViewId
             << ",\"submissionId\":" << token.submissionId << ",\"captureGeneration\":" << token.captureGeneration
             << ",\"specialized\":" << diagnostics.specialized << ",\"predictionCalibrated\":false,\"execution\":{";
        line << "\"scheduleMilliseconds\":" << diagnostics.execution.scheduleMilliseconds;
        line << ",\"recordingMilliseconds\":" << diagnostics.execution.recordingMilliseconds;
        line << ",\"submissionMilliseconds\":" << diagnostics.execution.submissionMilliseconds;
        line << ",\"totalMilliseconds\":" << diagnostics.execution.totalMilliseconds;
        line << ",\"recordedLists\":" << graph->GetStats().recordedLists;
        line << ",\"recordWorkers\":" << graph->GetStats().recordWorkers;
        line << ",\"recordUnits\":" << graph->GetStats().recordUnits;
        line << ",\"recordingWaveCount\":" << graph->GetStats().recordingWaveCount;
        line << ",\"plannedBatches\":" << diagnostics.execution.plannedBatches;
        line << ",\"plannedComputeBatches\":" << diagnostics.execution.plannedComputeBatches;
        line << ",\"plannedWaits\":" << diagnostics.execution.plannedWaits;
        line << ",\"submittedBatches\":" << diagnostics.execution.submittedBatches;
        line << ",\"computeBatches\":" << diagnostics.execution.computeBatches;
        line << ",\"submittedWaits\":" << diagnostics.execution.submittedWaits;
        line << ",\"plannedBarriers\":" << diagnostics.execution.plannedBarriers;
        line << ",\"prologueBarriers\":" << diagnostics.execution.prologueBarriers;
        line << ",\"epilogueBarriers\":" << diagnostics.execution.epilogueBarriers;
        line << ",\"predictedSerialNanoseconds\":" << diagnostics.execution.predictedSerialNanoseconds;
        line << ",\"predictedNanoseconds\":" << diagnostics.execution.predictedNanoseconds;
        line << ",\"submissionAttempted\":" << diagnostics.execution.submissionAttempted;
        line << ",\"recoveryRequired\":" << diagnostics.execution.recoveryRequired;
        line << ",\"completed\":" << diagnostics.execution.completed;
        line << "},\"schedule\":{\"compileGeneration\":" << diagnostics.schedule.compileGeneration
             << ",\"usesCompute\":" << diagnostics.schedule.usesCompute
             << ",\"predictionComplete\":" << diagnostics.schedule.predictionComplete
             << ",\"predictionCalibrated\":" << diagnostics.schedule.predictionCalibrated
             << ",\"fallbackReason\":\"" << RGQueueFallbackReasonName(diagnostics.schedule.fallbackReason) << '"'
             << ",\"eligibleComputePasses\":" << diagnostics.schedule.eligibleComputePasses
             << ",\"rejectedStatePasses\":" << diagnostics.schedule.rejectedStatePasses
             << ",\"measuredPasses\":" << diagnostics.schedule.measuredPasses
             << ",\"missingMeasurementPasses\":" << diagnostics.schedule.missingMeasurementPasses << ",\"entries\":[";
        const char* separator = "";
        for (const auto& entry : diagnostics.schedule.entries)
        {
            line << separator << "{\"pass\":" << entry.pass << ",\"queue\":"
                 << static_cast<uint32_t>(entry.queue) << '}';
            separator = ",";
        }
        line << "],\"waits\":[";
        separator = "";
        for (const auto& wait : diagnostics.schedule.waits)
        {
            line << separator << "{\"producer\":" << wait.producer << ",\"consumer\":" << wait.consumer
                 << ",\"resource\":" << wait.resource << '}';
            separator = ",";
        }
        line << "]},\"batches\":[";
        separator = "";
        for (const auto& batch : diagnostics.batches)
        {
            line << separator << "{\"queue\":" << static_cast<uint32_t>(batch.queue)
                 << ",\"barriers\":" << batch.barriers << ",\"submitted\":" << batch.submitted
                 << ",\"passes\":[";
            const char* passSeparator = "";
            for (const auto pass : batch.passes)
            {
                line << passSeparator << pass;
                passSeparator = ",";
            }
            line << "]}";
            separator = ",";
        }
        line << "],\"waits\":[";
        separator = "";
        for (const auto& wait : diagnostics.waits)
        {
            line << separator << "{\"producerBatch\":" << wait.producerBatch
                 << ",\"consumerBatch\":" << wait.consumerBatch
                 << ",\"submitted\":" << wait.submitted << '}';
            separator = ",";
        }
        line << "]}";
        std::printf("%s\n", line.str().c_str());
    }
    return impl.resources.JoinQueueFrame(execution.completion, outError);
}

IRHIParallelCommandPool& EnhancedSceneRendererLiveDX12Adapter::CommandPool()
{
    return m_impl->commandPool;
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::GetBackendGeneration() const
{
    return GetRHISubmissionThread().GetOwnerGeneration(&m_impl->resources);
}

bool EnhancedSceneRendererLiveDX12Adapter::EnqueueRecordedBatch(
    RHIRecordedBatch&& batch, RHISubmissionTicket& outTicket,
    std::string& outError)
{
    Impl& impl = *m_impl;
    return GetRHISubmissionThread().EnqueueRecordedBatch(&impl.resources,
        impl.resources, std::move(batch), outTicket, outError);
}

void EnhancedSceneRendererLiveDX12Adapter::WaitForGpu()
{
    if (IsInitialized()) m_impl->resources.WaitForGpu();
}

bool EnhancedSceneRendererLiveDX12Adapter::DrainForLifecycle(
    RHILifecycleCommand command, std::string& outError)
{
    if (!IsInitialized())
    {
        return true;
    }
    if (!m_impl->resources.DrainForLifecycle(command, outError))
    {
        return false;
    }
    if (command == RHILifecycleCommand::BackendShutdown || command == RHILifecycleCommand::UnrecoverableDeviceError)
    {
        return m_impl->resources.ShutdownQueueService(outError);
    }
    if (m_impl->graphics.queue)
    {
        m_impl->graphics.queue->CollectCompleted();
    }
    if (m_impl->compute.queue)
    {
        m_impl->compute.queue->CollectCompleted();
    }
    return true;
}

bool EnhancedSceneRendererLiveDX12Adapter::HasDeviceLossProof() const
{
    return GetRHISubmissionThread().GetOwnerStats(&m_impl->resources).faulted;
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::GetCompletedFenceValue() const
{
    return m_impl->resources.GetCompletedFenceValue();
}

bool EnhancedSceneRendererLiveDX12Adapter::SignalEventOnFenceValue(uint64_t value, void* event) const
{
    return IsInitialized() && m_impl->resources.SignalEventOnFenceValue(value, static_cast<HANDLE>(event));
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::GetLastSignaledFenceValue() const
{
    return m_impl->resources.GetLastSignaledFenceValue();
}

bool EnhancedSceneRendererLiveDX12Adapter::ConsumeSubmissionFailure(std::string& outError)
{
    Impl& impl = *m_impl;
    if (impl.ownedQueueExecution && !impl.resources.QueryQueueCapabilities().graphics)
    {
        outError = "Owned queue service was revoked; backend reinitialization is required.";
        return true;
    }
    if (GetRHISubmissionThread().ConsumeFailure(&impl.resources, outError))
    {
        return true;
    }
    if (ID3D12Device* device = impl.resources.GetDevice())
    {
        const HRESULT reason = device->GetDeviceRemovedReason();
        if (FAILED(reason))
        {
            outError = "DX12 producer device was removed";
            impl.resources.AppendDeviceRemovedReport(reason, outError);
            GetRHISubmissionThread().MarkUnrecoverableDeviceError(&impl.resources, outError);
            return true;
        }
    }
    return false;
}

IRenderDeviceServices& EnhancedSceneRendererLiveDX12Adapter::Resources()
{
    return m_impl->resources;
}

IRenderPipelineCache& EnhancedSceneRendererLiveDX12Adapter::Pipelines()
{
    return m_impl->pipelines;
}

IRenderRootSignatureCache& EnhancedSceneRendererLiveDX12Adapter::RootSignatures()
{
    return m_impl->rootSignatures;
}

IRenderMeshCache& EnhancedSceneRendererLiveDX12Adapter::MeshCache()
{
    return m_impl->meshCache;
}

IRenderTextureCache& EnhancedSceneRendererLiveDX12Adapter::TextureCache()
{
    return m_impl->textureCache;
}

IRHIGpuProfiler* EnhancedSceneRendererLiveDX12Adapter::Profiler()
{
    return &m_impl->profiler;
}

bool EnhancedSceneRendererLiveDX12Adapter::CreateDisplayTexture(
    uint32_t width, uint32_t height, RHITextureHandle& outTexture,
    DisplayToken& outToken, std::string& outError)
{
    outTexture = {};
    outToken = kInvalidDisplayToken;
    Impl& impl = *m_impl;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    Impl::DisplayResource display;
    if (FAILED(impl.resources.GetDevice()->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&display.texture))))
    {
        outError = "공유 텍스처 생성 실패";
        return false;
    }

    outTexture = impl.resources.RegisterExternalTexture(display.texture.Get());
    if (!outTexture.IsValid())
    {
        outError = "공유 텍스처 RHI 등록 실패";
        return false;
    }

    if (FAILED(impl.resources.GetDevice()->CreateSharedHandle(display.texture.Get(),
        nullptr, GENERIC_ALL, nullptr, &display.sharedHandle)) ||
        nullptr == display.sharedHandle)
    {
        impl.resources.ReleaseTexture(outTexture);
        outTexture = {};
        outError = "공유 핸들 생성 실패";
        return false;
    }

    display.token = impl.nextDisplayToken++;
    if (kInvalidDisplayToken == impl.nextDisplayToken) ++impl.nextDisplayToken;
    outToken = display.token;
    impl.activeDisplays.push_back(std::move(display));
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::RetireDisplayTexture(DisplayToken token)
{
    if (kInvalidDisplayToken == token) return;
    Impl& impl = *m_impl;
    for (auto it = impl.activeDisplays.begin(); it != impl.activeDisplays.end(); ++it)
    {
        if (it->token != token) continue;
        it->producerCompletion = impl.resources.GetLastSignaledFenceValue();
        impl.retiredDisplays.push_back(std::move(*it));
        impl.activeDisplays.erase(it);
        return;
    }
}

void EnhancedSceneRendererLiveDX12Adapter::CollectRetiredDisplays()
{
    Impl& impl = *m_impl;
    const auto completed = impl.resources.GetCompletedFenceValue();
    std::erase_if(impl.retiredDisplays, [&](auto& display)
    {
        if (display.producerCompletionLost || (!display.producerComplete && display.producerCompletion > completed)
            || display.consumerLease.use_count() != 1
            || display.consumerLease->m_completionLost.load(std::memory_order_acquire))
        {
            return false;
        }
        if (display.sharedHandle)
        {
            CloseHandle(display.sharedHandle);
            display.sharedHandle = nullptr;
        }
        return true;
    });
}

bool EnhancedSceneRendererLiveDX12Adapter::CanReuseDisplayTexture(DisplayToken token) const
{
    for (const Impl::DisplayResource& display : m_impl->activeDisplays)
    {
        if (display.token == token)
        {
            return display.consumerLease.use_count() == 1 &&
                !display.consumerLease->m_completionLost.load(std::memory_order_acquire);
        }
    }
    return false;
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::OpenDisplayTexture(
    IDisplayPresentationSink& sink, DisplayToken token) const
{
    if (kInvalidDisplayToken == token)
    {
        return 0;
    }
    for (const Impl::DisplayResource& display : m_impl->activeDisplays)
    {
        if (display.token == token &&
            !display.consumerLease->m_completionLost.load(std::memory_order_acquire))
        {
            // 호출자는 생산자 슬롯 선택과 같은 뮤텍스를 쥔다. sink는 잠금을
            // 놓기 전에 이 참조를 확보해야 하며 CPU 드로우 데이터가 아직
            // 기록·제출되지 않은 동안에도 참조를 유지한다.
            return sink.OpenSharedTexture(display.sharedHandle, display.consumerLease);
        }
    }
    return 0;
}

size_t EnhancedSceneRendererLiveDX12Adapter::GetRetiredDisplayCount() const
{
    return m_impl->retiredDisplays.size();
}

bool EnhancedSceneRendererLiveDX12Adapter::CreateFogCloudNeutral(
    RHITextureHandle& outTexture, std::string& outError)
{
    Impl& impl = *m_impl;
    if (impl.fogCloudNeutral)
    {
        return outTexture.IsValid();
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    if (FAILED(impl.resources.GetDevice()->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&impl.fogCloudNeutral))))
    {
        outError = "포그 중립 구름 텍스처 생성 실패";
        return false;
    }
    impl.fogCloudNeutral->SetName(L"Fog.CloudNeutral");

    const RHIBufferSlice staging = impl.resources.AllocateUpload(
        RHIUploadRequest{ D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
            RHIUploadUsage::TextureCopy, 1 });
    if (!staging.IsValid())
    {
        impl.fogCloudNeutral.Reset();
        outError = "포그 중립 구름 업로드 링 할당 실패";
        return false;
    }
    // ★ 이것은 `RHINeutralTexel::kWhite` 와 숫자가 같지만 **다른 값**이다. 여기의
    //   흰색은 "구름 그림자 없음"(포그 셰이더가 곱하는 가시도 1)이고, 저쪽은 PBR
    //   재질 슬롯의 중립이다. 두 뜻이 한 상수를 공유하면 한쪽 규약이 바뀔 때
    //   다른 쪽이 조용히 따라간다 — W3 는 이 자리를 일부러 합치지 않았다.
    //   또한 이 경로는 DX12 전용이라 백엔드 사이에 갈릴 짝 자체가 없다.
    const uint8_t white[4]{ 255, 255, 255, 255 };
    std::memcpy(staging.cpuAddress, white, sizeof(white));

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = impl.fogCloudNeutral.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = impl.resources.Resolve(staging.buffer);
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = staging.offset;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 1;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    impl.resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    outTexture = impl.resources.RegisterExternalTexture(impl.fogCloudNeutral.Get());
    if (!outTexture.IsValid())
    {
        impl.fogCloudNeutral.Reset();
        outError = "포그 중립 구름 RHI 등록 실패";
        return false;
    }
    const RHITransition toSrv[] = {
        { outTexture, RHIResourceState::CopyDest, RHIResourceState::ShaderResource } };
    impl.resources.TransitionResources(toSrv);
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::ReleaseFogCloudNeutral(
    RHITextureHandle& texture)
{
    if (texture.IsValid()) m_impl->resources.ReleaseTexture(texture);
    texture = {};
    m_impl->fogCloudNeutral.Reset();
}

GpuFrameToken EnhancedSceneRendererLiveDX12Adapter::BeginProfilerFrame(
    uint64_t engineFrameId, uint64_t submissionId, uint64_t renderViewId, uint64_t captureGeneration,
    bool diagnosticCapture)
{
    GpuFrameToken computeToken;
    if (m_impl->asyncCompute)
    {
        computeToken = m_impl->computeProfiler.BeginFrame(engineFrameId, submissionId, renderViewId, captureGeneration);
    }
    m_impl->currentProfilerToken =
        m_impl->profiler.BeginFrame(engineFrameId, submissionId, renderViewId, captureGeneration);
    const auto& token = m_impl->currentProfilerToken;
    if (token.IsValid() && token.ringSlot < m_impl->measurementSubmissions.size())
    {
        m_impl->measurementSubmissions[token.ringSlot] = {token,
            diagnosticCapture ? RGMeasurementDomain::Capture : RGMeasurementDomain::Normal,
            GetQueueExecutionMode(), computeToken,
            m_impl->ownedQueueExecution || m_impl->evidenceTelemetry || m_impl->queueExecutionModeOverride.has_value()};
    }
    return m_impl->currentProfilerToken;
}

void EnhancedSceneRendererLiveDX12Adapter::ResolveProfilerFrame(const GpuFrameToken& token)
{
    m_impl->profiler.ResolveFrame(m_impl->resources.GetCommandList(), token);
    const auto* submission = m_impl->FindMeasurementSubmission(token);
    if (submission && submission->computeToken.IsValid())
    {
        m_impl->computeProfiler.ResolveFrame(m_impl->resources.GetCommandList(), submission->computeToken);
    }
}

namespace
{
    bool CollectQueueProfiler(DX12GpuProfiler& profiler,
        DX12GpuProfiler::FrameTimings& scratch, std::vector<DX12GpuProfiler::PassTiming>& merged,
        const GpuFrameToken& token,
        std::vector<EnhancedLivePassTiming>& outTimings,
        std::vector<EnhancedLiveGpuSlice>& outSlices,
        EnhancedLiveGpuSpan& outSpan,
        double& outTotalMilliseconds, std::string& outError)
    {
        // 수집물은 멤버로 둔다. 지역 변수면 조각마다 std::string 을 프레임마다
        // 새로 할당하게 된다 — Collect 가 제자리 대입을 쓰는 이유가 그것이다.
        outSpan = {};
        DX12GpuProfiler::FrameTimings& timings = scratch;
        if (!profiler.Collect(token, timings, outError)) return false;

        // ★ raw 조각은 timings 에 그대로 남아 있다. 이름으로 묶는 것은 **표시용**이고,
        //   GPU 타임라인은 묶지 않은 쪽을 그릴 것이다.
        std::vector<DX12GpuProfiler::PassTiming> nativeTimings;
        nativeTimings.swap(merged);
        profiler.MergeSlices(timings, nativeTimings);

        outTimings.clear();
        outTimings.reserve(nativeTimings.size());
        for (DX12GpuProfiler::PassTiming& timing : nativeTimings)
        {
            outTimings.push_back({ timing.name, timing.milliseconds, timing.spanMilliseconds });
        }
        outTotalMilliseconds = profiler.GetLastTotalMilliseconds();
        merged.swap(nativeTimings);

        const double toMs = (timings.ticksPerSecond > 0)
            ? (1000.0 / static_cast<double>(timings.ticksPerSecond)) : 0.0;
        outSpan.queueSpanMs = (timings.queueEndTicks > timings.queueBeginTicks)
            ? static_cast<double>(timings.queueEndTicks - timings.queueBeginTicks) * toMs : 0.0;
        outSpan.busyMs = static_cast<double>(timings.busyTicks) * toMs;
        outSpan.sliceCount = static_cast<uint32_t>(timings.slices.size());
        outSpan.queryOverflowPasses = timings.overflowedPasses;
        outSpan.droppedSlices = timings.droppedSlices;
        outSpan.droppedSliceName = timings.droppedSliceName;
        outSpan.droppedSliceDeltaTicks = timings.droppedSliceDeltaTicks;
        outSpan.zeroLengthSlices = timings.zeroLengthSlices;

        // 통합 축. 표본이 없으면 옮기지 않았다는 것을 그대로 전한다 — 0 을 시각처럼
        // 내보내면 정렬된 것과 구별되지 않는다.
        outSpan.cpuAligned = timings.cpuAligned;
        outSpan.queueBeginCpuTicks = timings.queueBeginCpuTicks;
        outSpan.queueEndCpuTicks = timings.queueEndCpuTicks;
        if (timings.cpuAligned)
        {
            LARGE_INTEGER collectTick{};
            QueryPerformanceCounter(&collectTick);
            const double cpuToMs = (profiler.Calibration().cpuTicksPerSecond > 0)
                ? (1000.0 / static_cast<double>(profiler.Calibration().cpuTicksPerSecond))
                : 0.0;

            // ★ 부호를 살려서 뺀다. uint64 끼리 빼면 음수가 천문학적 양수가 되어
            //   "여유가 아주 많다" 로 읽히고, 어긋남이 통째로 숨는다.
            const int64_t submitToBegin = static_cast<int64_t>(timings.queueBeginCpuTicks) -
                static_cast<int64_t>(timings.token.cpuSubmitTick);
            const int64_t endToCollect = static_cast<int64_t>(collectTick.QuadPart) -
                static_cast<int64_t>(timings.queueEndCpuTicks);
            outSpan.submitToGpuBeginMs = static_cast<double>(submitToBegin) * cpuToMs;
            outSpan.gpuEndToCollectMs = static_cast<double>(endToCollect) * cpuToMs;
            const int64_t submitToCollect = static_cast<int64_t>(collectTick.QuadPart) -
                static_cast<int64_t>(timings.token.cpuSubmitTick);
            outSpan.submitToCollectMs = static_cast<double>(submitToCollect) * cpuToMs;

            // raw 조각을 CPU 축으로 옮겨 내준다. 묶은 것이 아니라 이것이 타임라인의
            // 자료다 — 묶으면 분할 패스가 한 덩어리로 보인다.
            outSlices.resize(timings.slices.size());
            for (size_t i = 0; i < timings.slices.size(); ++i)
            {
                outSlices[i].queueId = 0;
                outSlices[i].passIndex = timings.slices[i].identity.passIndex;
                outSlices[i].name.assign(timings.slices[i].name);
                outSlices[i].beginCpuTick =
                    profiler.GpuTickToCpuTick(timings.slices[i].beginTicks);
                outSlices[i].endCpuTick =
                    profiler.GpuTickToCpuTick(timings.slices[i].endTicks);
            }
        }
        else
        {
            // 옮기지 못한 틱을 내보내지 않는다. 받는 쪽은 이것을 CPU 시각으로 읽고,
            // 그러면 GPU 레인이 엉뚱한 곳에 그려진다.
            outSlices.clear();
        }
        return true;
    }
}

bool EnhancedSceneRendererLiveDX12Adapter::CollectProfiler(const GpuFrameToken& token,
    std::vector<EnhancedLivePassTiming>& outTimings, std::vector<EnhancedLiveGpuSlice>& outSlices,
    EnhancedLiveGpuSpan& outSpan, double& outTotalMilliseconds, std::string& outError)
{
    auto& impl = *m_impl;
    const auto* submission = impl.FindMeasurementSubmission(token);
    if (!CollectQueueProfiler(impl.profiler, impl.profilerTimings, impl.profilerMerged,
        token, outTimings, outSlices, outSpan, outTotalMilliseconds, outError))
    {
        impl.DiscardMeasurements(token);
        return false;
    }
    if (submission && submission->computeToken.IsValid())
    {
        std::vector<EnhancedLivePassTiming> computePasses;
        std::vector<EnhancedLiveGpuSlice> computeSlices;
        EnhancedLiveGpuSpan computeSpan;
        double computeTotal = 0;
        if (!CollectQueueProfiler(impl.computeProfiler, impl.computeTimings, impl.computeMerged,
            submission->computeToken, computePasses, computeSlices, computeSpan, computeTotal, outError))
        {
            impl.DiscardMeasurements(token);
            return false;
        }
        outSpan.computeSliceCount = computeSpan.sliceCount;
        outSpan.queryOverflowPasses += computeSpan.queryOverflowPasses;
        outSpan.droppedSlices += computeSpan.droppedSlices;
        outSpan.zeroLengthSlices += computeSpan.zeroLengthSlices;
        if (outSpan.droppedSliceName.empty())
        {
            outSpan.droppedSliceName = computeSpan.droppedSliceName;
            outSpan.droppedSliceDeltaTicks = computeSpan.droppedSliceDeltaTicks;
        }
        outSpan.sliceCount += computeSpan.sliceCount;
        outTotalMilliseconds += computeTotal; // Sum of pass durations, not critical path.
        outTimings.insert(outTimings.end(), computePasses.begin(), computePasses.end());
        if (computeSpan.sliceCount > 0)
        {
            if (!outSpan.cpuAligned || !computeSpan.cpuAligned)
            {
                outSlices.clear();
                outSpan.cpuAligned = false;
                outSpan.queueSpanMs = outSpan.busyMs = 0;
                outError = "Cross-queue timing requires both calibrated clocks";
                impl.DiscardMeasurements(token);
                return false;
            }
            for (auto& slice : computeSlices)
            {
                slice.queueId = 1;
                outSlices.push_back(std::move(slice));
            }
            // Union in QPC domain: never subtract raw ticks from different queues.
            std::vector<std::pair<uint64_t, uint64_t>> intervals;
            for (const auto& slice : outSlices)
            {
                intervals.emplace_back(slice.beginCpuTick, slice.endCpuTick);
            }
            std::sort(intervals.begin(), intervals.end());
            auto begin = intervals.front().first;
            auto end = intervals.front().second;
            uint64_t busy = 0;
            outSpan.queueBeginCpuTicks = begin;
            for (const auto& interval : intervals)
            {
                if (interval.first > end)
                {
                    busy += end - begin;
                    begin = interval.first;
                }
                end = (std::max)(end, interval.second);
            }
            busy += end - begin;
            outSpan.queueEndCpuTicks = end;
            const double toMs = 1000.0 / impl.profiler.Calibration().cpuTicksPerSecond;
            outSpan.queueSpanMs = (end - outSpan.queueBeginCpuTicks) * toMs;
            outSpan.busyMs = busy * toMs;
            outSpan.submitToGpuBeginMs = (static_cast<int64_t>(outSpan.queueBeginCpuTicks) -
                static_cast<int64_t>(impl.profilerTimings.token.cpuSubmitTick)) * toMs;
            outSpan.gpuEndToCollectMs = computeSpan.queueEndCpuTicks > impl.profilerTimings.queueEndCpuTicks
                ? computeSpan.gpuEndToCollectMs : outSpan.gpuEndToCollectMs;
        }
    }
    const auto& origin = impl.profilerTimings.token;
    // Explicit capture collection and later display promotion can collect one
    // token twice. The query-ring slot bounds this exactly-once evidence cache.
    bool firstCollection = false;
    if (origin.ringSlot < impl.reportedProfilerTokens.size())
    {
        auto& reported = impl.reportedProfilerTokens[origin.ringSlot];
        firstCollection = !reported.IsValid() || reported.submissionId != origin.submissionId ||
            reported.renderViewId != origin.renderViewId || reported.engineFrameId != origin.engineFrameId;
        reported = origin;
    }
    // Union each queue before intersecting: split slices can overlap within one
    // queue, and a queue's first-to-last envelope includes idle gaps.
    const auto& graphicsClock = impl.profiler.Calibration();
    const auto& computeClock = impl.computeProfiler.Calibration();
    outSpan.cpuTicksPerSecond = graphicsClock.cpuTicksPerSecond;
    outSpan.overlapClockValid = outSpan.cpuAligned && graphicsClock.valid &&
        outSpan.queryOverflowPasses == 0 && outSpan.droppedSlices == 0 &&
        graphicsClock.cpuTicksPerSecond > 0 &&
        (outSpan.computeSliceCount == 0 || (computeClock.valid &&
            graphicsClock.cpuTicksPerSecond == computeClock.cpuTicksPerSecond));
    if (outSpan.overlapClockValid)
    {
        std::vector<std::pair<uint64_t, uint64_t>> lanes[2];
        for (const auto& slice : outSlices)
        {
            if (slice.queueId < 2 && slice.endCpuTick > slice.beginCpuTick)
            {
                lanes[slice.queueId].emplace_back(slice.beginCpuTick, slice.endCpuTick);
            }
        }
        for (auto& lane : lanes)
        {
            std::sort(lane.begin(), lane.end());
            size_t count = 0;
            for (const auto interval : lane)
            {
                if (count == 0 || interval.first > lane[count - 1].second)
                {
                    lane[count++] = interval;
                }
                else
                {
                    lane[count - 1].second = (std::max)(lane[count - 1].second, interval.second);
                }
            }
            lane.resize(count);
        }
        uint64_t overlapTicks = 0;
        size_t graphicsIndex = 0;
        size_t computeIndex = 0;
        while (graphicsIndex < lanes[0].size() && computeIndex < lanes[1].size())
        {
            const auto& graphics = lanes[0][graphicsIndex];
            const auto& compute = lanes[1][computeIndex];
            const auto begin = (std::max)(graphics.first, compute.first);
            const auto end = (std::min)(graphics.second, compute.second);
            if (end > begin)
            {
                overlapTicks += end - begin;
            }
            if (graphics.second <= compute.second)
            {
                ++graphicsIndex;
            }
            else
            {
                ++computeIndex;
            }
        }
        const double toMs = 1000.0 / graphicsClock.cpuTicksPerSecond;
        outSpan.measuredOverlapMilliseconds = overlapTicks * toMs;
        // Drift is a measured uncertainty indicator, not a guarantee of clock accuracy.
        outSpan.overlapClockErrorMilliseconds = outSpan.computeSliceCount == 0 ? 0.0 :
            (static_cast<double>(graphicsClock.maxAbsoluteDriftTicks) +
                static_cast<double>(computeClock.maxAbsoluteDriftTicks)) * toMs;
        if (firstCollection && outSpan.measuredOverlapMilliseconds > outSpan.overlapClockErrorMilliseconds)
        {
            ++impl.measuredOverlapSubmissions;
        }
    }
    if (impl.evidenceTelemetry && firstCollection)
    {
        std::ostringstream line;
        line.precision(17);
        line << std::boolalpha << "[rg8.timing] {\"schemaVersion\":2,\"mode\":"
             << (submission ? submission->mode : UINT32_MAX)
             << ",\"measurementDomain\":\""
             << (submission ? RGMeasurementDomainName(submission->domain) : "unknown") << '"'
             << ",\"backendGeneration\":" << GetBackendGeneration()
             << ",\"frameId\":" << origin.engineFrameId << ",\"viewId\":" << origin.renderViewId
             << ",\"submissionId\":" << origin.submissionId << ",\"captureGeneration\":" << origin.captureGeneration
             << ",\"cpuSubmitTick\":" << origin.cpuSubmitTick
             << ",\"cpuTicksPerSecond\":" << outSpan.cpuTicksPerSecond
             << ",\"clockValid\":" << outSpan.overlapClockValid
             << ",\"computeSliceCount\":" << outSpan.computeSliceCount
             << ",\"sliceCount\":" << outSpan.sliceCount
             << ",\"queryOverflow\":" << outSpan.queryOverflowPasses
             << ",\"droppedSlices\":" << outSpan.droppedSlices
             << ",\"queueSpanMilliseconds\":" << outSpan.queueSpanMs
             << ",\"busyMilliseconds\":" << outSpan.busyMs
             << ",\"measuredOverlapMilliseconds\":" << outSpan.measuredOverlapMilliseconds
             << ",\"overlapClockErrorMilliseconds\":" << outSpan.overlapClockErrorMilliseconds
             << ",\"graphicsCalibrationSamples\":" << graphicsClock.sampleCount
             << ",\"computeCalibrationSamples\":" << computeClock.sampleCount
             << ",\"slices\":[";
        const char* separator = "";
        for (const auto& slice : outSlices)
        {
            line << separator << "{\"queue\":" << static_cast<uint32_t>(slice.queueId)
                 << ",\"passIndex\":" << slice.passIndex << ",\"name\":";
            LiveDx12WriteJsonString(line, slice.name);
            line << ",\"beginCpuTick\":" << slice.beginCpuTick
                 << ",\"endCpuTick\":" << slice.endCpuTick << '}';
            separator = ",";
        }
        line << "]}";
        std::printf("%s\n", line.str().c_str());
    }
    // Queue-local raw samples retain declaration identity; display-name merges
    // never feed placement; failed/partial samples invalidate older measurements.
    if (outSpan.overlapClockValid)
    {
        impl.StoreMeasurements(origin);
    }
    else
    {
        impl.DiscardMeasurements(origin);
    }
    return true;
}

EnhancedLiveGpuClock EnhancedSceneRendererLiveDX12Adapter::ProfilerClock() const
{
    const DX12GpuProfiler::ClockCalibration& calibration = m_impl->profiler.Calibration();
    EnhancedLiveGpuClock clock{};
    clock.gpuTicksPerSecond = calibration.gpuTicksPerSecond;
    clock.cpuTicksPerSecond = calibration.cpuTicksPerSecond;
    clock.sampleCount = calibration.sampleCount;
    const double cpuToMs = (calibration.cpuTicksPerSecond > 0)
        ? (1000.0 / static_cast<double>(calibration.cpuTicksPerSecond)) : 0.0;
    clock.lastDriftMs = static_cast<double>(calibration.lastDriftTicks) * cpuToMs;
    clock.maxAbsoluteDriftMs =
        static_cast<double>(calibration.maxAbsoluteDriftTicks) * cpuToMs;
    clock.valid = calibration.valid;
    clock.lastError = m_impl->profiler.CalibrationError();
    return clock;
}

void EnhancedSceneRendererLiveDX12Adapter::MaintainAssetCaches(uint64_t frameIndex)
{
    Impl& impl = *m_impl;
    const uint64_t completedFence = impl.resources.GetCompletedFenceValue();
    const uint64_t lastFence = impl.resources.GetLastSignaledFenceValue();
    impl.textureCache.BeginFrame(frameIndex);
    impl.meshCache.BeginFrame(frameIndex);

    const RHIDeviceMemoryPressureInfo pressureInfo = impl.resources
        .GetPersistentMemoryBudgetCoordinator().GetMemoryPressureInfo();
    RHIAssetEvictionPass evictionPass = BeginRHIAssetEvictionPass(
        pressureInfo.memoryPressure, pressureInfo.targetReleaseBytes);
    impl.textureCache.RetireUnused(lastFence, &evictionPass);
    impl.meshCache.RetireUnused(lastFence, &evictionPass);
    impl.textureCache.SweepGraveyard(completedFence);
    impl.meshCache.SweepGraveyard(completedFence);
    impl.pipelines.CollectRetiredPipelines(RHICompletionPoint{ completedFence });
}

uint32_t EnhancedSceneRendererLiveDX12Adapter::DrainDebugMessages(
    std::string& outMessages)
{
    return m_impl->resources.DrainDebugMessages(outMessages);
}

uint32_t EnhancedSceneRendererLiveDX12Adapter::GetAssetGraveyardCount() const
{
    const auto texture = m_impl->textureCache.GetStats();
    const auto mesh = m_impl->meshCache.GetStats();
    return texture.graveyardCount + mesh.graveyardCount;
}

std::string EnhancedSceneRendererLiveDX12Adapter::FormatUploadStatus() const
{
    const auto stats = m_impl->resources.GetUploadStats();
    char line[192]{};
    std::snprintf(line, sizeof(line),
        "\n  업로드 링 — 세그먼트 %u개 %.1f MB(증설 %u회) · 최대 프레임 %.2f MB · 거절 %llu",
        stats.segmentCount,
        static_cast<double>(stats.segmentBytes) / (1024.0 * 1024.0),
        stats.growths,
        static_cast<double>(stats.peakFrameBytes) / (1024.0 * 1024.0),
        static_cast<unsigned long long>(stats.overflows));
    return line;
}

std::string EnhancedSceneRendererLiveDX12Adapter::FormatAssetStatus() const
{
    const auto texStats = m_impl->textureCache.GetStats();
    const auto meshStats = m_impl->meshCache.GetStats();
    constexpr double kBytesPerMB = 1024.0 * 1024.0;
    std::string status;

    char textureLine[160]{};
    std::snprintf(textureLine, sizeof(textureLine),
        "\n  텍스처 업로드 %u건 (CPU 직결 %u) · 히트 %u · 실패 %u",
        texStats.uploads, texStats.fromCpuPixels, texStats.hits, texStats.failures);
    status += textureLine;

    char residentLine[192]{};
    std::snprintf(residentLine, sizeof(residentLine),
        "\n  자산 상주 — 텍스처 %u개 %.1f MB · 메시 %u개 %.1f MB · 합계 %.1f MB",
        texStats.residentCount, texStats.residentBytes / kBytesPerMB,
        meshStats.residentCount, meshStats.residentBytes / kBytesPerMB,
        (texStats.residentBytes + meshStats.residentBytes) / kBytesPerMB);
    status += residentLine;

    const RHIPersistentHeapStats& bufferHeap = meshStats.persistentHeap;
    const RHIPersistentHeapStats& textureHeap = texStats.persistentHeap;
    const RHIPersistentHeapStats& budgetStats = 0 != textureHeap.budgetBytes
        ? textureHeap : bufferHeap;
    const RHIDeviceMemoryBudgetCoordinatorStats coordinatorStats =
        m_impl->resources.GetPersistentMemoryBudgetStats();
    char heapLine[768]{};
    std::snprintf(heapLine, sizeof(heapLine),
        "\n  Persistent heap — buffer %u segment %.1f/%.1f MB · texture %u segment %.1f/%.1f MB"
        " · dedicated live %u · trim %llu · fallback %llu"
        "\n  Device-local budget — %.1f/%.1f MB · DXGI LOCAL · pressure %s"
        "\n  Budget coordinator — owner %u · ticket grant/deny %llu/%llu"
        " · pending/committed %.1f/%.1f MB · snapshot %llu",
        bufferHeap.activeSegments, bufferHeap.allocatedBytes / kBytesPerMB,
        bufferHeap.segmentBytes / kBytesPerMB,
        textureHeap.activeSegments, textureHeap.allocatedBytes / kBytesPerMB,
        textureHeap.segmentBytes / kBytesPerMB,
        bufferHeap.liveDedicatedAllocations + textureHeap.liveDedicatedAllocations,
        static_cast<unsigned long long>(bufferHeap.trimmedSegments + textureHeap.trimmedSegments),
        static_cast<unsigned long long>(bufferHeap.dedicatedFallbacks + textureHeap.dedicatedFallbacks),
        budgetStats.budgetUsageBytes / kBytesPerMB,
        budgetStats.budgetBytes / kBytesPerMB,
        (bufferHeap.memoryPressure || textureHeap.memoryPressure) ? "ON" : "off",
        coordinatorStats.registeredOwners,
        static_cast<unsigned long long>(coordinatorStats.growthGrants),
        static_cast<unsigned long long>(coordinatorStats.growthDenials),
        coordinatorStats.reservedGrowthBytes / kBytesPerMB,
        coordinatorStats.committedSinceRefreshBytes / kBytesPerMB,
        static_cast<unsigned long long>(coordinatorStats.snapshotRefreshes));
    status += heapLine;

    char retireLine[224]{};
    std::snprintf(retireLine, sizeof(retireLine),
        "\n  자산 은퇴 — 누적 텍스처 %u개 %.1f MB · 메시 %u개 %.1f MB"
        " · 묘지 %u개 %.1f MB · 격리 %u개",
        texStats.retired, texStats.retiredBytes / kBytesPerMB,
        meshStats.retired, meshStats.retiredBytes / kBytesPerMB,
        texStats.graveyardCount + meshStats.graveyardCount,
        (texStats.graveyardBytes + meshStats.graveyardBytes) / kBytesPerMB,
        texStats.quarantinedCount + meshStats.quarantinedCount);
    status += retireLine;

    char evictionLine[256]{};
    std::snprintf(evictionLine, sizeof(evictionLine),
        "\n  Pressure eviction — pass %llu · 퇴출 %llu개 %.1f MB"
        " · recent 보호 %llu · upload-pending 보호 %llu",
        static_cast<unsigned long long>(texStats.eviction.pressurePasses +
            meshStats.eviction.pressurePasses),
        static_cast<unsigned long long>(texStats.eviction.pressureRetired +
            meshStats.eviction.pressureRetired),
        (texStats.eviction.pressureRetiredBytes +
            meshStats.eviction.pressureRetiredBytes) / kBytesPerMB,
        static_cast<unsigned long long>(texStats.eviction.pressureProtectedRecent +
            meshStats.eviction.pressureProtectedRecent),
        static_cast<unsigned long long>(texStats.eviction.pressureUploadPending +
            meshStats.eviction.pressureUploadPending));
    status += evictionLine;
    return status;
}

std::string EnhancedSceneRendererLiveDX12Adapter::FormatTargetHeapStatus() const
{
    const auto rtvStats = m_impl->resources.GetRtvViewHeap().GetStats();
    const auto dsvStats = m_impl->resources.GetDsvViewHeap().GetStats();
    char line[192]{};
    std::snprintf(line, sizeof(line),
        "\n  타깃 뷰 힙 — RTV 최대 %u/%u(넘침 %llu) · DSV 최대 %u/%u(넘침 %llu)",
        rtvStats.peakFrameDescriptors,
        m_impl->resources.GetRtvViewHeap().GetCapacity(),
        static_cast<unsigned long long>(rtvStats.overflows),
        dsvStats.peakFrameDescriptors,
        m_impl->resources.GetDsvViewHeap().GetCapacity(),
        static_cast<unsigned long long>(dsvStats.overflows));
    return line;
}

