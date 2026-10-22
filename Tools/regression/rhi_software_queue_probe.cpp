#include "../../Engine/RenderEngine/RHI/RHISoftwareQueueRecorder.h"
#include "../../Engine/RenderEngine/RHI/IRenderDeviceServices.h"
#include <iostream>

namespace
{
    std::atomic<uint64_t> checks{};
    std::atomic<uint64_t> renderAllocations{};
    std::atomic<uint64_t> renderBytes{};
    std::atomic<bool> rejectNextAllocation{};
    const char* activeTest{};
    thread_local uint32_t submissionDepth{};

    void require(bool value, const char* message)
    {
        checks.fetch_add(1, std::memory_order_relaxed);
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    class ProbeDeviceServices final : public IRenderDeviceServices
    {
      public:
        uint64_t m_uploadRecordingId{41};
        uint64_t m_descriptorVersion{17};
        uint64_t GetCurrentUploadRecordingId() const override { return m_uploadRecordingId; }
        uint64_t GetDescriptorVersionToken() const override { return m_descriptorVersion; }
        bool ReserveUploadBatch(std::span<const RHIUploadRequest>, std::span<RHIBufferSlice>, std::string&) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHIBufferSlice AllocateUpload(const RHIUploadRequest&) override
        {
            throw std::logic_error("unexpected device service");
        }
        void RegisterUploadTransactionListener(IRHIUploadTransactionListener*) override
        {
            throw std::logic_error("unexpected device service");
        }
        void UnregisterUploadTransactionListener(IRHIUploadTransactionListener*) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHIBufferSlice UploadConstants(const void*, size_t) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc>) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHIEncoder& GetImmediateEncoder() override { throw std::logic_error("unexpected device service"); }
        RHIBindingTable CreateBindings(std::span<const RHIBindingDesc>) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHIRenderTargetBinding CreateRenderTargets(std::span<const RHITextureHandle>,
                                                   const RHIDepthTargetDesc*) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHIRenderTargetBinding CreateRenderTargets(std::span<const RHIColorTargetDesc>,
                                                   const RHIDepthTargetDesc*) override
        {
            throw std::logic_error("unexpected device service");
        }
        RHITextureInfo DescribeTexture(RHITextureHandle) const override
        {
            throw std::logic_error("unexpected device service");
        }
        void ReleaseTexture(RHITextureHandle) override { throw std::logic_error("unexpected device service"); }
        void ReleaseBuffer(RHIBufferHandle) override { throw std::logic_error("unexpected device service"); }
        void TransitionResources(std::span<const RHITransition>) override
        {
            throw std::logic_error("unexpected device service");
        }
        void TransitionBuffers(std::span<const RHIBufferTransition>) override
        {
            throw std::logic_error("unexpected device service");
        }
        bool CreateBuffer(const RHIBufferDesc&, RHIBufferHandle&, std::string&) override
        {
            throw std::logic_error("unexpected device service");
        }
        bool CreateTexture(const RHITextureDesc&, RHITextureHandle&, std::string&) override
        {
            throw std::logic_error("unexpected device service");
        }
        bool CreateReadback(uint32_t, uint32_t, RHIFormat, uint32_t, RHIReadback&, std::string&) override
        {
            throw std::logic_error("unexpected device service");
        }
        bool MapReadback(const RHIReadback&, RHIReadbackImage&, std::string&) override
        {
            throw std::logic_error("unexpected device service");
        }
        void ReleaseReadback(RHIReadback&) override { throw std::logic_error("unexpected device service"); }
        bool CreateBufferReadback(uint64_t, RHIReadback&, std::string&) override
        {
            throw std::logic_error("unexpected device service");
        }
    };

    class ProbeValidator final : public IRHISoftwareCommandValidator
    {
      public:
        bool Validate(RHISoftwareCommandType, const void*, size_t, const RHISoftwareEncoderState&) const override
        {
            return true;
        }
    };

