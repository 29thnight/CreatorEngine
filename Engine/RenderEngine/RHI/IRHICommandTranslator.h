#pragma once
#include "../../Utility_Framework/Ownership.h"
#include "RHISoftwareBatch.h"
#include "RHICommandProvenance.h"

class IRHIParallelCommandPool;
class IRenderDeviceServices;

enum class RHICommandTranslationMode : uint8_t
{
    Serial,
    Parallel
};

struct RHICommandTranslationOptions
{
    RHICommandTranslationMode mode{RHICommandTranslationMode::Serial};
    uint32_t maxTranslationJobs{1};
    uint64_t minCommandsPerJob{};
};

enum class RHITranslatedBatchKind : uint8_t
{
    Empty,
    ParallelPool,
    OwnedQueue
};

class RHITranslatedBatch final
{
  public:
    RHITranslatedBatch() = default;
    RHITranslatedBatch(const RHITranslatedBatch&) = delete;
    RHITranslatedBatch& operator=(const RHITranslatedBatch&) = delete;
    RHITranslatedBatch(RHITranslatedBatch&&) noexcept = default;
    RHITranslatedBatch& operator=(RHITranslatedBatch&&) noexcept = default;

    RHITranslatedBatchKind GetKind() const
    {
        return m_native.IsValid() ? RHITranslatedBatchKind::ParallelPool
               : m_owned          ? RHITranslatedBatchKind::OwnedQueue
                                  : RHITranslatedBatchKind::Empty;
    }
    bool IsValid() const { return GetKind() != RHITranslatedBatchKind::Empty; }
    const RHICommandTranslationStats& GetTranslationStats() const { return m_stats; }

  private:
    friend class RHICommandTranslator;
    friend class RHIQueueCommandTranslator;
    friend class RHISoftwareCommandQueue;
    friend class RHISubmissionThread;
    RHIRecordedBatch m_native;
    ce::pooled_owner<IRHIQueueCommandBatch> m_owned;
    RHICommandTranslationStats m_stats;
    RHICommandProvenance m_provenance;
};

class IRHICommandTranslator
{
  public:
    virtual ~IRHICommandTranslator() = default;
    virtual bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,
                           RHITranslatedBatch& outBatch, RHICommandFailure& outFailure) = 0;
};

// Replay uses independent targets from a frozen recording for parallel jobs. Legacy
// synchronous callers must retain the current upload/descriptor recording.
// Queue admission advances only the producer; frozen pools retain their leases.
class RHICommandTranslator : public IRHICommandTranslator
{
  public:
    RHICommandTranslator(IRHIParallelCommandPool& pool, IRenderDeviceServices& services, RHIQueueIdentity queue)
        : m_pool(pool), m_services(services), m_queue(queue)
    {
    }
    bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,
                   RHITranslatedBatch& outBatch, RHICommandFailure& outFailure) override;

  private:
    IRHIParallelCommandPool& m_pool;
    IRenderDeviceServices& m_services;
    RHIQueueIdentity m_queue;
};
