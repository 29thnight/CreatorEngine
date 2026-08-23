#pragma once
#include "RHIResourceTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class IRHIDeviceResources;
class RHIRecordedBatch;

/// RHI owner의 세대를 바꾸는 명시적 경계. 정상 프레임은 이 명령을 만들지 않는다.
/// GPU drain은 아래 네 경우에만 허용한다.
enum class RHILifecycleCommand : uint8_t
{
    None,
    SceneReplacement,
    PipelineRebuild,
    DisplaySlotReplacement,
    SwapChainResize,
    BackendShutdown,
    UnrecoverableDeviceError,
    OfflineReadbackCapture
};

const char* ToString(RHILifecycleCommand command);
bool RequiresGpuDrain(RHILifecycleCommand command);

struct RHISubmissionOwnerStats
{
    uint64_t generation{ 0 };
    uint64_t lifecycleCommands{ 0 };
    uint64_t drainCommands{ 0 };
    uint32_t pendingTasks{ 0 };
    uint32_t pendingBatches{ 0 };
    uint32_t pendingRetirements{ 0 };
    RHILifecycleCommand lastCommand{ RHILifecycleCommand::None };
    bool registered{ false };
    bool accepting{ false };
    bool transitioning{ false };
    bool faulted{ false };

    bool IsIdle() const
    {
        return 0 == pendingTasks && 0 == pendingBatches &&
            0 == pendingRetirements;
    }
};

/// lifecycle command 하나의 독립 판정값. task/batch/retirement를 합산하지 않는
/// 이유는 한 종류의 누락을 다른 두 종류의 0이 가릴 수 없게 하기 위해서다.
struct RHILifecycleResult
{
    RHILifecycleCommand command{ RHILifecycleCommand::None };
    uint64_t previousGeneration{ 0 };
    uint64_t generation{ 0 };
    uint32_t pendingTasks{ 0 };
    uint32_t pendingBatches{ 0 };
    uint32_t pendingRetirements{ 0 };
    bool drained{ false };

    bool IsClean() const
    {
        return drained && 0 == pendingTasks && 0 == pendingBatches &&
            0 == pendingRetirements;
    }
};

struct RHISubmissionThreadStats
{
    uint64_t enqueued{ 0 };
    uint64_t executed{ 0 };
    uint64_t failed{ 0 };
    uint64_t retired{ 0 };
    uint64_t saturationWaits{ 0 };
    uint64_t producerWaitNanoseconds{ 0 };
    uint64_t maxProducerWaitNanoseconds{ 0 };
    uint64_t drainCount{ 0 };
    uint64_t lifecycleCommands{ 0 };
    uint64_t lifecycleFailures{ 0 };
    uint64_t orderingErrors{ 0 };
    uint64_t lastCompletedSequence{ 0 };
    uint64_t threadIdHash{ 0 };
    uint32_t maxQueueDepth{ 0 };
    uint32_t pendingTasks{ 0 };
    uint32_t pendingBatches{ 0 };
    uint32_t pendingRetirements{ 0 };
    bool running{ false };
    bool accepting{ false };
};

class RHISubmissionTicket
{
public:
    RHISubmissionTicket() = default;

    bool IsValid() const { return nullptr != m_state; }
    uint64_t GetSequence() const;
    bool IsComplete() const;
    RHIRecordedBatch* GetRecordedBatch();
    const RHIRecordedBatch* GetRecordedBatch() const;

private:
    friend class RHISubmissionThread;
    struct State;
    std::shared_ptr<State> m_state;
};

/// 프로세스의 native queue 호출을 하나의 bounded FIFO와 전용 스레드로 모은다.
///
/// 각 DeviceResources가 client 하나를 잡고 owner=this로 작업을 넣는다. owner drain은
/// 그 디바이스의 CPU 제출과 GPU completion 기반 lifetime retirement가 모두 끝날 때만
/// 돌아오므로, 다른 디바이스가 같은 RHI thread를 공유해도 해체 순서가 섞이지 않는다.
class RHISubmissionThread final
{
public:
    static constexpr uint32_t kQueueCapacity = 3;

    using Work = std::function<bool(std::string&)>;
    using CompletionQuery = std::function<uint64_t()>;

    RHISubmissionThread();
    ~RHISubmissionThread();
    RHISubmissionThread(const RHISubmissionThread&) = delete;
    RHISubmissionThread& operator=(const RHISubmissionThread&) = delete;

    bool AcquireClient(const void* owner, std::string& outError);
    void ReleaseClient(const void* owner);

    bool Enqueue(const void* owner, const char* label, Work work,
        RHISubmissionTicket& outTicket, std::string& outError,
        RHICompletionPoint retirementPoint = {},
        CompletionQuery completionQuery = {},
        std::shared_ptr<const void> lifetimeToken = {});

    bool ExecuteAndWait(const void* owner, const char* label, Work work,
        std::string& outError);
    bool Wait(const RHISubmissionTicket& ticket, std::string& outError) const;

    /// CPU queue 호출까지만 비운다. GPU completion retirement는 남을 수 있다.
    bool DrainSubmissions(const void* owner, std::string& outError);
    /// CPU 제출과 GPU completion retirement를 모두 비운다.
    bool Drain(const void* owner, std::string& outError);
    bool ConsumeFailure(const void* owner, std::string& outError);

    /// 앞선 owner 제출을 FIFO로 지난 뒤 backend의 GPU-idle 작업을 RHI thread에서
    /// 실행한다. 성공하면 generation을 올리고 pending 세 값을 각각 0으로 단정한다.
    bool ExecuteLifecycleDrain(const void* owner,
        RHILifecycleCommand command, Work gpuDrain,
        RHILifecycleResult& outResult, std::string& outError);

    /// device lost 뒤에는 GPU completion을 더 기다릴 수 없다. 아직 시작하지 않은
    /// owner 작업과 retirement를 명시적으로 폐기하고 owner를 닫는다.
    bool AbandonForDeviceError(const void* owner,
        RHILifecycleResult& outResult, std::string& outError);

    /// native queue 작업이 복구 불가능한 device error를 관측했을 때 표시한다.
    /// RHI thread 안에서도 호출할 수 있다.
    void MarkUnrecoverableDeviceError(const void* owner,
        const std::string& error);

    bool IsCurrentThread() const;
    RHISubmissionThreadStats GetStats() const;
    RHISubmissionOwnerStats GetOwnerStats(const void* owner) const;
    uint64_t GetOwnerGeneration(const void* owner) const;

    /// batch completion을 producer에서 예약한 뒤 값 자체를 FIFO로 이동한다.
    bool EnqueueRecordedBatch(const void* owner,
        IRHIDeviceResources& completionSource, RHIRecordedBatch&& batch,
        RHISubmissionTicket& outTicket, std::string& outError);

private:
    enum class EntryKind : uint8_t;
    bool EnqueueInternal(const void* owner, const char* label, Work work,
        RHISubmissionTicket& outTicket, std::string& outError,
        RHICompletionPoint retirementPoint,
        CompletionQuery completionQuery,
        std::shared_ptr<const void> lifetimeToken,
        EntryKind kind, bool allowTransition,
        uint64_t expectedGeneration = 0);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

RHISubmissionThread& GetRHISubmissionThread();

