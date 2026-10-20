#include "RHISoftwareRecorder.h"

void RHISoftwareRecordingStorage::Reset() noexcept
{
    for (auto& target : std::span(targets.data(), targets.size()))
    {
        for (auto& list : std::span(target.segments.data(), target.segments.size()))
        {
            list->Clear();
            list->m_stats = {};
            list->m_error = {};
            list->m_sealed = false;
            list->m_consumed = true;
        }
        target.activeSegments = 0;
    }
    for (auto& budget : std::span(budgets.data(), budgets.size()))
    {
        if (budget)
        {
            budget->arena.Clear();
        }
    }
    for (auto& encoder : std::span(encoders.data(), encoders.size()))
    {
        if (encoder)
        {
            encoder->ReleaseRecording();
        }
    }
    targetOrder.clear();
}

size_t RHISoftwareRecordingStorage::RetainedBytes() const noexcept
{
    size_t bytes =
        targets.capacity() * sizeof(Target) + targetOrder.capacity() * sizeof(uint32_t) +
        budgets.capacity() *
            (sizeof(std::shared_ptr<RHISoftwareRecordingBudget>) + sizeof(RHISoftwareRecordingBudget)) +
        encoders.capacity() * (sizeof(ce::render_unique_ptr<RHISoftwareEncoder>) + sizeof(RHISoftwareEncoder));
    for (const auto& target : std::span(targets.data(), targets.size()))
    {
        bytes += target.segments.capacity() * sizeof(ce::render_unique_ptr<RHISoftwareCommandList>) +
                 target.segments.size() * sizeof(RHISoftwareCommandList);
    }
    return bytes;
}

namespace
{
    struct SoftwareStoragePool
    {
        static constexpr size_t MaxBytes = 16 * 1024 * 1024;
        static_assert(std::atomic<RHISoftwareRecordingStorage*>::is_always_lock_free);
        static_assert(std::atomic<size_t>::is_always_lock_free);
        std::array<std::atomic<RHISoftwareRecordingStorage*>, 8> free{};
        std::atomic<size_t> bytes{};
        ~SoftwareStoragePool()
        {
            for (auto& slot : free)
            {
                ce::render_delete<RHISoftwareRecordingStorage>{}(slot.load(std::memory_order_relaxed));
            }
        }
        void Release(RHISoftwareRecordingStorage* storage) noexcept
        {
            storage->Reset();
            const auto retained = storage->RetainedBytes();
            auto used = bytes.load(std::memory_order_relaxed);
            while (retained <= MaxBytes && used <= MaxBytes - retained)
            {
                if (bytes.compare_exchange_weak(used, used + retained, std::memory_order_relaxed))
                {
                    for (auto& slot : free)
                    {
                        RHISoftwareRecordingStorage* empty{};
                        if (slot.compare_exchange_strong(empty, storage, std::memory_order_release,
                                                         std::memory_order_relaxed))
                        {
                            return;
                        }
                    }
                    bytes.fetch_sub(retained, std::memory_order_relaxed);
                    break;
                }
            }
            ce::render_delete<RHISoftwareRecordingStorage>{}(storage);
        }
    };

    std::shared_ptr<RHISoftwareRecordingStorage> AcquireSoftwareStorage()
    {
        static const auto pool = ce::make_render_shared<SoftwareStoragePool>();
        RHISoftwareRecordingStorage* storage{};
        for (auto& slot : pool->free)
        {
            if (!slot.load(std::memory_order_relaxed))
            {
                continue;
            }
            storage = slot.exchange(nullptr, std::memory_order_acquire);
            if (storage)
            {
                pool->bytes.fetch_sub(storage->RetainedBytes(), std::memory_order_relaxed);
                break;
            }
        }
        if (!storage)
        {
            storage = ce::make_render_unique<RHISoftwareRecordingStorage>().release();
        }
        return std::shared_ptr<RHISoftwareRecordingStorage>(
            storage, [state = pool](auto* value) { state->Release(value); }, ce::render_allocator<std::byte>{});
    }

    std::atomic<uint64_t> g_softwareRecordingId{1};

    void AddSoftwareRecordingStats(RHICommandRecordingStats& total, const RHICommandRecordingStats& next)
    {
        total.commandCount += next.commandCount;
        total.commandBytes += next.commandBytes;
        total.payloadBytes += next.payloadBytes;
        total.allocatedBytes += next.allocatedBytes;
        total.arenaBlocks += next.arenaBlocks;
        total.arenaCacheHits += next.arenaCacheHits;
        total.computeDispatchCommands += next.computeDispatchCommands;
    }
} // namespace

