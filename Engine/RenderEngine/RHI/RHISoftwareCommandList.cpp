#include "RHISoftwareCommandList.h"

RHISoftwareCommandArena::~RHISoftwareCommandArena() = default;

RHISoftwareCommandArena::RHISoftwareCommandArena(RHISoftwareCommandArena&& other) noexcept
    : m_storage(std::move(other.m_storage)), m_resource(std::exchange(other.m_resource, nullptr)),
      m_cursor(std::exchange(other.m_cursor, nullptr)), m_end(std::exchange(other.m_end, nullptr))
{
}

RHISoftwareCommandArena& RHISoftwareCommandArena::operator=(RHISoftwareCommandArena&& other) noexcept
{
    if (this != &other)
    {
        Clear();
        m_storage = std::move(other.m_storage);
        m_resource = std::exchange(other.m_resource, nullptr);
        m_cursor = std::exchange(other.m_cursor, nullptr);
        m_end = std::exchange(other.m_end, nullptr);
    }
    return *this;
}

void RHISoftwareCommandArena::Clear() noexcept
{
    m_resource = nullptr;
    m_cursor = nullptr;
    m_end = nullptr;
    m_storage.reset();
}

void* RHISoftwareCommandArena::Allocate(size_t bytes, size_t alignment, RHICommandRecordingStats& stats)
{
    if (!bytes || !alignment || alignment > alignof(std::max_align_t) || (alignment & (alignment - 1)))
    {
        throw std::bad_alloc();
    }
    if (m_cursor)
    {
        const size_t padding = (uintptr_t{} - reinterpret_cast<uintptr_t>(m_cursor)) & (alignment - 1);
        const size_t available = static_cast<size_t>(m_end - m_cursor);
        if (padding <= available && bytes <= available - padding)
        {
            auto* memory = m_cursor + padding;
            m_cursor = memory + bytes;
            return memory;
        }
    }
    if (!m_storage)
    {
        static const ce::RenderArenaPool pool;
        m_storage = pool.Acquire();
        m_resource = &m_storage->Resource(0);
    }
    const auto& state = m_resource->stats();
    const size_t activeCapacity = state.activeCapacity;
    const size_t activeBlocks = state.activeBlocks;
    const size_t blockCount = state.blockCount;
    // Account and activate retained storage once per chunk, not per command.
    // Payload/command byte budgets remain exact in the owning command lists.
    const size_t capacity = (std::max)(size_t{65536}, bytes);
    auto* memory = static_cast<std::byte*>(m_resource->allocate(capacity, alignof(std::max_align_t)));
    m_cursor = memory + bytes;
    m_end = memory + capacity;
    stats.allocatedBytes += state.activeCapacity - activeCapacity;
    stats.arenaBlocks += state.activeBlocks - activeBlocks;
    stats.arenaCacheHits += (state.activeBlocks - activeBlocks) - (state.blockCount - blockCount);
    return memory;
}

RHISoftwareCommandList::RHISoftwareCommandList(uint64_t maxCommandBytes, uint64_t maxPayloadBytes,
                                               std::shared_ptr<RHISoftwareRecordingBudget> budget)
    : m_budget(std::move(budget)), m_maxCommandBytes(maxCommandBytes), m_maxPayloadBytes(maxPayloadBytes)
{
    if (!maxCommandBytes || !maxPayloadBytes)
    {
        Fail("Software command and payload budgets must be positive.");
    }
}

RHISoftwareCommandList::~RHISoftwareCommandList()
{
    Clear();
}

RHISoftwareCommandList::RHISoftwareCommandList(RHISoftwareCommandList&& other) noexcept
{
    MoveFrom(std::move(other));
}

RHISoftwareCommandList& RHISoftwareCommandList::operator=(RHISoftwareCommandList&& other) noexcept
{
    if (this != &other)
    {
        Clear();
        MoveFrom(std::move(other));
    }
    return *this;
}

void RHISoftwareCommandList::MoveFrom(RHISoftwareCommandList&& other) noexcept
{
    m_arena = std::move(other.m_arena);
    m_budget = std::move(other.m_budget);
    m_head = std::exchange(other.m_head, nullptr);
    m_tail = std::exchange(other.m_tail, nullptr);
    m_maxCommandBytes = other.m_maxCommandBytes;
    m_maxPayloadBytes = other.m_maxPayloadBytes;
    m_stats = std::exchange(other.m_stats, {});
    m_error = std::exchange(other.m_error, {});
    m_sealed = std::exchange(other.m_sealed, false);
    m_consumed = std::exchange(other.m_consumed, true);
}

void RHISoftwareCommandList::Clear() noexcept
{
    m_head = nullptr;
    m_tail = nullptr;
    m_arena.Clear();
    if (m_budget)
    {
        m_budget->commandBytes -= m_stats.commandBytes;
        m_budget->payloadBytes -= m_stats.payloadBytes;
        m_budget.reset();
    }
}

void RHISoftwareCommandList::Fail(const char* message)
{
    if (m_error.empty())
    {
        m_error = message;
    }
}

void* RHISoftwareCommandList::Allocate(size_t bytes, size_t alignment)
{
    if (bytes > SIZE_MAX - alignment)
    {
        Fail("Software arena allocation overflow.");
        return nullptr;
    }
    try
    {
        auto& arena = m_budget ? m_budget->arena : m_arena;
        return arena.Allocate(bytes, alignment, m_stats);
    }
    catch (const std::bad_alloc&)
    {
        Fail("Software arena allocation failed.");
        return nullptr;
    }
}

bool RHISoftwareCommandList::Seal(std::string& outError)
{
    if (!CanWrite())
    {
        outError = m_error.empty() ? "Software command list cannot be sealed twice." : m_error;
        return false;
    }
    m_sealed = true;
    outError.clear();
    return true;
}

bool RHISoftwareCommandList::Replay(RHIEncoder& encoder, RHICommandTranslationStats& outStats,
                                    RHICommandFailure& outFailure)
{
    outStats = {};
    outFailure.Reset();
    outFailure.stage = RHICommandFailureStage::Translation;
    if (!m_sealed || m_consumed || !m_error.empty())
    {
        outFailure.message = "Software command list is not sealed or was already consumed.";
        return false;
    }
    m_consumed = true;
    const uint64_t before = encoder.GetComputeDispatchCount();
    bool success = true;
    for (Command* command = m_head; command; command = command->next)
    {
        outFailure.commandIndex = outStats.commandCount;
        outFailure.commandType = command->type;
        try
        {
            if (!command->replay(encoder, reinterpret_cast<const std::byte*>(command) + command->payloadOffset))
            {
                outFailure.message = "Native encoder rejected a validated software command.";
                success = false;
                break;
            }
        }
        catch (const std::exception& error)
        {
            outFailure.message = error.what();
            success = false;
            break;
        }
        catch (...)
        {
            outFailure.message = "Native command translation threw an unknown exception.";
            success = false;
            break;
        }
        ++outStats.commandCount;
    }
    const uint64_t after = encoder.GetComputeDispatchCount();
    outStats.nativeComputeDispatches =
        before != UINT64_MAX && after != UINT64_MAX && after >= before ? after - before : UINT64_MAX;
    Clear();
    if (success)
    {
        outFailure.Reset();
    }
    return success;
}
