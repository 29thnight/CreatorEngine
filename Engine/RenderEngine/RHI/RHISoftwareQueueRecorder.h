#pragma once
#include "../../Utility_Framework/LifetimeToken.h"
#include "../../Utility_Framework/Ownership.h"
#include "IRHICommandTranslator.h"
#include "RHIQueueRecorder.h"
#include "RHISoftwareRecorder.h"

struct RHISoftwareQueueOptions
{
    uint64_t maxCommandBytes{64ull << 20};
    uint64_t maxPayloadBytes{64ull << 20};
    // Sealed/running CPU arena admission only. Finish rejects without waiting;
    // this does not bound the transient peak of unfinished graph callbacks.
    uint64_t maxPendingArenaBytes{256ull << 20};
    RHICommandTranslationOptions translation;
};

struct RHISoftwareQueueState;

// The existing backend recorder opens/seals native targets on its submission
// owner. Software replay only borrows disjoint encoders until RunParallel joins.
// The recorder and its device services outlive this synchronous translator.
class RHIQueueCommandTranslator final : public IRHICommandTranslator
{
  public:
    RHIQueueCommandTranslator(IRHIQueueRecorder& recorder, own::shared_owner<IRHICommandQueue> queue)
        : m_recorder(recorder), m_queue(std::move(queue))
    {
    }
    bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,
                   RHITranslatedBatch& outBatch, RHICommandFailure& outFailure) override;

  private:
    IRHIQueueRecorder& m_recorder;
    own::shared_owner<IRHICommandQueue> m_queue;
};

// Wrapping does not create another GPU queue or timeline. Every public operation
// enters the backend's submission owner; a whole graph transaction cannot be
// overtaken by an external Wait/Signal/Submit on another calling thread.
class RHISoftwareCommandQueue final : public IRHICommandQueue
{
  public:
    RHIQueueIdentity GetIdentity() const override;
    bool Signal(uint64_t value, RHITimelinePoint& issuedPoint, std::string& error) override;
    bool Wait(const RHITimelinePoint& point, std::string& error) override;
    bool Submit(const ce::pooled_owner<IRHIQueueCommandBatch>& batch, uint64_t value, RHITimelinePoint& point,
                std::string& error) override;
    size_t CollectCompleted() override;
    size_t GetPendingBatchCount() const override;

  private:
    friend class RHISoftwareQueueRecorder;
    friend struct RHISoftwareQueueState;

  public:
    class ConstructionKey
    {
        friend class RHISoftwareQueueRecorder;
        ConstructionKey() = default;
    };
    RHISoftwareCommandQueue(ConstructionKey, own::shared_owner<RHISoftwareQueueState> state,
                            own::shared_owner<IRHICommandQueue> nativeQueue);

  private:
    bool PrepareBatch(const ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error);
    own::shared_owner<RHISoftwareQueueState> m_state;
    own::shared_owner<IRHICommandQueue> m_native;
};

class RHISoftwareQueueRecorder final : public IRHIQueueRecorder
{
  public:
    // These shared owners retain the recorder and validator through outstanding
    // CPU recordings. Their referenced device services keep the backend's usual
    // drain-before-destruction lifetime; native batches retain GPU owners alone.
    RHISoftwareQueueRecorder(own::shared_owner<IRHIQueueRecorder> nativeRecorder,
                             own::shared_owner<const IRHISoftwareCommandValidator> validator,
                             RHISoftwareQueueOptions options = {}, job_scheduler& scheduler = ce::get_job_scheduler());
    ~RHISoftwareQueueRecorder() override;
    IRenderDeviceServices& DeviceServices() const override;
    uint32_t GetWorkerCount() const override;
    void SetFrameDesc(const RHISoftwareBatchDesc& desc);
    bool WrapQueue(const own::shared_owner<IRHICommandQueue>& nativeQueue, own::shared_owner<IRHICommandQueue>& queue,
                   std::string& error);
    bool Record(const own::shared_owner<IRHICommandQueue>& queue, const std::function<void(RHIEncoder&)>& commands,
                ce::lifetime_token token, ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) override;
    bool BeginRecording(const own::shared_owner<IRHICommandQueue>& queue, uint32_t targetCount,
                        ce::lifetime_token token, ce::pooled_owner<IRHIQueueRecording>& recording,
                        std::string& error) override;
    bool ExecuteSubmission(const std::function<bool(std::string&)>& submit, std::string& error) override;
    bool PrepareSubmissionBatch(const own::shared_owner<IRHICommandQueue>& queue,
                                const ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) override;

  private:
    own::shared_owner<RHISoftwareQueueState> m_state;
};

// The factory and recorder are referenced only while creating a wrapper. Created
// queues retain their shared CPU backend owners independently of these facades.
class RHISoftwareQueueFactory final : public IRHICommandQueueFactory
{
  public:
    RHISoftwareQueueFactory(IRHICommandQueueFactory& nativeFactory, RHISoftwareQueueRecorder& recorder)
        : m_native(nativeFactory), m_recorder(recorder)
    {
    }
    RHIQueueCapabilities QueryQueueCapabilities() const override;
    bool CreateQueue(RHIQueueKind kind, own::shared_owner<IRHICommandQueue>& queue, std::string& error) override;

  private:
    IRHICommandQueueFactory& m_native;
    RHISoftwareQueueRecorder& m_recorder;
};
