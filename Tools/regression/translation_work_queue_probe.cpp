// 실제 Translate 본문을 고정 저장소 RHI fixture와 컴파일한다. GPU·드라이버 실행은 검증하지 않는다.
#include "Engine/RenderEngine/RHI/RHITranslationWorkQueue.h"
#include "Engine/RenderEngine/RHI/RHIBackendExecution.h"
#include "Engine/RenderEngine/RHI/RHICommandProvenance.h"
#include "Engine/Utility_Framework/Foundation/EngineFunc.h"
#include "Engine/EngineDiagnostics/ProfileScope.h"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#if CE_TRANSLATION_NATIVE_JOBS
#include "Engine/RenderEngine/RHI/RHICommandRecordingJobs.h"
#endif

namespace
{
    std::atomic<bool> count_allocations{};
    std::atomic<std::size_t> allocations{};
    thread_local uint32_t current_job{};
#if CE_TRANSLATION_NATIVE_JOBS
    job_scheduler* native_scheduler{};
#endif

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

void* operator new(std::size_t size)
{
    if (count_allocations.load(std::memory_order_relaxed))
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (auto* pointer = std::malloc(size ? size : 1))
    {
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void* operator new(std::size_t size, std::align_val_t alignment)
{
    const auto width = static_cast<std::size_t>(alignment);
    if (size > SIZE_MAX - width + 1)
    {
        throw std::bad_alloc();
    }
    if (count_allocations.load(std::memory_order_relaxed))
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    const auto rounded = size ? ((size + width - 1) / width) * width : width;
    if (auto* pointer = std::aligned_alloc(width, rounded))
    {
        return pointer;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    std::free(pointer);
}

enum class RHICommandTranslationMode
{
    Serial,
    Parallel
};
enum class RHISoftwareBatchState
{
    Empty,
    Sealed,
    Translating
};
struct RHICommandTranslationOptions
{
    RHICommandTranslationMode mode{RHICommandTranslationMode::Serial};
    uint32_t maxTranslationJobs{1};
    uint64_t minCommandsPerJob{};
};
struct RHICommandTranslationStats
{
    uint32_t translationJobCount{};
    uint64_t commandCount{}, nativeComputeDispatches{};
};
struct RHICommandRecordingStats
{
    uint64_t commandCount{};
};
struct RHIRecordedBatchDesc
{
    uint64_t frameId{17}, backendGeneration{29}, displayToken{31};
    uint64_t sourceEngineFrame{41}, renderViewId{43};
    bool sourceEngineFrameAvailable{true};
    ce::lifetime_token lifetimeToken;
};
struct RHISoftwareBatchDesc : RHIRecordedBatchDesc
{
    uint64_t uploadRecordingId{1}, descriptorVersion{2};
    RHIQueueIdentity queue{11, 13, RHIQueueKind::Graphics};
};
struct RHIRecordedBatch
{
    bool valid{};
    ce::lifetime_token lifetimeToken;
    bool IsValid() const { return valid; }
};
struct RHITranslatedBatch
{
    RHIRecordedBatch m_native;
    ce::pooled_owner<IRHIQueueCommandBatch> m_owned;
    RHICommandTranslationStats m_stats;
    RHICommandProvenance m_provenance;
    bool IsValid() const { return m_native.IsValid() || bool(m_owned); }
};

// These value conversions are copied verbatim by the runner from the production
// header; the shipping profile scopes and backend TLS guard above are real types.
#include "RHIProfilingContextProduction.inl"

template<typename Value, std::size_t Capacity>
struct fixed_range
{
    std::array<Value, Capacity> values_{};
    std::size_t size_{};
    Value* data() { return values_.data(); }
    std::size_t size() const { return size_; }
};

struct RHIEncoder
{
    uint32_t target{};
};
struct ReplayState
{
    std::thread::id owner{std::this_thread::get_id()};
    bool requireInline{};
    std::array<std::atomic<uint32_t>, 8> nextSegment{}, owners{}, visitsByJob{};
    std::array<uint32_t, 8> replayOrder{};
    std::atomic<uint32_t> replayCount{}, costReads{};
    ReplayState()
    {
        for (auto& owner : owners)
        {
            owner.store(UINT32_MAX);
        }
    }
};
struct RHISoftwareCommandList
{
    ReplayState* state{};
    uint64_t cost{};
    uint32_t target{}, segment{};
    bool fail{}, raise{};
    uint64_t GetCommandCount() const
    {
        state->costReads.fetch_add(1, std::memory_order_relaxed);
        return cost;
    }
    bool Replay(RHIEncoder& encoder, RHICommandTranslationStats& stats, RHICommandFailure& failure)
    {
        require(RHIBackendExecutionScope::IsActive(), "native replay escaped the production backend scope");
        require(!state->requireInline || (current_job == 0 && state->owner == std::this_thread::get_id()),
                "one-job translation left its owner thread");
        require(encoder.target == target, "segment replay used a different target encoder");
        auto unowned = UINT32_MAX;
        state->owners[target].compare_exchange_strong(unowned, current_job);
        require(state->owners[target].load() == current_job, "one target moved between logical jobs");
        require(state->nextSegment[target].fetch_add(1) == segment, "target segments reordered or duplicated");
        if (segment == 0)
        {
            state->replayOrder[state->replayCount.fetch_add(1)] = target;
            state->visitsByJob[current_job ? current_job - 1 : 0].fetch_add(1);
        }
        if (raise)
        {
            throw std::runtime_error("fixture replay exception");
        }
        if (fail)
        {
            failure.commandIndex = 1;
            failure.commandType = RHISoftwareCommandType::Dispatch;
            failure.message = "fixture replay failure";
            return false;
        }
        stats.commandCount = cost;
        stats.nativeComputeDispatches = segment;
        return true;
    }
};
struct RHISoftwareRecordingStorage
{
    struct Target
    {
        std::array<RHISoftwareCommandList*, 3> segments{};
        std::size_t activeSegments{};
    };
    std::array<Target, 8> targets{};
    fixed_range<uint32_t, 8> targetOrder;
};
struct RHISoftwareBatch
{
    RHISoftwareRecordingStorage* m_storage{};
    RHISoftwareBatchDesc m_desc;
    RHICommandRecordingStats m_stats;
    RHISoftwareBatchState m_state{RHISoftwareBatchState::Sealed};
    RHISoftwareBatch() = default;
    RHISoftwareBatch(const RHISoftwareBatch&) = delete;
    RHISoftwareBatch& operator=(const RHISoftwareBatch&) = delete;
    RHISoftwareBatch(RHISoftwareBatch&& other) noexcept
        : m_storage(std::exchange(other.m_storage, nullptr)), m_desc(std::move(other.m_desc)),
          m_stats(other.m_stats), m_state(std::exchange(other.m_state, RHISoftwareBatchState::Empty))
    {
    }
    bool IsValid() const { return m_state == RHISoftwareBatchState::Sealed; }
    uint64_t GetRecordingId() const { return 3; }
    const auto& GetDesc() const { return m_desc; }
    const auto& GetRecordingStats() const { return m_stats; }
    uint32_t GetTargetCount() const { return static_cast<uint32_t>(m_storage->targetOrder.size()); }
};
struct IRenderDeviceServices
{
    uint64_t GetCurrentUploadRecordingId() const { return 1; }
    uint64_t GetDescriptorVersionToken() const { return 2; }
};

struct IRHIParallelCommandPool
{
    static constexpr uint32_t kMaxWorkers = 8;
    std::thread::id owner{std::this_thread::get_id()};
    std::array<RHIEncoder, 8> encoders{};
    fixed_range<uint32_t, 8> opened, finalized;
    std::atomic<uint32_t> active{}, completed{};
    bool sequentialCallbacks{}, delayFirst{}, failAfterAccept{}, closed{};
    uint32_t accepted{}, ranJobs{};

    bool IsInitialized() const { return true; }
    bool SupportsParallelTranslation() const { return true; }
    uint32_t GetWorkerCount() const { return kMaxWorkers; }
    bool GetTranslationRecording(uint64_t&, uint64_t&) const { return false; }
    void RequireOwner() const { require(owner == std::this_thread::get_id(), "owner-only native operation moved"); }
    bool OpenWorker(uint32_t target, std::string&)
    {
        RequireOwner();
        opened.values_[opened.size_++] = target;
        encoders[target].target = target;
        return true;
    }
    RHIEncoder& AcquireTranslationEncoder(uint32_t target) { return encoders[target]; }
    bool CloseAll(std::string&)
    {
        RequireOwner();
        require(active.load() == 0 && completed.load() == accepted,
                "failed recording closed before accepted work drained");
        closed = true;
        return true;
    }
    bool DrainEncoderDrops(std::string&) { return false; }
    bool FinalizeRecordedBatch(std::span<const uint32_t> order, const RHIRecordedBatchDesc& desc, RHIRecordedBatch& batch,
                               std::string&)
    {
        RequireOwner();
        require(active.load() == 0 && completed.load() == accepted, "finalize ran before accepted work drained");
        std::copy(order.begin(), order.end(), finalized.values_.begin());
        finalized.size_ = order.size();
        batch.lifetimeToken = desc.lifetimeToken;
        batch.valid = true;
        return true;
    }
    void RunParallel(const ce::owned_func<void(uint32_t)>& callback, uint32_t jobs)
    {
        RequireOwner();
        ranJobs = jobs;
        std::atomic<bool> cancelDelay{};
        const auto invoke = [&](uint32_t job)
        {
#if CE_TRANSLATION_NATIVE_JOBS
            if (native_scheduler && !sequentialCallbacks && !delayFirst && !failAfterAccept)
            {
                require(thread_pool::is_worker_thread(), "multi-job translation did not run on an engine worker");
            }
#endif
            current_job = job + 1;
            if (delayFirst && job == 0)
            {
                while (completed.load() != jobs - 1 && !cancelDelay.load())
                {
                    std::this_thread::yield();
                }
            }
            active.fetch_add(1);
            try
            {
                callback(job);
            }
            catch (...)
            {
                active.fetch_sub(1);
                completed.fetch_add(1);
                current_job = 0;
                throw;
            }
            active.fetch_sub(1);
            completed.fetch_add(1);
            current_job = 0;
        };
#if CE_TRANSLATION_NATIVE_JOBS
        if (native_scheduler && !sequentialCallbacks && !delayFirst && !failAfterAccept)
        {
            accepted = jobs;
            RHIRunCommandRecordingJobs(*native_scheduler, invoke, jobs);
            return;
        }
#endif
        if (sequentialCallbacks)
        {
            for (uint32_t job = 0; job < jobs; ++job)
            {
                ++accepted;
                invoke(job);
            }
            return;
        }
        std::array<std::thread, kMaxWorkers> threads;
        std::array<std::exception_ptr, kMaxWorkers> errors;
        std::exception_ptr admissionError;
        try
        {
            for (uint32_t job = 0; job < jobs; ++job)
            {
                threads[job] = std::thread(
                    [&, job]
                    {
                        try
                        {
                            invoke(job);
                        }
                        catch (...)
                        {
                            errors[job] = std::current_exception();
                        }
                    });
                ++accepted;
                if (failAfterAccept)
                {
                    throw std::runtime_error("fixture admission failure");
                }
            }
        }
        catch (...)
        {
            admissionError = std::current_exception();
            cancelDelay.store(true);
        }
        for (auto& thread : threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        if (admissionError)
        {
            std::rethrow_exception(admissionError);
        }
        for (const auto& error : errors)
        {
            if (error)
            {
                std::rethrow_exception(error);
            }
        }
    }
};
struct RHICommandTranslator
{
    IRHIParallelCommandPool& m_pool;
    IRenderDeviceServices& m_services;
    RHIQueueIdentity m_queue;
    bool Translate(RHISoftwareBatch&&, const RHICommandTranslationOptions&, RHITranslatedBatch&, RHICommandFailure&);
};

#include "RHICommandTranslatorProduction.inl"

namespace
{
    struct fixture
    {
        ReplayState replay_;
        std::array<std::array<RHISoftwareCommandList, 3>, 8> segments_{};
        RHISoftwareRecordingStorage storage_;
        RHISoftwareBatch input_;
        IRHIParallelCommandPool pool_;
        IRenderDeviceServices services_;
        RHICommandTranslator translator_{pool_, services_, input_.m_desc.queue};
        RHITranslatedBatch output_;
        RHICommandFailure failure_;

        fixture()
        {
            storage_.targetOrder.values_ = {5, 1, 7, 0, 6, 3, 4, 2};
            storage_.targetOrder.size_ = 8;
            const std::array<uint64_t, 8> costs{3, 20, 0, 20, 8, 1, 30, 2};
            for (std::size_t index = 0; index < costs.size(); ++index)
            {
                const auto target = storage_.targetOrder.values_[index];
                for (uint32_t segment = 0; segment < 3; ++segment)
                {
                    // 비활성 잔류 segment의 큰 비용을 실수로 읽으면 우선순위 검증이 실패한다.
                    segments_[target][segment] = {&replay_,
                                                  segment == 0   ? costs[index]
                                                  : segment == 1 ? 0
                                                                 : UINT64_MAX,
                                                  target, segment};
                    storage_.targets[target].segments[segment] = &segments_[target][segment];
                }
                storage_.targets[target].activeSegments = 2;
                input_.m_stats.commandCount += costs[index];
            }
            input_.m_storage = &storage_;
        }

        bool translate(uint32_t jobs, bool serial = false, uint64_t minimum = 0)
        {
            const RHICommandTranslationOptions options{
                serial ? RHICommandTranslationMode::Serial : RHICommandTranslationMode::Parallel, jobs, minimum};
            const bool previousBackendScope = RHIBackendExecutionScope::IsActive();
            const bool accepted = translator_.Translate(std::move(input_), options, output_, failure_);
            require(RHIBackendExecutionScope::IsActive() == previousBackendScope,
                    "translation leaked its backend scope on success or failure");
            return accepted;
        }

        void check_order_and_completion(bool success) const
        {
            require(pool_.opened.values_ == storage_.targetOrder.values_,
                    "native targets opened out of original order");
            require(replay_.replayCount.load() == 8, "a target was skipped or replayed more than once");
            require(pool_.active.load() == 0 && pool_.completed.load() == pool_.accepted,
                    "Translate returned with an accepted callback still live");
            if (success)
            {
                require(pool_.finalized.values_ == storage_.targetOrder.values_, "native submission order changed");
                require(output_.m_stats.commandCount == input_.m_stats.commandCount, "command aggregation changed");
                const auto& provenance = output_.m_provenance;
                require(provenance.recordingId == 3 && provenance.queue == input_.m_desc.queue &&
                            provenance.frameId == 17 && provenance.sourceEngineFrame == 41 &&
                            provenance.sourceEngineFrameAvailable && provenance.renderViewId == 43 &&
                            provenance.backendGeneration == 29 && provenance.targetIndex == UINT32_MAX,
                        "translated provenance lost its production source/queue identity");
                for (const auto& count : replay_.nextSegment)
                {
                    require(count.load() == 2, "active segment coverage changed");
                }
            }
            else
            {
                require(pool_.closed && !output_.IsValid() && pool_.finalized.size_ == 0,
                        "failed translation published a native batch");
            }
        }
    };

    void exercise_queue()
    {
        const std::array<uint64_t, 8> costs{0, UINT64_MAX, 3, UINT64_MAX, 7, 1, 7, 0};
        const std::array<std::size_t, 8> expected{1, 3, 4, 6, 2, 5, 0, 7};
        for (std::size_t count = 0; count <= costs.size(); ++count)
        {
            translation_work_queue<8> queue(std::span(costs.data(), count));
            for (const auto index : expected)
            {
                if (index < count)
                {
                    require(queue.claim() == index, "cost ordering or original-index tie break changed");
                }
            }
            for (unsigned attempt = 0; attempt < 8; ++attempt)
            {
                require(queue.claim() == count, "exhausted queue returned an index");
            }
        }
        bool rejected{};
        try
        {
            translation_work_queue<7> oversized(costs);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "fixed queue accepted too many targets");
        allocations.store(0);
        count_allocations.store(true);
        std::size_t checksum{};
        for (unsigned iteration = 0; iteration < 10000; ++iteration)
        {
            translation_work_queue<8> queue(costs);
            for (auto index = queue.claim(); index < costs.size(); index = queue.claim())
            {
                checksum += index;
            }
        }
        count_allocations.store(false);
        require(allocations.load() == 0 && checksum == 280000, "fixed work queue allocated or lost claims");
        std::cout << "PASS fixed queue: 0..8 targets, stable ties, zero/full costs, exhaustion, capacity, "
                     "10000 repetitions with zero dynamic allocations; bytes="
                  << sizeof(translation_work_queue<8>) << '\n';
    }

    void exercise_translation()
    {
        {
            fixture test;
            test.storage_.targetOrder.size_ = 9;
            require(!test.translate(8) && test.pool_.opened.size_ == 0,
                    "oversized sealed target order reached fixed translation storage");
        }
        for (uint32_t jobs : {1u, 2u, 4u, 8u})
        {
            for (unsigned repeat = 0; repeat < 16; ++repeat)
            {
                fixture test;
                require(test.translate(jobs), "parallel translation failed");
                test.check_order_and_completion(true);
                require(test.output_.m_stats.translationJobCount == jobs, "job admission changed");
                require(test.replay_.costReads.load() == (jobs > 1 ? 16u : 0u),
                        "cost scan included inactive segments or ran in the one-job path");
            }
        }
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            fixture test;
            test.replay_.requireInline = true;
            require(test.translate(mode == 2 ? 8 : 1, mode == 0, mode == 2 ? UINT64_MAX : 0),
                    "serial or command-budget fallback failed");
            test.check_order_and_completion(true);
            require(test.replay_.replayOrder == test.storage_.targetOrder.values_ &&
                        test.replay_.costReads.load() == 0 && test.pool_.ranJobs == 0,
                    "serial route reordered targets, scanned costs or submitted jobs");
        }
        {
            fixture test;
            test.storage_.targetOrder.size_ = 1;
            test.replay_.requireInline = true;
            test.input_.m_stats.commandCount = test.segments_[5][0].cost;
            require(test.translate(8), "single-target translation failed");
            require(test.output_.m_stats.translationJobCount == 1 && test.pool_.ranJobs == 0 &&
                        test.replay_.costReads.load() == 0 && test.replay_.replayCount.load() == 1 &&
                        test.replay_.nextSegment[5].load() == 2 && test.pool_.finalized.size_ == 1 &&
                        test.pool_.finalized.values_[0] == 5,
                    "single target admitted a job or lost its original target identity");
        }
        for (const uint32_t jobs : {1u, 8u})
        {
            for (const bool failure : {false, true})
            {
                fixture test;
                auto owner = own::make_shared<int>(17);
                const own::weak_owner<int> lifetime = owner;
                test.input_.m_desc.lifetimeToken = std::move(owner);
                test.replay_.requireInline = jobs == 1;
                test.segments_[1][1].raise = failure;
                require(test.translate(jobs) != failure, "lifetime fixture translation outcome changed");
                test.check_order_and_completion(!failure);
                require(lifetime.expired() == failure,
                        "translation released successful native resources or retained a failed recording");
                test.output_ = {};
                require(lifetime.expired(), "retired CPU output retained its recording resources");
            }
        }
        {
            fixture test;
            test.pool_.sequentialCallbacks = true;
            require(test.translate(4), "ordered callback fixture failed");
            const std::array<uint32_t, 8> expected{4, 1, 0, 6, 5, 2, 3, 7};
            require(test.replay_.replayOrder == expected, "production translation did not consume weighted claims");
            test.check_order_and_completion(true);
        }
        {
            fixture test;
            test.pool_.delayFirst = true;
            require(test.translate(4), "delayed worker translation failed");
            require(test.replay_.visitsByJob[0].load() == 0, "delayed worker retained statically assigned targets");
            test.check_order_and_completion(true);
        }
        {
            fixture test;
            test.pool_.sequentialCallbacks = true;
            test.segments_[5][0].cost = UINT64_MAX - 4;
            test.segments_[5][1].cost = 10;
            test.segments_[1][0].cost = UINT64_MAX - 1;
            test.input_.m_stats.commandCount = 0;
            for (const auto& target : test.storage_.targets)
            {
                for (std::size_t segment = 0; segment < target.activeSegments; ++segment)
                {
                    test.input_.m_stats.commandCount += target.segments[segment]->cost;
                }
            }
            require(test.translate(4), "saturated scheduling cost translation failed");
            require(test.replay_.replayOrder[0] == 5 && test.replay_.replayOrder[1] == 1,
                    "target command-cost sum wrapped and reversed priority");
            test.check_order_and_completion(true);
        }
        {
            fixture test;
            test.pool_.sequentialCallbacks = true;
            test.segments_[1][1].fail = true;
            test.segments_[4][0].fail = true;
            require(!test.translate(4), "target failures were discarded");
            require(test.failure_.targetIndex == 1 && test.failure_.commandIndex == 21 &&
                        test.failure_.commandType == RHISoftwareCommandType::Dispatch,
                    "failure selection followed execution order or lost the segment command offset");
            test.check_order_and_completion(false);
        }
        {
            fixture test;
            test.segments_[1][1].raise = true;
            require(!test.translate(8), "target exception was discarded");
            require(test.failure_.targetIndex == 1 && test.failure_.message == "fixture replay exception",
                    "target exception attribution changed");
            test.check_order_and_completion(false);
        }
        {
            fixture test;
            test.pool_.failAfterAccept = true;
            require(!test.translate(4), "admission exception was discarded");
            require(test.failure_.message == "fixture admission failure", "admission exception attribution changed");
            test.check_order_and_completion(false);
        }
        std::cout
            << "PASS exact Translate body: 1/2/4/8 jobs, exactly-once segments, exclusive target owner, "
               "original open/finalize/error order, active-only/saturated costs, serial fallback, delayed participant, "
               "replay/admission failure drain, owner inline/worker execution, backend scope restore, "
               "production provenance, output resource lifetime\n";
    }
} // namespace

int main()
{
    try
    {
        exercise_queue();
#if CE_TRANSLATION_NATIVE_JOBS
        thread_pool pool;
        job_scheduler scheduler(pool);
        for (std::size_t workers : {1u, 4u, 8u})
        {
            scheduler.start(workers);
            native_scheduler = &scheduler;
            exercise_translation();
            native_scheduler = nullptr;
            scheduler.shutdown();
            std::cout << "PASS actual RHIRunCommandRecordingJobs + JobScheduler + ThreadPool, enkiTS workers="
                      << workers << '\n';
        }
        std::cout << "BOUNDARY native RHI, driver cost and engine frame speed are not measured; "
                     "forced delay/admission fixtures use joined std::threads\n";
#else
        exercise_translation();
        std::cout << "BOUNDARY native RHI, driver cost, enkiTS scheduling and engine frame speed are not measured\n";
#endif
        return 0;
    }
    catch (const std::exception& error)
    {
        count_allocations.store(false);
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
