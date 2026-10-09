#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>

enum class RHIQueueKind : uint8_t
{
    Graphics,
    Compute,
    Copy
};

// Native handles and backend queue-family indices stay behind the implementation.
struct RHIQueueIdentity
{
    // Process-unique device-incarnation token, not a per-device counter reused by
    // another factory. queueId is unique within that incarnation, across kinds.
    uint64_t deviceGeneration{0};
    uint32_t queueId{0};
    RHIQueueKind kind{RHIQueueKind::Graphics};

    bool IsValid() const
    {
        return deviceGeneration != 0 && queueId != 0 &&
            (kind == RHIQueueKind::Graphics || kind == RHIQueueKind::Compute || kind == RHIQueueKind::Copy);
    }
    bool operator==(const RHIQueueIdentity&) const = default;
};

struct RHIQueueCapabilities
{
    // Service availability, not a promise of hardware overlap or speedup.
    bool graphics{false};
    bool compute{false};
    bool copy{false};
    bool crossQueueTimeline{false};

    bool Supports(RHIQueueKind kind) const
    {
        switch (kind)
        {
        case RHIQueueKind::Graphics: return graphics;
        case RHIQueueKind::Compute: return compute;
        case RHIQueueKind::Copy: return copy;
        default: return false;
        }
    }
};

enum class RHITimelineStatus : uint8_t
{
    Available,
    Unavailable,
    DeviceLost
};

struct RHITimelineSnapshot
{
    RHITimelineStatus status{RHITimelineStatus::Unavailable};
    uint64_t completedValue{0};
};

class IRHITimelineFence
{
public:
    virtual ~IRHITimelineFence() = default;
    virtual RHIQueueIdentity GetProducer() const = 0;
    // Publish only after the backend accepts the native signal operation.
    virtual uint64_t GetLastIssuedValue() const = 0;
    virtual RHITimelineSnapshot QueryCompletion() const = 0;
};

struct RHITimelinePoint
{
    // Keeps the native synchronization object alive through queued waits.
    std::shared_ptr<IRHITimelineFence> fence;
    uint64_t value{0};

    bool IsValid() const
    {
        return fence && fence->GetProducer().IsValid() && value != 0 &&
            value != std::numeric_limits<uint64_t>::max() && value <= fence->GetLastIssuedValue();
    }
    bool IsComplete() const
    {
        if (!IsValid())
        {
            return false;
        }
        const auto snapshot = fence->QueryCompletion();
        return snapshot.status == RHITimelineStatus::Available &&
            snapshot.completedValue != std::numeric_limits<uint64_t>::max() &&
            snapshot.completedValue >= value;
    }
};

inline bool ValidateQueueWait(const RHIQueueIdentity& consumer,
    const RHITimelinePoint& point, std::string& error)
{
    error.clear();
    if (!consumer.IsValid() || !point.IsValid())
    {
        error = "Queue wait requires an identified queue and an issued timeline point.";
        return false;
    }
    if (consumer.deviceGeneration != point.fence->GetProducer().deviceGeneration)
    {
        error = "Queue wait crosses a device or retired device generation.";
        return false;
    }
    const auto snapshot = point.fence->QueryCompletion();
    if (snapshot.status != RHITimelineStatus::Available ||
        snapshot.completedValue == std::numeric_limits<uint64_t>::max())
    {
        error = "Queue wait timeline is unavailable or device-lost.";
        return false;
    }
    return true;
}

struct RHIQueueHandoff
{
    RHIQueueIdentity producer;
    RHIQueueIdentity consumer;
    RHITimelinePoint readyAfter;
    // A conservative resource handoff boundary; backend implementations translate
    // this to native state/layout/family ownership. This is not an alias handoff.
    bool releaseToCommon{true};
    bool acquireFromCommon{true};
};

inline bool ValidateQueueHandoff(const RHIQueueHandoff& handoff, std::string& error)
{
    if (!ValidateQueueWait(handoff.consumer, handoff.readyAfter, error))
    {
        return false;
    }
    if (!handoff.producer.IsValid() || handoff.producer.queueId == handoff.consumer.queueId ||
        handoff.readyAfter.fence->GetProducer() != handoff.producer)
    {
        error = "Queue handoff requires distinct queues and the producer's own timeline.";
        return false;
    }
    if (!handoff.releaseToCommon || !handoff.acquireFromCommon)
    {
        error = "Queue handoff requires release/acquire through the common boundary.";
        return false;
    }
    return true;
}

class IRHIQueueCommandBatch
{
public:
    virtual ~IRHIQueueCommandBatch() = default;
    virtual RHIQueueIdentity GetQueueIdentity() const = 0;
    virtual uint64_t GetRecordingId() const = 0;
};

class IRHICommandQueue
{
public:
    virtual ~IRHICommandQueue() = default;
    virtual RHIQueueIdentity GetIdentity() const = 0;
    // Values must increase; zero and UINT64_MAX are reserved. Failure leaves the
    // output point invalid and never publishes an accepted signal value.
    virtual bool Signal(uint64_t value, RHITimelinePoint& issuedPoint, std::string& error) = 0;
    // Enqueue a GPU wait; do not turn this into a CPU drain.
    virtual bool Wait(const RHITimelinePoint& point, std::string& error) = 0;
    // CPU admission only. Backend retains the sealed one-shot recording and its
    // resources until this queue's GPU completion, including unfenced failures.
    virtual bool Submit(const std::shared_ptr<IRHIQueueCommandBatch>& batch,
        uint64_t value, RHITimelinePoint& point, std::string& error) = 0;
    virtual size_t CollectCompleted() = 0;
    virtual size_t GetPendingBatchCount() const = 0;
};

class IRHICommandQueueFactory
{
public:
    virtual ~IRHICommandQueueFactory() = default;
    virtual RHIQueueCapabilities QueryQueueCapabilities() const = 0;
    virtual bool CreateQueue(RHIQueueKind kind, std::shared_ptr<IRHICommandQueue>& queue,
        std::string& error) = 0;
};
