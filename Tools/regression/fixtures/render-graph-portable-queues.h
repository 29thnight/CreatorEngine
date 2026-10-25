#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "render-graph-portable-services.h"
#include "Engine/RenderEngine/RHI/RHIQueueRecorder.h"

namespace ce::regression
{
    class PortableFence final : public IRHITimelineFence
    {
      public:
        RHIQueueIdentity identity;
        uint64_t issued{}, completed{};
        RHIQueueIdentity GetProducer() const override { return identity; }
        uint64_t GetLastIssuedValue() const override { return issued; }
        RHITimelineSnapshot QueryCompletion() const override { return {RHITimelineStatus::Available, completed}; }
    };
    class PortableQueue final : public IRHICommandQueue
    {
      public:
        explicit PortableQueue(RHIQueueIdentity identity) : fence(own::make_shared<PortableFence>())
        {
            fence->identity = identity;
        }
        own::shared_owner<PortableFence> fence;
        std::vector<std::pair<ce::pooled_owner<IRHIQueueCommandBatch>, uint64_t>> pending;
        std::vector<RHITimelinePoint> waits;
        size_t rejectAfter{SIZE_MAX}, submissions{};
        RHIQueueIdentity GetIdentity() const override { return fence->identity; }
        bool Signal(uint64_t value, RHITimelinePoint& point, std::string& error) override
        {
            point = {};
            if (value <= fence->issued || value == UINT64_MAX)
            {
                error = "Invalid fixture signal";
                return false;
            }
            fence->issued = value;
            point = {fence, value};
            return true;
        }
        bool Wait(const RHITimelinePoint& point, std::string& error) override
        {
            if (!ValidateQueueWait(GetIdentity(), point, error))
            {
                return false;
            }
            waits.push_back(point);
            return true;
        }
        bool Submit(const ce::pooled_owner<IRHIQueueCommandBatch>& batch, uint64_t value, RHITimelinePoint& point,
                    std::string& error) override
        {
            if (submissions == rejectAfter || !batch || batch->GetQueueIdentity() != GetIdentity())
            {
                error = "Injected fixture queue rejection";
                return false;
            }
            if (!Signal(value, point, error))
            {
                return false;
            }
            ++submissions;
            pending.emplace_back(batch, value);
            return true;
        }
        size_t CollectCompleted() override
        {
            const auto size = pending.size();
            std::erase_if(pending, [&](const auto& entry) { return entry.second <= fence->completed; });
            return size - pending.size();
        }
        size_t GetPendingBatchCount() const override { return pending.size(); }
        void Complete()
        {
            fence->completed = fence->issued;
            CollectCompleted();
        }
    };
    class PortableQueueRecorder final : public IRHIQueueRecorder
    {
      public:
        struct Recorded
        {
            RHIQueueIdentity queue;
            PortableRenderEncoder encoder;
        };
        std::vector<own::unique_owner<Recorded>> recorded;
        size_t preparations{}, rejectPreparationAt{SIZE_MAX};
        explicit PortableQueueRecorder(PortableRenderServices& services) : services(services) {}
        IRenderDeviceServices& DeviceServices() const override { return services; }
        bool Record(const own::shared_owner<IRHICommandQueue>& queue, const std::function<void(RHIEncoder&)>& commands,
                    ce::lifetime_token owner, ce::pooled_owner<IRHIQueueCommandBatch>& batch,
                    std::string& error) override
        {
            error.clear();
            auto record = own::make_unique<Recorded>();
            record->queue = queue->GetIdentity();
            commands(record->encoder);
            recorded.push_back(std::move(record));
            auto result = own::make_shared<Batch>();
            result->identity = queue->GetIdentity();
            result->recording = recorded.size();
            result->owner = std::move(owner);
            batch = std::move(result);
            return true;
        }
        bool PrepareSubmissionBatch(const own::shared_owner<IRHICommandQueue>&,
                                    const ce::pooled_owner<IRHIQueueCommandBatch>&, std::string& error) override
        {
            if (preparations++ == rejectPreparationAt)
            {
                error = "Injected preparation rejection";
                return false;
            }
            return true;
        }

      private:
        struct Batch final : IRHIQueueCommandBatch
        {
            RHIQueueIdentity identity;
            uint64_t recording{};
            ce::lifetime_token owner;
            RHIQueueIdentity GetQueueIdentity() const override { return identity; }
            uint64_t GetRecordingId() const override { return recording; }
        };
        PortableRenderServices& services;
    };
} // namespace ce::regression