    struct ProbeGate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered{};
        bool released{};
        void Enter()
        {
            std::unique_lock lock(mutex);
            entered = true;
            changed.notify_all();
            require(changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; }),
                    "fixture gate was not released");
        }
        void Wait()
        {
            std::unique_lock lock(mutex);
            require(changed.wait_for(lock, std::chrono::seconds(10), [&] { return entered; }),
                    "fixture gate was not entered");
        }
        void Release()
        {
            std::lock_guard lock(mutex);
            released = true;
            changed.notify_all();
        }
    };

    struct ProbeReplayState
    {
        std::atomic<uint32_t> active{};
        std::atomic<uint32_t> completed{};
        std::atomic<uint32_t> failures{};
        std::atomic<uint32_t> calls{};
        uint32_t failValue{};
        uint32_t secondFailValue{};
        bool unknownFailure{};
        uint32_t gatedValue{};
        ProbeGate* gate{};
    };

    class ProbeEncoder final : public RHIEncoder
    {
      public:
        std::array<uint32_t, 256> m_values{};
        size_t m_count{};
        uint32_t m_segments{};
        uint64_t m_dispatches{};
        ProbeReplayState* m_replay{};
        std::atomic_flag m_busy{};
        void SetViewportAndScissor(uint32_t, uint32_t) override {}
        void SetPipeline(RHIBindPoint, RHIPipelineHandle) override {}
        void SetPrimitiveTopology(RHIPrimitiveTopology) override {}
        void SetBindings(RHIBindPoint, uint32_t, const RHIBindingTable&) override {}
        void SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&) override {}
        void SetConstantBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice& value) override
        {
            require(!value.cpuAddress, "borrowed upload CPU pointer reached native replay");
        }
        void SetRootBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override {}
        void SetVertexBuffer(const RHIBufferSlice&, uint32_t) override {}
        void SetIndexBuffer(const RHIBufferSlice&, RHIFormat) override {}
        void Draw(uint32_t value, uint32_t, uint32_t, uint32_t) override { Append(value); }
        void DrawIndexed(uint32_t value, uint32_t, uint32_t, int32_t, uint32_t) override { Append(value); }
        void Dispatch(uint32_t value, uint32_t, uint32_t) override
        {
            Append(value);
            ++m_dispatches;
        }
        uint64_t GetComputeDispatchCount() const override { return m_dispatches; }
        void BindRenderTargets(const RHIRenderTargetBinding&) override {}
        void ClearRenderTargets(const RHIRenderTargetBinding&, const float*) override {}
        void ClearDepthTarget(const RHIRenderTargetBinding&, float) override {}
        void ResourceBarriers(const RHIBarrierBatch&) override {}
        void UavBarrier(std::span<const RHITextureHandle>) override {}
        void UavBarrierBuffers(std::span<const RHIBufferHandle>) override {}
        void CopyResource(RHITextureHandle, RHITextureHandle) override {}
        void ClearUnorderedAccess(const RHIBindingDesc&, const float*) override {}
        void CopyToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t) override {}
        void CopyVolumeToReadback(const RHIReadback&, RHITextureHandle, uint32_t) override {}
        void CopyPartialToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t) override {}
        void CopyBufferToReadback(const RHIReadback&, RHIBufferHandle, uint64_t, uint64_t) override {}
        void CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t, uint32_t) override {}
        void ClearRenderTargetRect(const RHIRenderTargetBinding&, const float*, const RHIRect&) override {}
        void Append(uint32_t value)
        {
            require(!m_busy.test_and_set(), "two replay jobs owned one target");
            m_replay->active.fetch_add(1);
            struct Release
            {
                ProbeEncoder& encoder;
                ~Release()
                {
                    encoder.m_replay->active.fetch_sub(1);
                    encoder.m_busy.clear();
                }
            } release{*this};
            m_replay->calls.fetch_add(1);
            if (m_replay->gate && value == m_replay->gatedValue)
            {
                m_replay->gate->Enter();
            }
            if (value == m_replay->failValue || value == m_replay->secondFailValue)
            {
                m_replay->failures.fetch_add(1);
                if (m_replay->unknownFailure)
                {
                    throw 17;
                }
                throw std::runtime_error("injected native replay failure");
            }
            require(m_count < m_values.size(), "fixture stream capacity exceeded");
            m_values[m_count++] = value;
            m_replay->completed.fetch_add(1);
        }
    };

    class ProbeRecording final : public IRHIQueueRecording,
                                 public IRHIQueueCommandBatch,
                                 public own::enable_owner_from_this<ProbeRecording>
    {
      public:
        std::array<ProbeEncoder, 8> m_encoders;
        RHIQueueIdentity m_identity;
        ce::lifetime_token m_owner;
        ProbeReplayState* m_replay{};
        uint32_t m_targetCount{};
        uint64_t m_id{};
        bool m_failFinish{};
        bool m_sealed{};
        bool m_submitted{};
        ~ProbeRecording() override
        {
            if (m_replay && m_replay->active.load())
            {
                std::terminate();
            }
        }
        RHIEncoder& AcquireEncoder(uint32_t target) override
        {
            require(!m_sealed && target < m_targetCount, "invalid native target lease");
            auto& encoder = m_encoders[target];
            encoder.m_replay = m_replay;
            ++encoder.m_segments;
            return encoder;
        }
        bool Finish(ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) override
        {
            require(m_replay->active.load() == 0, "native finish before translation workers joined");
            if (m_failFinish)
            {
                error = "injected native finish failure";
                return false;
            }
            m_sealed = true;
            batch = shared_from_this();
            return true;
        }
        RHIQueueIdentity GetQueueIdentity() const override { return m_identity; }
        uint64_t GetRecordingId() const override { return m_id; }
    };

    class ProbeFence final : public IRHITimelineFence
    {
      public:
        RHIQueueIdentity m_identity;
        std::atomic<uint64_t> m_issued{};
        std::atomic<uint64_t> m_completed{};
        RHIQueueIdentity GetProducer() const override { return m_identity; }
        uint64_t GetLastIssuedValue() const override { return m_issued.load(); }
        RHITimelineSnapshot QueryCompletion() const override
        {
            return {RHITimelineStatus::Available, m_completed.load()};
        }
    };

    struct ProbeEvents
    {
        std::mutex mutex;
        std::array<uint32_t, 512> values{};
        size_t size{};
        void Add(uint32_t value)
        {
            std::lock_guard lock(mutex);
            require(size < values.size(), "fixture event capacity exceeded");
            values[size++] = value;
        }
    };

    class ProbeQueue final : public IRHICommandQueue
    {
      public:
        struct Pending
        {
            ce::pooled_owner<IRHIQueueCommandBatch> batch;
            uint64_t value{};
        };
        own::shared_owner<ProbeFence> m_fence = own::make_shared<ProbeFence>();
        std::array<Pending, 128> m_pending;
        ProbeEvents* m_events{};
        bool m_rejectSubmit{};
        bool m_failSignal{};
        bool m_throwBeforeExecute{};
        bool m_throwAfterExecute{};
        bool m_fabricatePoint{};
        uint32_t m_submissions{};
        RHIQueueIdentity GetIdentity() const override { return m_fence->m_identity; }
        bool Signal(uint64_t value, RHITimelinePoint& point, std::string& error) override
        {
            require(submissionDepth != 0, "native signal bypassed submission owner");
            point = {};
            if (!value || value == UINT64_MAX || value <= m_fence->m_issued.load() || m_failSignal)
            {
                error = "injected/invalid native signal";
                return false;
            }
            if (!m_fabricatePoint)
            {
                m_fence->m_issued.store(value);
            }
            point = {m_fence, value};
            if (m_events)
            {
                m_events->Add(GetIdentity().queueId * 100 + 3);
            }
            return true;
        }
        bool Wait(const RHITimelinePoint& point, std::string& error) override
        {
            require(submissionDepth != 0, "native wait bypassed submission owner");
            if (!ValidateQueueWait(GetIdentity(), point, error))
            {
                return false;
            }
            if (m_events)
            {
                m_events->Add(GetIdentity().queueId * 100 + 1);
            }
            return true;
        }
        bool Submit(const ce::pooled_owner<IRHIQueueCommandBatch>& batch, uint64_t value, RHITimelinePoint& point,
                    std::string& error) override
        {
            require(submissionDepth != 0, "native submit bypassed submission owner");
            const auto native = ce::dynamic_pooled_cast<ProbeRecording>(batch);
            require(native && native->m_sealed && !native->m_submitted && native->GetQueueIdentity() == GetIdentity(),
                    "invalid translated native batch");
            native->m_submitted = true;
            ++m_submissions;
            if (m_rejectSubmit)
            {
                error = "injected native submission rejection";
                return false;
            }
            if (m_throwBeforeExecute)
            {
                throw std::runtime_error("injected pre-execute exception");
            }
            auto slot = std::ranges::find_if(m_pending, [](const auto& entry) { return !entry.batch; });
            require(slot != m_pending.end(), "fixture native pending capacity exceeded");
            slot->batch = batch;
            RHICommandFailureScope::SetExecutionUncertain(true);
            if (m_events)
            {
                m_events->Add(GetIdentity().queueId * 100 + 2);
            }
            if (m_throwAfterExecute)
            {
                throw std::runtime_error("injected post-execute exception");
            }
            if (!Signal(value, point, error))
            {
                return false; // Executed but unfenced: quarantine value zero until queue destruction.
            }
            slot->value = value;
            RHICommandFailureScope::SetExecutionUncertain(false);
            return true;
        }
        size_t CollectCompleted() override
        {
            size_t released{};
            for (auto& entry : m_pending)
            {
                if (entry.batch && entry.value && entry.value <= m_fence->m_completed.load())
                {
                    entry = {};
                    ++released;
                }
            }
            return released;
        }
        size_t GetPendingBatchCount() const override
        {
            return std::ranges::count_if(m_pending, [](const auto& entry) { return bool(entry.batch); });
        }
    };

    class ProbeNativeRecorder final : public IRHIQueueRecorder
    {
      public:
        ProbeNativeRecorder(ProbeDeviceServices& services, job_scheduler& scheduler, uint32_t workers)
            : IRHIQueueRecorder(scheduler), m_services(services), m_workers(workers)
        {
        }
        ProbeDeviceServices& m_services;
        uint32_t m_workers;
        ProbeReplayState m_replay;
        own::weak_owner<ProbeRecording> m_last;
        bool m_failBegin{};
        bool m_failFinish{};
        std::atomic<bool> m_rejectAdmission{};
        std::atomic<uint32_t> m_ownerEntries{};
        bool m_dispatchToOwner{};
        RHICommandFailure m_ownerFailure;
        uint64_t m_nextRecordingId{1};
        IRenderDeviceServices& DeviceServices() const override { return m_services; }
        uint32_t GetWorkerCount() const override { return m_workers; }
        bool BeginRecording(const own::shared_owner<IRHICommandQueue>& queue, uint32_t count, ce::lifetime_token token,
                            ce::pooled_owner<IRHIQueueRecording>& result, std::string& error) override
        {
            require(count > 0 && count <= 8, "native begin outside target contract");
            if (m_failBegin)
            {
                error = "injected native begin failure";
                return false;
            }
            auto recording = own::make_shared<ProbeRecording>();
            recording->m_identity = queue->GetIdentity();
            recording->m_targetCount = count;
            recording->m_owner = std::move(token);
            recording->m_replay = &m_replay;
            recording->m_id = m_nextRecordingId++;
            recording->m_failFinish = m_failFinish;
            m_last = recording;
            result = std::move(recording);
            return true;
        }
        bool Record(const own::shared_owner<IRHICommandQueue>& queue, const std::function<void(RHIEncoder&)>& commands,
                    ce::lifetime_token token, ce::pooled_owner<IRHIQueueCommandBatch>& batch,
                    std::string& error) override
        {
            ce::pooled_owner<IRHIQueueRecording> recording;
            if (!BeginRecording(queue, 1, std::move(token), recording, error))
            {
                return false;
            }
            commands(recording->AcquireEncoder(0));
            return recording->Finish(batch, error);
        }
        bool ExecuteSubmission(const std::function<bool(std::string&)>& submit, std::string& error) override
        {
            if (m_rejectAdmission.load())
            {
                error = "injected owner admission rejection";
                return false;
            }
            const auto execute = [&]
            {
                // The base backend may execute inline. The adapter's transaction
                // lock must serialize callers even without a backend queue thread.
                struct OwnerScope
                {
                    OwnerScope() { ++submissionDepth; }
                    ~OwnerScope() { --submissionDepth; }
                } owner;
                m_ownerEntries.fetch_add(1);
                return submit(error);
            };
            if (!m_dispatchToOwner || submissionDepth)
            {
                return execute();
            }
            // A real thread boundary checks that production's synchronous handoff
            // forwards the request context and restores both TLS scopes on failure.
            m_ownerFailure.Reset();
            bool accepted{};
            bool restored{};
            std::exception_ptr exception;
            std::thread owner(
                [&]
                {
                    try
                    {
                        const RHICommandFailureScope diagnostic(m_ownerFailure);
                        accepted = execute();
                    }
                    catch (...)
                    {
                        exception = std::current_exception();
                    }
                    restored = RHICommandFailureScope::Current() == nullptr;
                });
            owner.join();
            require(restored, "owner failure sink escaped its synchronous handoff");
            if (exception)
            {
                std::rethrow_exception(exception);
            }
            return accepted;
        }
    };

    class ProbeFactory final : public IRHICommandQueueFactory
    {
      public:
        uint32_t m_created{};
        RHIQueueCapabilities QueryQueueCapabilities() const override { return {true, true, true, true}; }
        bool CreateQueue(RHIQueueKind kind, own::shared_owner<IRHICommandQueue>& queue, std::string&) override
        {
            auto native = own::make_shared<ProbeQueue>();
            native->m_fence->m_identity = {83, ++m_created, kind};
            queue = std::move(native);
            return true;
        }
    };

    struct ProbeContext
    {
        ProbeDeviceServices services;
        own::shared_owner<ProbeNativeRecorder> native;
        own::shared_owner<ProbeValidator> validator = own::make_shared<ProbeValidator>();
        std::unique_ptr<RHISoftwareQueueRecorder> recorder;
        own::shared_owner<ProbeQueue> nativeQueue = own::make_shared<ProbeQueue>();
        own::shared_owner<IRHICommandQueue> queue;
        explicit ProbeContext(job_scheduler& scheduler, uint32_t workers = 8, bool parallel = true,
                              uint64_t arenaBytes = 256ull << 20)
        {
            native = own::make_shared<ProbeNativeRecorder>(services, scheduler, workers);
            RHISoftwareQueueOptions options;
            options.maxPendingArenaBytes = arenaBytes;
            if (parallel)
            {
                options.translation = {RHICommandTranslationMode::Parallel, 8, 0};
            }
            recorder = std::make_unique<RHISoftwareQueueRecorder>(native, validator, options, scheduler);
            RHISoftwareBatchDesc desc;
            desc.backendGeneration = 19;
            desc.frameId = 71;
            recorder->SetFrameDesc(desc);
            nativeQueue->m_fence->m_identity = {83, 1, RHIQueueKind::Graphics};
            std::string error;
            require(recorder->WrapQueue(nativeQueue, queue, error), "queue wrapping failed");
        }
        ~ProbeContext()
        {
            // The fake device/recorder is kept alive until all fake GPU leases
            // have been retired, including the deliberately unfenced cases.
            nativeQueue->m_pending = {};
        }
        ce::pooled_owner<IRHIQueueCommandBatch> Record(uint32_t targets, ce::lifetime_token token, uint32_t base = 100)
        {
            ce::pooled_owner<IRHIQueueRecording> recording;
            std::string error;
            require(recorder->BeginRecording(queue, targets, std::move(token), recording, error),
                    "software begin failed");
            for (uint32_t segment = 0; segment < (native->GetWorkerCount() ? 2u : 1u); ++segment)
            {
                for (uint32_t target = 0; target < targets; ++target)
                {
                    auto& encoder = recording->AcquireEncoder(target);
                    for (uint32_t command = 0; command <= target; ++command)
                    {
                        encoder.Dispatch(base + target * 16 + segment * 8 + command, 1, 1);
                    }
                }
            }
            ce::pooled_owner<IRHIQueueCommandBatch> batch;
            if (!recording->Finish(batch, error))
            {
                throw std::runtime_error("software finish failed: " + error);
            }
            require(bool(batch), "software finish returned no batch");
            return batch;
        }
        void Drain()
        {
            nativeQueue->m_fence->m_completed.store(nativeQueue->m_fence->m_issued.load());
            queue->CollectCompleted();
        }
    };

    void verify_basic(job_scheduler& scheduler)
    {
        for (const auto workerCount : {0u, 1u, 4u, 8u})
        {
            for (const bool parallel : {false, true})
            {
                ProbeContext context(scheduler, workerCount, parallel);
                const auto targets = (std::max)(1u, workerCount);
                auto owner = own::make_shared<int>(17);
                const own::weak_owner<int> lifetime = owner;
                auto batch = context.Record(targets, owner);
                auto alias = batch;
                require(context.native->m_replay.calls.load() == 0, "recording performed native replay early");
                owner.reset();
                RHITimelinePoint point;
                std::string error;
                require(context.queue->Submit(batch, 1, point, error), "software submit failed");
                require(point.IsValid() && point.value == 1 && !point.IsComplete(), "point was not actually issued");
                const auto native = context.native->m_last.lock();
                require(native && native->m_targetCount == targets, "native target count changed");
                for (uint32_t target = 0; target < targets; ++target)
                {
                    const auto& encoder = native->m_encoders[target];
                    const auto segments = workerCount ? 2u : 1u;
                    require(encoder.m_count == segments * (target + 1), "target lost or duplicated commands");
                    require(encoder.m_segments == (workerCount ? 2u : 1u), "segment order/serial adapter changed");
                    for (uint32_t segment = 0; segment < segments; ++segment)
                    {
                        for (uint32_t command = 0; command <= target; ++command)
                        {
                            require(encoder.m_values[segment * (target + 1) + command] ==
                                        100 + target * 16 + segment * 8 + command,
                                    "target replay order changed");
                        }
                    }
                }
                const auto calls = context.native->m_replay.calls.load();
                require(!context.queue->Submit(alias, 2, point, error) && !point.IsValid(), "batch replayed twice");
                require(context.native->m_replay.calls.load() == calls, "consumed alias was translated again");
                batch.reset();
                alias.reset();
                require(!lifetime.expired() && context.queue->GetPendingBatchCount() == 1,
                        "CPU alias release retired GPU owner early");
                context.Drain();
                require(!lifetime.expired(), "independent native inspection alias was not strong");
                // native is still an inspection alias until this scope exits.
            }
        }
        ProbeContext context(scheduler);
        std::string error;
        auto owner = own::make_shared<int>(1);
        ce::pooled_owner<IRHIQueueCommandBatch> batch;
        uint32_t callbacks{};
        require(context.recorder->Record(
                    context.queue,
                    [&](RHIEncoder& encoder)
                    {
                        ++callbacks;
                        encoder.Dispatch(33, 1, 1);
                    },
                    owner, batch, error),
                "callback recording failed");
        RHITimelinePoint point;
        require(context.queue->Submit(batch, 1, point, error) && callbacks == 1,
                "translation reentered graph callback");
        batch.reset();
        context.Drain();
    }

    void verify_submission_preparation(job_scheduler& scheduler)
    {
        {
            ProbeContext context(scheduler);
            auto owner = own::make_shared<int>(1);
            const own::weak_owner<int> lifetime = owner;
            auto batch = context.Record(8, owner);
            owner.reset();
            std::string error;
            require(context.recorder->PrepareSubmissionBatch(context.queue, batch, error),
                    "pre-submit native preparation failed");
            const auto calls = context.native->m_replay.calls.load();
            auto native = context.native->m_last.lock();
            require(calls == 72 && native && native->m_sealed && context.native->m_replay.active.load() == 0,
                    "preparation returned before native targets were translated and sealed");
            require(context.native->m_ownerEntries.load() == 0 && context.nativeQueue->m_submissions == 0 &&
                        context.nativeQueue->m_fence->m_issued.load() == 0,
                    "preparation entered the native submission transaction");
            require(context.recorder->PrepareSubmissionBatch(context.queue, batch, error) &&
                        context.native->m_replay.calls.load() == calls,
                    "idempotent prepared-batch inspection replayed native commands");
            RHITimelinePoint point;
            require(context.recorder->ExecuteSubmission(
                        [&](std::string& failure) { return context.queue->Submit(batch, 1, point, failure); }, error),
                    "prepared batch submission failed");
            require(context.native->m_replay.calls.load() == calls && context.native->m_ownerEntries.load() != 0 &&
                        context.nativeQueue->m_submissions == 1 && point.IsValid(),
                    "native submission replayed commands or did not consume the prepared batch once");
            batch.reset();
            native.reset();
            require(!lifetime.expired(), "prepared submission lost the native GPU lifetime owner");
            context.Drain();
            require(lifetime.expired(), "prepared batch did not retire after native completion");
        }
        {
            ProbeContext context(scheduler);
            auto owner = own::make_shared<int>(2);
            const own::weak_owner<int> lifetime = owner;
            auto batch = context.Record(4, owner);
            owner.reset();
            std::string error;
            require(context.recorder->PrepareSubmissionBatch(context.queue, batch, error),
                    "cancelled batch preparation failed");
            require(!context.native->m_last.expired() && !lifetime.expired(),
                    "prepared batch did not retain its native storage");
            batch.reset();
            require(context.native->m_last.expired() && lifetime.expired() && context.nativeQueue->m_submissions == 0 &&
                        context.native->m_ownerEntries.load() == 0,
                    "cancelling prepared work leaked native storage or submitted it");
        }
        for (uint32_t failure = 0; failure < 4; ++failure)
        {
            ProbeContext context(scheduler);
            auto first = context.Record(1, own::make_shared<int>(3));
            auto owner = own::make_shared<int>(4);
            const own::weak_owner<int> lifetime = owner;
            auto rejected = context.Record(8, owner, 200);
            owner.reset();
            std::string error;
            require(context.recorder->PrepareSubmissionBatch(context.queue, first, error),
                    "first batch did not prepare before later preparation failure");
            switch (failure)
            {
            case 0:
                context.native->m_failBegin = true;
                break;
            case 1:
                context.native->m_failFinish = true;
                break;
            case 2:
                context.native->m_replay.failValue = 200;
                break;
            case 3:
                ++context.services.m_descriptorVersion;
                break;
            }
            RHICommandFailure diagnostic;
            {
                const RHICommandFailureScope capture(diagnostic);
                require(!context.recorder->PrepareSubmissionBatch(context.queue, rejected, error) && !error.empty(),
                        "failed preparation was accepted");
            }
            require(context.native->m_ownerEntries.load() == 0 && context.nativeQueue->m_submissions == 0 &&
                        context.native->m_replay.active.load() == 0,
                    "preparation failure submitted a prefix or left translation jobs active");
            require(diagnostic.stage == RHICommandFailureStage::Translation && !diagnostic.executionUncertain &&
                        diagnostic.recordingId == rejected->GetRecordingId() && diagnostic.backendGeneration == 19,
                    "preparation failure lost translation identity or reported uncertain execution");
            const auto calls = context.native->m_replay.calls.load();
            require(!context.recorder->PrepareSubmissionBatch(context.queue, rejected, error) &&
                        context.native->m_replay.calls.load() == calls,
                    "failed preparation retried a partly translated batch");
            rejected.reset();
            require(lifetime.expired(), "failed preparation retained an unsubmitted native owner");
            first.reset();
        }
        std::cout << "{\"test\":\"pre_submit_native_preparation\",\"failure_cases\":4,"
                     "\"replay_before_transaction\":true,\"no_replay_during_submit\":true,"
                     "\"cancel_releases_native_owner\":true,\"native_or_gpu\":false}\n";
    }

    void verify_failures(job_scheduler& scheduler)
    {
        for (uint32_t failure = 0; failure < 9; ++failure)
        {
            ProbeContext context(scheduler);
            auto owner = own::make_shared<int>(1);
            own::weak_owner<int> lifetime = owner;
            auto batch = context.Record(8, owner);
            owner.reset();
            switch (failure)
            {
            case 0:
                context.native->m_failBegin = true;
                break;
            case 1:
                context.native->m_failFinish = true;
                break;
            case 2:
                context.native->m_replay.failValue = 100;
                break;
            case 3:
                context.native->m_replay.failValue = 100;
                context.native->m_replay.unknownFailure = true;
                break;
            case 4:
                ++context.services.m_descriptorVersion;
                break;
            case 5:
                context.nativeQueue->m_rejectSubmit = true;
                break;
            case 6:
                context.nativeQueue->m_failSignal = true;
                break;
            case 7:
                context.nativeQueue->m_throwBeforeExecute = true;
                break;
            case 8:
                context.nativeQueue->m_throwAfterExecute = true;
                break;
            }
            RHITimelinePoint point;
            RHICommandFailure diagnostic;
            RHICommandFailure transactionFailure;
            {
                const RHICommandFailureScope transaction(transactionFailure);
                require(!context.queue->SubmitWithDiagnostics(batch, 1, point, diagnostic) && !point.IsValid() &&
                            !diagnostic.message.empty(),
                        "failed submission published a point");
            }
            require(transactionFailure.executionUncertain == diagnostic.executionUncertain &&
                        transactionFailure.stage == diagnostic.stage &&
                        transactionFailure.recordingId == diagnostic.recordingId &&
                        transactionFailure.backendGeneration == diagnostic.backendGeneration,
                    "nested graph transaction did not receive the submission failure context");
            require(diagnostic.recordingId == batch->GetRecordingId() && diagnostic.backendGeneration == 19,
                    "failure lost the sealed software recording identity");
            require(diagnostic.stage ==
                        (failure < 5 ? RHICommandFailureStage::Translation : RHICommandFailureStage::Submission),
                    "failure reported the wrong execution stage");
            require(diagnostic.executionUncertain == (failure == 6 || failure == 8),
                    "preflight rejection and unfenced execution were not distinguished");
            if (failure == 2 || failure == 3)
            {
                require(diagnostic.targetIndex == 0 && diagnostic.commandIndex == 0 &&
                            diagnostic.commandType == RHISoftwareCommandType::Dispatch,
                        "joined translation failure lost target/opcode provenance");
            }
            require(context.native->m_replay.active.load() == 0, "failed replay did not drain all workers");
            const auto calls = context.native->m_replay.calls.load();
            context.native->m_failBegin = false;
            context.native->m_failFinish = false;
            context.native->m_replay.failValue = 0;
            context.nativeQueue->m_rejectSubmit = false;
            context.nativeQueue->m_failSignal = false;
            context.nativeQueue->m_throwBeforeExecute = false;
            context.nativeQueue->m_throwAfterExecute = false;
            require(!context.queue->SubmitWithDiagnostics(batch, 2, point, diagnostic) &&
                        context.native->m_replay.calls.load() == calls,
                    "failed accepted translation was retried");
            require(diagnostic.stage == RHICommandFailureStage::Admission && !diagnostic.executionUncertain &&
                        diagnostic.recordingId == batch->GetRecordingId() && diagnostic.backendGeneration == 19,
                    "rejected retry reused the previous execution failure");
            batch.reset();
            if (failure == 6 || failure == 8)
            {
                require(!lifetime.expired() && context.queue->GetPendingBatchCount() == 1,
                        "unfenced executed batch lost retirement owner");
                context.nativeQueue->m_fence->m_completed.store(UINT64_MAX - 1);
                require(context.queue->CollectCompleted() == 0 && !lifetime.expired(),
                        "unfenced batch was retired by unrelated completion");
                context.nativeQueue->m_pending = {};
            }
            require(lifetime.expired(), "failed unsubmitted software/native owner leaked");
        }
        ProbeContext context(scheduler);
        auto batch = context.Record(1, own::make_shared<int>(1));
        RHICommandFailure diagnostic;
        RHITimelinePoint point;
        context.native->m_rejectAdmission.store(true);
        require(!context.queue->SubmitWithDiagnostics(batch, 1, point, diagnostic) &&
                    context.native->m_replay.calls.load() == 0,
                "owner admission failure consumed work early");
        require(diagnostic.stage == RHICommandFailureStage::Admission && !diagnostic.executionUncertain &&
                    diagnostic.recordingId == batch->GetRecordingId() && diagnostic.backendGeneration == 19,
                "owner rejection was reported as accepted execution");
        context.native->m_rejectAdmission.store(false);
        require(context.queue->SubmitWithDiagnostics(batch, 1, point, diagnostic),
                "unaccepted owner transaction could not be retried");
        require(diagnostic.message.empty() && !diagnostic.executionUncertain && diagnostic.recordingId == 0 &&
                    diagnostic.backendGeneration == 0 && diagnostic.targetIndex == UINT32_MAX,
                "successful submit retained stale failure fields");
        context.Drain();
    }

    void verify_failure_handoff(job_scheduler& scheduler)
    {
        require(RHICommandFailureScope::Current() == nullptr, "caller began with a stale diagnostic sink");
        for (const bool uncertain : {false, true})
        {
            ProbeContext context(scheduler);
            auto batch = context.Record(8, own::make_shared<int>(1));
            context.native->m_dispatchToOwner = true;
            context.nativeQueue->m_throwBeforeExecute = !uncertain;
            context.nativeQueue->m_throwAfterExecute = uncertain;
            RHITimelinePoint point;
            RHICommandFailure failure;
            require(!context.queue->SubmitWithDiagnostics(batch, 1, point, failure) && !point.IsValid(),
                    "cross-thread failure was accepted");
            const auto& ownerFailure = context.native->m_ownerFailure;
            require(failure.executionUncertain == uncertain && ownerFailure.executionUncertain == uncertain &&
                        failure.recordingId == batch->GetRecordingId() &&
                        ownerFailure.recordingId == failure.recordingId && ownerFailure.backendGeneration == 19 &&
                        ownerFailure.stage == RHICommandFailureStage::Submission,
                    "synchronous owner handoff lost caller/ticket failure context");
            require(RHICommandFailureScope::Current() == nullptr, "caller diagnostic sink was not restored");
        }
        ProbeContext context(scheduler);
        context.native->m_dispatchToOwner = true;
        auto batch = context.Record(1, own::make_shared<int>(1));
        RHITimelinePoint point;
        RHICommandFailure failure;
        failure.executionUncertain = true;
        failure.message = "stale";
        require(context.queue->SubmitWithDiagnostics(batch, 1, point, failure) && point.IsValid() &&
                    !failure.executionUncertain && failure.message.empty() && failure.recordingId == 0,
                "cross-thread success did not reset a previous failure");
        require(RHICommandFailureScope::Current() == nullptr, "successful handoff left a diagnostic sink installed");
        context.Drain();
        std::cout << "{\"test\":\"structured_failure_diagnostics\",\"translation_failure_cases\":5,"
                     "\"submission_failure_cases\":4,\"retry_admission_checks\":9,\"cross_thread_handoff_cases\":3,"
                     "\"native_or_gpu\":false}\n";
    }

    void verify_failure_drain(job_scheduler& scheduler)
    {
        ProbeContext context(scheduler);
        ProbeGate gate;
        context.native->m_replay.failValue = 100;
        context.native->m_replay.gatedValue = 212;
        context.native->m_replay.gate = &gate;
        auto batch = context.Record(8, own::make_shared<int>(1));
        std::atomic<bool> returned{};
        bool result{};
        std::string error;
        RHITimelinePoint point;
        std::thread submitter(
            [&]
            {
                result = context.queue->Submit(batch, 1, point, error);
                returned.store(true);
            });
        try
        {
            gate.Wait();
            require(!returned.load(), "join returned while one accepted replay was blocked");
        }
        catch (...)
        {
            gate.Release();
            submitter.join();
            throw;
        }
        gate.Release();
        submitter.join();
        require(returned.load() && !result && !point.IsValid() && context.native->m_replay.active.load() == 0,
                "failure join did not release every target");
    }

    void verify_concurrent_aliases(job_scheduler& scheduler)
    {
        ProbeContext context(scheduler);
        auto batch = context.Record(8, own::make_shared<int>(1));
        std::barrier start(3);
        std::array<bool, 2> results{};
        std::array<RHITimelinePoint, 2> points;
        std::array<std::string, 2> errors;
        const auto submit = [&](size_t index)
        {
            start.arrive_and_wait();
            results[index] = context.queue->Submit(batch, index + 1, points[index], errors[index]);
        };
        std::thread first(submit, 0);
        std::thread second(submit, 1);
        start.arrive_and_wait();
        first.join();
        second.join();
        require(results[0] != results[1] && points[0].IsValid() != points[1].IsValid() &&
                    context.nativeQueue->m_submissions == 1 && context.native->m_replay.calls.load() == 72,
                "concurrent aliases replayed or submitted the same batch twice");
        context.Drain();
    }

    void verify_versions_and_cancel(job_scheduler& scheduler)
    {
        ProbeContext context(scheduler);
        auto owner = own::make_shared<int>(1);
        own::weak_owner<int> lifetime = owner;
        ce::pooled_owner<IRHIQueueRecording> recording;
        ce::pooled_owner<IRHIQueueCommandBatch> batch;
        std::string error;
        require(context.recorder->BeginRecording(context.queue, 1, owner, recording, error), "begin failed");
        recording->AcquireEncoder(0).Dispatch(7, 1, 1);
        owner.reset();
        ++context.services.m_uploadRecordingId;
        require(!recording->Finish(batch, error) && !batch, "changed upload recording was accepted");
        recording.reset();
        require(lifetime.expired(), "cancel retained a frame owner in idle pool");
        require(!context.recorder->BeginRecording(context.queue, 0, own::make_shared<int>(1), recording, error),
                "zero target count accepted");
        require(!context.recorder->BeginRecording(context.queue, 9, own::make_shared<int>(1), recording, error),
                "oversized target count accepted");
        require(!context.recorder->BeginRecording(context.nativeQueue, 1, own::make_shared<int>(1), recording, error),
                "native queue bypassed domain validation");
        require(!context.recorder->BeginRecording(context.queue, 1, {}, recording, error),
                "ownerless recording accepted");
        auto sealed = context.Record(1, own::make_shared<int>(1));
        RHITimelinePoint point;
        require(!context.queue->Submit(sealed, 0, point, error), "reserved signal accepted");
        require(context.queue->Submit(sealed, 1, point, error), "invalid argument consumed an otherwise sealed batch");
        context.Drain();
        require(context.recorder->BeginRecording(context.queue, 8, own::make_shared<int>(1), recording, error),
                "empty batch begin failed");
        require(recording->Finish(batch, error), "empty batch seal failed");
        recording.reset();
        require(context.queue->Submit(batch, 2, point, error), "empty owned batch was not supported");
        context.Drain();
        context.nativeQueue->m_fabricatePoint = true;
        require(!context.queue->Signal(3, point, error) && !point.IsValid(), "unissued future signal accepted");

        ProbeContext serial(scheduler, 0);
        require(serial.recorder->BeginRecording(serial.queue, 1, own::make_shared<int>(1), recording, error),
                "serial-only software begin failed");
        recording->AcquireEncoder(0).Dispatch(1, 1, 1);
        recording->AcquireEncoder(0).Dispatch(2, 1, 1);
        require(recording->Finish(batch, error), "serial-only multi-segment software seal failed");
        recording.reset();
        require(!serial.queue->Submit(batch, 1, point, error) && serial.native->m_replay.calls.load() == 0,
                "serial-only recorder replayed segments without logical state reset");
    }

    void verify_direct_translator(job_scheduler& scheduler)
    {
        ProbeContext context(scheduler);
        for (const bool parallel : {false, true})
        {
            RHISoftwareRecorder software(*context.validator);
            std::string error;
            require(software.BeginRecording({context.nativeQueue->GetIdentity(), 8, 1 << 20, 1 << 20}, error),
                    "direct software begin failed");
            for (uint32_t segment = 0; segment < 3; ++segment)
            {
                for (uint32_t target = 0; target < 8; ++target)
                {
                    software.AcquireEncoder(target).Dispatch(100 + target * 16 + segment, 1, 1);
                }
            }
            const std::array<uint32_t, 8> order{7, 2, 5, 0, 6, 1, 4, 3};
            RHISoftwareBatchDesc desc;
            desc.backendGeneration = 19;
            desc.uploadRecordingId = context.services.GetCurrentUploadRecordingId();
            desc.descriptorVersion = context.services.GetDescriptorVersionToken();
            desc.queue = context.nativeQueue->GetIdentity();
            desc.lifetimeToken = own::make_shared<int>(1);
            RHISoftwareBatch input;
            require(software.FinishRecording(order, desc, input, error), "direct software seal failed");
            RHITranslatedBatch translated;
            RHICommandFailure failure;
            RHIQueueCommandTranslator translator(*context.native, context.nativeQueue);
            const RHICommandTranslationOptions options{parallel ? RHICommandTranslationMode::Parallel
                                                                : RHICommandTranslationMode::Serial,
                                                       parallel ? 8u : 1u, 0};
            require(context.native->ExecuteSubmission(
                        [&](std::string&)
                        { return translator.Translate(std::move(input), options, translated, failure); }, error),
                    "direct owned translation failed");
            require(!input.IsValid() && translated.GetKind() == RHITranslatedBatchKind::OwnedQueue,
                    "owned translated batch tag/move was invalid");
            require(translated.GetTranslationStats().translationJobCount == (parallel ? 8u : 1u),
                    "translation job admission changed");
            const auto native = context.native->m_last.lock();
            require(bool(native), "translated native lease disappeared");
            for (uint32_t slot = 0; slot < 8; ++slot)
            {
                for (uint32_t segment = 0; segment < 3; ++segment)
                {
                    require(native->m_encoders[slot].m_values[segment] == 100 + order[slot] * 16 + segment,
                            "translation changed original targetOrder");
                }
            }
            // Rejecting an already-populated output also consumes the input,
            // preserving exactly-once translation semantics.
            require(software.BeginRecording({desc.queue, 1, 1024, 1024}, error), "second direct begin failed");
            software.AcquireEncoder(0).Dispatch(1, 1, 1);
            const std::array<uint32_t, 1> single{0};
            require(software.FinishRecording(single, desc, input, error), "second direct seal failed");
            require(!translator.Translate(std::move(input), options, translated, failure) && !input.IsValid(),
                    "nonempty translation output was overwritten");
        }
        // Two independent failures: canonical metadata follows the original
        // targetOrder even when the worker claim/completion order differs.
        context.native->m_replay.failValue = 100;
        context.native->m_replay.secondFailValue = 107;
        RHISoftwareRecorder software(*context.validator);
        std::string error;
        require(software.BeginRecording({context.nativeQueue->GetIdentity(), 8, 65536, 65536}, error),
                "canonical failure begin failed");
        for (uint32_t target = 0; target < 8; ++target)
        {
            software.AcquireEncoder(target).Dispatch(100 + target, 1, 1);
        }
        const std::array<uint32_t, 8> order{7, 3, 4, 5, 0, 2, 6, 1};
        RHISoftwareBatchDesc desc;
        desc.backendGeneration = 19;
        desc.uploadRecordingId = context.services.GetCurrentUploadRecordingId();
        desc.descriptorVersion = context.services.GetDescriptorVersionToken();
        desc.queue = context.nativeQueue->GetIdentity();
        desc.lifetimeToken = own::make_shared<int>(1);
        RHISoftwareBatch input;
        require(software.FinishRecording(order, desc, input, error), "canonical failure seal failed");
        const auto recordingId = input.GetRecordingId();
        RHITranslatedBatch translated;
        RHICommandFailure failure;
        RHIQueueCommandTranslator translator(*context.native, context.nativeQueue);
        require(!context.native->ExecuteSubmission(
                    [&](std::string&)
                    {
                        return translator.Translate(std::move(input), {RHICommandTranslationMode::Parallel, 8, 0},
                                                    translated, failure);
                    },
                    error),
                "failed direct translation succeeded");
        require(!translated.IsValid() && !input.IsValid() && failure.targetIndex == 7 &&
                    failure.recordingId == recordingId && failure.backendGeneration == 19 &&
                    failure.message == "injected native replay failure" &&
                    context.native->m_replay.failures.load() == 2,
                "canonical failure identity/order changed");
    }

    void verify_queue_transaction(job_scheduler& scheduler)
    {
        ProbeContext context(scheduler);
        ProbeEvents events;
        context.nativeQueue->m_events = &events;
        auto computeNative = own::make_shared<ProbeQueue>();
        computeNative->m_fence->m_identity = {83, 2, RHIQueueKind::Compute};
        computeNative->m_events = &events;
        own::shared_owner<IRHICommandQueue> compute;
        std::string error;
        require(context.recorder->WrapQueue(computeNative, compute, error), "compute wrapping failed");
        auto graphicsFirst = context.Record(1, own::make_shared<int>(1));
        ce::pooled_owner<IRHIQueueCommandBatch> computeBatch;
        require(context.recorder->Record(
                    compute, [](RHIEncoder& encoder) { encoder.Dispatch(7, 1, 1); }, own::make_shared<int>(1),
                    computeBatch, error),
                "compute recording failed");
        auto graphicsLast = context.Record(1, own::make_shared<int>(1));
        ProbeGate middle;
        bool transactionResult{};
        std::string transactionError;
        std::thread transaction(
            [&]
            {
                transactionResult = context.recorder->ExecuteSubmission(
                    [&](std::string& failure)
                    {
                        RHITimelinePoint first;
                        RHITimelinePoint second;
                        RHITimelinePoint third;
                        if (!context.queue->Submit(graphicsFirst, 1, first, failure))
                        {
                            return false;
                        }
                        middle.Enter();
                        return compute->Wait(first, failure) && compute->Submit(computeBatch, 1, second, failure) &&
                               context.queue->Wait(second, failure) &&
                               context.queue->Submit(graphicsLast, 2, third, failure);
                    },
                    transactionError);
            });
        middle.Wait();
        const auto entries = context.native->m_ownerEntries.load();
        bool externalResult{};
        std::string externalError;
        RHITimelinePoint externalPoint;
        std::thread external([&] { externalResult = context.queue->Signal(3, externalPoint, externalError); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (context.native->m_ownerEntries.load() == entries && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        const bool externalEntered = context.native->m_ownerEntries.load() != entries;
        middle.Release();
        transaction.join();
        external.join();
        require(transactionResult && externalResult && externalPoint.IsValid() && externalEntered,
                "whole queue transaction or concurrent admission failed");
        const std::array<uint32_t, 9> expected{102, 103, 201, 202, 203, 101, 102, 103, 103};
        require(events.size == expected.size(), "transaction event count changed");
        for (size_t index = 0; index < expected.size(); ++index)
        {
            require(events.values[index] == expected[index], "external operation overtook graph transaction");
        }
        context.Drain();
        computeNative->m_fence->m_completed.store(1);
        require(compute->CollectCompleted() == 1, "compute native retirement was not forwarded");
        ProbeContext other(scheduler);
        auto foreign = other.Record(1, own::make_shared<int>(1));
        RHITimelinePoint point;
        require(!context.queue->Submit(foreign, 4, point, error), "foreign recorder domain batch accepted");
        auto fence = own::make_shared<ProbeFence>();
        fence->m_identity = {99, 1, RHIQueueKind::Graphics};
        fence->m_issued.store(1);
        require(!context.queue->Wait({fence, 1}, error), "cross-device timeline wait accepted");
    }

    void verify_pool_lifetime(job_scheduler& scheduler)
    {
        ProbeDeviceServices services;
        auto native = own::make_shared<ProbeNativeRecorder>(services, scheduler, 8);
        auto validator = own::make_shared<ProbeValidator>();
        own::weak_owner<ProbeNativeRecorder> nativeLifetime = native;
        own::weak_owner<ProbeValidator> validatorLifetime = validator;
        auto recorder =
            std::make_unique<RHISoftwareQueueRecorder>(native, validator, RHISoftwareQueueOptions{}, scheduler);
        RHISoftwareBatchDesc desc;
        desc.backendGeneration = 1;
        recorder->SetFrameDesc(desc);
        ProbeFactory factory;
        RHISoftwareQueueFactory wrappedFactory(factory, *recorder);
        require(wrappedFactory.QueryQueueCapabilities().crossQueueTimeline, "factory capabilities were changed");
        own::shared_owner<IRHICommandQueue> queue;
        std::string error;
        require(wrappedFactory.CreateQueue(RHIQueueKind::Graphics, queue, error) && factory.m_created == 1,
                "wrapper created extra native queues");
        require(recorder->WrapQueue(queue, queue, error) && queue, "in-place idempotent wrapping lost queue");
        std::array<ce::pooled_owner<IRHIQueueCommandBatch>, 65> batches;
        auto owner = own::make_shared<int>(1);
        const own::weak_owner<int> lifetime = owner;
        for (auto& batch : batches)
        {
            require(recorder->Record(
                        queue, [](RHIEncoder& encoder) { encoder.Dispatch(17, 1, 1); }, owner, batch, error),
                    "simultaneous software recordings failed");
        }
        owner.reset();
        native.reset();
        validator.reset();
        recorder.reset();
        queue.reset();
        require(!nativeLifetime.expired() && !validatorLifetime.expired() && !lifetime.expired(),
                "CPU recording lost its backend/validator owners");
        for (auto& batch : batches)
        {
            batch.reset();
        }
        require(lifetime.expired() && nativeLifetime.expired() && validatorLifetime.expired(),
                "idle recording pool/control block retained a backend ownership cycle");
    }

    void verify_byte_admission(job_scheduler& scheduler)
    {
        ProbeValidator validator;
        RHISoftwareRecorder sizing(validator);
        std::string error;
        require(sizing.BeginRecording({{83, 1, RHIQueueKind::Graphics}, 1, 65536, 65536}, error),
                "arena credit sizing begin failed");
        sizing.AcquireEncoder(0).Dispatch(1, 1, 1);
        const auto arenaBytes = sizing.GetRecordingStats().allocatedBytes;
        require(arenaBytes > 0, "arena credit sizing did not activate backing storage");
        sizing.CancelRecording();
        ProbeContext context(scheduler, 8, true, arenaBytes);
        auto first = context.Record(1, own::make_shared<int>(1));
        auto owner = own::make_shared<int>(2);
        const own::weak_owner<int> failedOwner = owner;
        ce::pooled_owner<IRHIQueueRecording> rejected;
        ce::pooled_owner<IRHIQueueCommandBatch> output;
        require(context.recorder->BeginRecording(context.queue, 1, std::move(owner), rejected, error),
                "byte-limit recording begin failed");
        rejected->AcquireEncoder(0).Dispatch(17, 1, 1);
        require(!rejected->Finish(output, error) && !output && failedOwner.expired() &&
                    context.native->m_replay.calls.load() == 0,
                "aggregate arena limit admitted excess sealed storage");
        RHITimelinePoint point;
        require(context.queue->Submit(first, 1, point, error), "limited byte admission submit failed");
        require(!rejected->Finish(output, error), "rejected seal could be retried after credit changed");
        rejected.reset();
        // first remains aliased and GPU-in-flight; only its CPU arena credit is returned.
        auto next = context.Record(1, own::make_shared<int>(3));
        auto alias = next;
        next.reset();
        require(context.recorder->BeginRecording(context.queue, 1, own::make_shared<int>(4), rejected, error),
                "second byte-limit recording begin failed");
        rejected->AcquireEncoder(0).Dispatch(18, 1, 1);
        require(!rejected->Finish(output, error), "one alias release returned live sealed arena credit");
        rejected.reset();
        alias.reset();
        auto afterCancel = context.Record(1, own::make_shared<int>(5));
        afterCancel.reset();
        context.native->m_failBegin = true;
        auto failure = context.Record(1, own::make_shared<int>(6));
        require(!context.queue->Submit(failure, 2, point, error), "injected translation failure succeeded");
        context.native->m_failBegin = false;
        auto afterFailure = context.Record(1, own::make_shared<int>(7));
        context.native->m_rejectAdmission.store(true);
        require(!context.queue->Submit(afterFailure, 2, point, error), "owner rejection unexpectedly submitted");
        require(context.recorder->BeginRecording(context.queue, 1, own::make_shared<int>(8), rejected, error),
                "owner rejection follow-up recording failed");
        rejected->AcquireEncoder(0).Dispatch(19, 1, 1);
        require(!rejected->Finish(output, error), "owner admission rejection returned unconsumed arena credit");
        rejected.reset();
        afterFailure.reset();
        context.native->m_rejectAdmission.store(false);
        auto final = context.Record(1, own::make_shared<int>(9));
        require(context.queue->Submit(final, 2, point, error), "credit cancellation/reuse failed");
        context.Drain();

        ProbeContext concurrent(scheduler, 8, true, arenaBytes);
        std::array<ce::pooled_owner<IRHIQueueRecording>, 2> recordings;
        std::array<ce::pooled_owner<IRHIQueueCommandBatch>, 2> batches;
        std::array<bool, 2> accepted{};
        std::array<std::string, 2> errors;
        for (auto& recording : recordings)
        {
            require(
                concurrent.recorder->BeginRecording(concurrent.queue, 1, own::make_shared<int>(1), recording, error),
                "concurrent byte-limit begin failed");
            recording->AcquireEncoder(0).Dispatch(21, 1, 1);
        }
        std::barrier start(3);
        const auto seal = [&](size_t index)
        {
            start.arrive_and_wait();
            accepted[index] = recordings[index]->Finish(batches[index], errors[index]);
        };
        std::thread left(seal, 0);
        std::thread right(seal, 1);
        start.arrive_and_wait();
        left.join();
        right.join();
        require(accepted[0] != accepted[1], "concurrent seals overcommitted the domain arena byte limit");
        recordings = {};
        batches = {};
        auto recycled = concurrent.Record(1, own::make_shared<int>(2));
        require(bool(recycled), "concurrent cancellation lost arena credit");
    }

    void verify_warm_reuse(job_scheduler& scheduler)
    {
        ProbeContext context(scheduler);
        const auto owner = own::make_shared<int>(1);
        std::array<const IRHIQueueCommandBatch*, 3> addresses{};
        for (size_t repeat = 0; repeat < 4; ++repeat)
        {
            for (size_t shape = 0; shape < 3; ++shape)
            {
                auto batch = context.Record(shape == 1 ? 1 : 8, owner);
                if (repeat)
                {
                    require(batch.operator->() == addresses[shape], "idle CPU recording object was not reused");
                }
                addresses[shape] = batch.operator->();
            }
        }
        std::array<uint64_t, 3> allocations{};
        std::array<uint64_t, 3> bytes{};
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            for (size_t shape = 0; shape < 3; ++shape)
            {
                const auto before = renderAllocations.load();
                const auto beforeBytes = renderBytes.load();
                auto batch = context.Record(shape == 1 ? 1 : 8, owner);
                require(batch.operator->() == addresses[shape], "warm wrapper address changed");
                const auto count = renderAllocations.load() - before;
                const auto allocatedBytes = renderBytes.load() - beforeBytes;
                if (repeat)
                {
                    require(allocations[shape] == count && bytes[shape] == allocatedBytes,
                            "warmed target capacity regrew during 8/1/8 alternation");
                }
                allocations[shape] = count;
                bytes[shape] = allocatedBytes;
            }
        }
        std::cout << "{\"test\":\"warm_record_cancel\",\"target_shapes\":[8,1,8],\"repetitions\":128,"
                  << "\"render_allocator_calls\":[" << allocations[0] << ',' << allocations[1] << ',' << allocations[2]
                  << "],\"requested_bytes\":[" << bytes[0] << ',' << bytes[1] << ',' << bytes[2]
                  << "],\"includes_control_blocks\":true,\"native_or_gpu\":false}\n";

        // Fail the next warmed shared-lease control block. ce::pooled_owner must
        // return the raw pooled object via its deleter without leaking the owner.
        auto failedOwner = own::make_shared<int>(1);
        const own::weak_owner<int> failedLifetime = failedOwner;
        ce::pooled_owner<IRHIQueueRecording> recording;
        std::string error;
        bool rejected{};
        rejectNextAllocation.store(true);
        try
        {
            context.recorder->BeginRecording(context.queue, 1, std::move(failedOwner), recording, error);
        }
        catch (const std::bad_alloc&)
        {
            rejected = true;
        }
        require(rejected && failedLifetime.expired() && !recording, "failed pooled lease allocation leaked an owner");
        auto recycled = context.Record(8, owner);
        require(recycled.operator->() == addresses[0], "failed lease allocation lost reusable storage");
    }
} // namespace

void* queue_probe_allocate(size_t bytes, size_t alignment)
{
    if (rejectNextAllocation.exchange(false))
    {
        throw std::bad_alloc();
    }
    renderAllocations.fetch_add(1, std::memory_order_relaxed);
    renderBytes.fetch_add(bytes, std::memory_order_relaxed);
    void* pointer{};
    if (posix_memalign(&pointer, (std::max)(alignment, sizeof(void*)), (std::max)(bytes, size_t{1})) != 0)
    {
        throw std::bad_alloc();
    }
    return pointer;
}

void queue_probe_free(void* pointer) noexcept
{
    std::free(pointer);
}

int main()
{
    try
    {
        thread_pool pool;
        job_scheduler scheduler(pool);
        scheduler.start(8);
        activeTest = "basic";
        verify_basic(scheduler);
        activeTest = "submission_preparation";
        verify_submission_preparation(scheduler);
        activeTest = "failures";
        verify_failures(scheduler);
        activeTest = "failure_handoff";
        verify_failure_handoff(scheduler);
        activeTest = "failure_drain";
        verify_failure_drain(scheduler);
        activeTest = "concurrent_aliases";
        verify_concurrent_aliases(scheduler);
        activeTest = "versions_and_cancel";
        verify_versions_and_cancel(scheduler);
        activeTest = "direct_translator";
        verify_direct_translator(scheduler);
        activeTest = "queue_transaction";
        verify_queue_transaction(scheduler);
        activeTest = "pool_lifetime";
        verify_pool_lifetime(scheduler);
        activeTest = "byte_admission";
        verify_byte_admission(scheduler);
        activeTest = "warm_reuse";
        verify_warm_reuse(scheduler);
        scheduler.shutdown();
        std::cout << "{\"test\":\"owned_software_queue\",\"passed\":true,\"checks\":" << checks.load()
                  << ",\"actual_scheduler_workers\":8,\"native_or_gpu\":false}\n";
    }
    catch (const std::exception& exception)
    {
        std::cerr << activeTest << ": " << exception.what() << '\n';
        return 1;
    }
}
