#pragma once
#include "RHI/RHIQueueContract.h"
#include <stdexcept>

namespace
{
    class QueueContractTestFence final : public IRHITimelineFence
    {
    public:
        RHIQueueIdentity producer{71, 1, RHIQueueKind::Graphics};
        uint64_t issued{7};
        RHITimelineSnapshot completion{RHITimelineStatus::Available, 0};
        RHIQueueIdentity GetProducer() const override { return producer; }
        uint64_t GetLastIssuedValue() const override { return issued; }
        RHITimelineSnapshot QueryCompletion() const override { return completion; }
    };
}

bool DX12Test::RunQueueContractTest(std::string& outLog)
{
    uint32_t checks = 0;
    const auto require = [&](bool accepted, const char* message)
    {
        ++checks;
        if (!accepted)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        const RHIQueueIdentity graphics{71, 1, RHIQueueKind::Graphics};
        const RHIQueueIdentity compute{71, 2, RHIQueueKind::Compute};
        auto fence = std::make_shared<QueueContractTestFence>();
        RHITimelinePoint point{fence, 7};
        std::string error;
        require(point.IsValid() && !point.IsComplete(), "Issued pending fence was treated as complete.");
        require(ValidateQueueWait(compute, point, error), "Same-device compute wait was rejected.");
        fence->completion.completedValue = 7;
        require(point.IsComplete(), "Completed issued fence was not recognized.");
        fence->completion = {RHITimelineStatus::DeviceLost, UINT64_MAX};
        require(!point.IsComplete() && !ValidateQueueWait(compute, point, error), "Device loss was treated as completion.");
        fence->completion = {RHITimelineStatus::Unavailable, 7};
        require(!point.IsComplete() && !ValidateQueueWait(compute, point, error), "Unavailable timeline admitted a wait.");
        fence->completion = {RHITimelineStatus::Available, UINT64_MAX};
        require(!point.IsComplete() && !ValidateQueueWait(compute, point, error), "Native loss sentinel was treated as completion.");
        fence->completion = {RHITimelineStatus::Available, 7};
        require(!ValidateQueueWait({72, 2, RHIQueueKind::Compute}, point, error), "Retired/foreign generation admitted a wait.");
        require(!ValidateQueueWait({71, 0, RHIQueueKind::Compute}, point, error), "Unidentified queue admitted a wait.");
        require(!ValidateQueueWait({71, 2, static_cast<RHIQueueKind>(255)}, point, error), "Unknown queue kind admitted a wait.");
        require(!ValidateQueueWait(compute, {}, error), "Null timeline admitted a wait.");
        require(!ValidateQueueWait(compute, {fence, 0}, error), "Zero timeline point admitted a wait.");
        require(!ValidateQueueWait(compute, {fence, 8}, error), "Unissued timeline point admitted a wait.");
        require(!ValidateQueueWait(compute, {fence, UINT64_MAX}, error), "Reserved loss value admitted a wait.");

        RHIQueueHandoff handoff{graphics, compute, point};
        require(ValidateQueueHandoff(handoff, error), "Valid common-boundary handoff was rejected.");
        handoff.producer.queueId = 3;
        require(!ValidateQueueHandoff(handoff, error), "Foreign producer timeline admitted a handoff.");
        handoff.producer = graphics;
        handoff.consumer = graphics;
        require(!ValidateQueueHandoff(handoff, error), "Same-queue operation was treated as a handoff.");
        handoff.consumer = compute;
        handoff.consumer.queueId = graphics.queueId;
        require(!ValidateQueueHandoff(handoff, error), "Reusing a queue ID with another kind admitted a handoff.");
        handoff.consumer = compute;
        handoff.releaseToCommon = false;
        require(!ValidateQueueHandoff(handoff, error), "Missing release boundary admitted a handoff.");
        handoff.releaseToCommon = true;
        handoff.acquireFromCommon = false;
        require(!ValidateQueueHandoff(handoff, error), "Missing acquire boundary admitted a handoff.");
        handoff.acquireFromCommon = true;
        RHIQueueCapabilities unavailable;
        require(!unavailable.Supports(RHIQueueKind::Compute) && !unavailable.crossQueueTimeline,
            "Unimplemented queue service advertised async availability.");

        std::weak_ptr<IRHITimelineFence> weak = fence;
        fence.reset();
        require(!weak.expired(), "Queued point failed to retain its timeline.");
        point.fence.reset();
        require(!weak.expired(), "Handoff failed to retain its producer timeline.");
        handoff.readyAfter.fence.reset();
        require(weak.expired(), "Timeline survived its last point owner.");
        outLog = "Q0_QUEUE_CONTRACT_OK checks=" + std::to_string(checks) +
            " scope=cpu-contract nativeQueues=not-implemented\n";
        return true;
    }
    catch (const std::exception& error)
    {
        outLog = "Q0_QUEUE_CONTRACT_FAILED checks=" + std::to_string(checks) + " " + error.what() + "\n";
        return false;
    }
}