bool RHISoftwareRecorder::BeginRecording(const RHISoftwareRecordingDesc& desc, std::string& outError)
{
    outError.clear();
    if (m_active || !desc.queue.IsValid() || !desc.targetCount || desc.targetCount > 8 ||
        desc.maxCommandBytes < desc.targetCount || desc.maxPayloadBytes < desc.targetCount)
    {
        outError =
            "Software recording requires an inactive session, identified queue, 1..8 targets and positive budgets.";
        return false;
    }
    m_storage = AcquireSoftwareStorage();
    m_storage->targets.resize(desc.targetCount);
    m_storage->encoders.resize(desc.targetCount);
    m_storage->budgets.resize(desc.targetCount);
    for (uint32_t target = 0; target < desc.targetCount; ++target)
    {
        auto& budget = m_storage->budgets[target];
        if (!budget)
        {
            budget = ce::make_render_shared<RHISoftwareRecordingBudget>();
        }
        budget->maxCommandBytes =
            desc.maxCommandBytes / desc.targetCount + (target < desc.maxCommandBytes % desc.targetCount ? 1 : 0);
        budget->maxPayloadBytes =
            desc.maxPayloadBytes / desc.targetCount + (target < desc.maxPayloadBytes % desc.targetCount ? 1 : 0);
    }
    m_desc = desc;
    m_active = true;
    return true;
}

RHIEncoder& RHISoftwareRecorder::AcquireEncoder(uint32_t target)
{
    if (!m_active || target >= m_storage->targets.size())
    {
        throw std::out_of_range("Software encoder target is outside the active session.");
    }
    auto& slot = m_storage->targets[target];
    auto& segments = slot.segments;
    if (slot.activeSegments)
    {
        thread_local std::string error;
        if (!segments[slot.activeSegments - 1]->Seal(error))
        {
            throw std::runtime_error(error);
        }
    }
    const auto index = slot.activeSegments;
    if (index == segments.size())
    {
        segments.push_back(ce::make_render_unique<RHISoftwareCommandList>(
            m_desc.maxCommandBytes, m_desc.maxPayloadBytes, m_storage->budgets[target]));
    }
    else
    {
        segments[index]->Reset(m_desc.maxCommandBytes, m_desc.maxPayloadBytes, m_storage->budgets[target]);
    }
    auto& encoder = m_storage->encoders[target];
    if (!encoder)
    {
        encoder = ce::make_render_unique<RHISoftwareEncoder>(*segments[index], m_validator);
    }
    else
    {
        encoder->Reset(*segments[index], m_validator);
    }
    ++slot.activeSegments;
    return *encoder;
}

RHICommandRecordingStats RHISoftwareRecorder::GetRecordingStats() const
{
    RHICommandRecordingStats total;
    if (!m_storage)
    {
        return total;
    }
    for (const auto& target : std::span(m_storage->targets.data(), m_storage->targets.size()))
    {
        for (const auto& segment : std::span(target.segments.data(), target.activeSegments))
        {
            AddSoftwareRecordingStats(total, segment->GetRecordingStats());
        }
    }
    return total;
}

bool RHISoftwareRecorder::FinishRecording(std::span<const uint32_t> targetOrder, const RHISoftwareBatchDesc& desc,
                                          RHISoftwareBatch& outBatch, std::string& outError)
{
    outError.clear();
    if (!m_active || outBatch.GetState() != RHISoftwareBatchState::Empty || desc.queue != m_desc.queue ||
        !desc.backendGeneration || !desc.lifetimeToken)
    {
        outError = "Software batch output, queue identity, backend generation or lifetime owner is invalid.";
        return false;
    }
    std::bitset<8> seen;
    for (const uint32_t target : targetOrder)
    {
        if (target >= m_storage->targets.size() || seen.test(target) || m_storage->targets[target].activeSegments == 0)
        {
            outError = "Software target order has an invalid, duplicate or unopened target.";
            return false;
        }
        seen.set(target);
    }
    for (size_t target = 0; target < m_storage->targets.size(); ++target)
    {
        for (const auto& segment :
             std::span(m_storage->targets[target].segments.data(), m_storage->targets[target].activeSegments))
        {
            if (segment->GetCommandCount() && !seen.test(target))
            {
                outError = "Software target order omitted recorded commands.";
                return false;
            }
        }
    }
    const auto stats = GetRecordingStats();
    if (stats.commandBytes > m_desc.maxCommandBytes || stats.payloadBytes > m_desc.maxPayloadBytes)
    {
        outError = "Software recording exceeded its aggregate byte budget.";
        return false;
    }
    for (auto& target : std::span(m_storage->targets.data(), m_storage->targets.size()))
    {
        for (auto& segment : std::span(target.segments.data(), target.activeSegments))
        {
            if ((!segment->IsSealed() && !segment->Seal(outError)) || !segment->GetError().empty())
            {
                if (outError.empty())
                {
                    outError = segment->GetError();
                }
                return false;
            }
        }
    }
    RHISoftwareBatch batch;
    m_storage->targetOrder.assign(targetOrder.begin(), targetOrder.end());
    batch.m_desc = desc;
    batch.m_stats = stats;
    batch.m_recordingId = g_softwareRecordingId.fetch_add(1, std::memory_order_relaxed);
    batch.m_storage = std::move(m_storage);
    batch.m_state = RHISoftwareBatchState::Sealed;
    m_active = false;
    outBatch = std::move(batch);
    return true;
}

void RHISoftwareRecorder::CancelRecording()
{
    m_storage.reset();
    m_active = false;
}
