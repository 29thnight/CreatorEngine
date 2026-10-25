#pragma once
#include "../RenderArena.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Arena.h"
#else
import Creator.Core.Arena;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "RHISoftwareCommandTypes.h"
#include <string_view>

class RHISoftwareEncoder;

// A target owns one arena across all pass segments. Only its recording writer
// allocates; the last translated/cancelled segment returns the blocks.
class RHISoftwareCommandArena final
{
  public:
    RHISoftwareCommandArena() = default;
    ~RHISoftwareCommandArena();
    RHISoftwareCommandArena(const RHISoftwareCommandArena&) = delete;
    RHISoftwareCommandArena& operator=(const RHISoftwareCommandArena&) = delete;
    RHISoftwareCommandArena(RHISoftwareCommandArena&& other) noexcept;
    RHISoftwareCommandArena& operator=(RHISoftwareCommandArena&& other) noexcept;
    void* Allocate(size_t bytes, size_t alignment, RHICommandRecordingStats& stats);
    void Clear() noexcept;

  private:
    ce::RenderArenaLease m_storage;
    ce::render_arena_resource* m_resource{};
    std::byte* m_cursor{};
    std::byte* m_end{};
};

// Shared only by successive pass segments of one exclusively owned target.
struct RHISoftwareRecordingBudget final
{
    RHISoftwareCommandArena arena;
    uint64_t maxCommandBytes{};
    uint64_t maxPayloadBytes{};
    uint64_t commandBytes{};
    uint64_t payloadBytes{};

    static bool CanReserve(uint64_t used, uint64_t limit, uint64_t bytes)
    {
        return used <= limit && bytes <= limit - used;
    }

    static bool TryReserve(uint64_t& used, uint64_t limit, uint64_t bytes)
    {
        if (!CanReserve(used, limit, bytes))
        {
            return false;
        }
        used += bytes;
        return true;
    }
};
// One writer owns a list. Chunks never move; copied spans remain stable even
// when later commands grow the arena. The list is consumed once, including on
// replay failure. GPU resource ownership is a separate batch lifetime contract.
class RHISoftwareCommandList final
{
  public:
    explicit RHISoftwareCommandList(uint64_t maxCommandBytes, uint64_t maxPayloadBytes,
                                    own::shared_owner<RHISoftwareRecordingBudget> budget = {});
    ~RHISoftwareCommandList();
    RHISoftwareCommandList(const RHISoftwareCommandList&) = delete;
    RHISoftwareCommandList& operator=(const RHISoftwareCommandList&) = delete;
    RHISoftwareCommandList(RHISoftwareCommandList&& other) noexcept;
    RHISoftwareCommandList& operator=(RHISoftwareCommandList&& other) noexcept;

    uint64_t GetCommandCount() const { return m_stats.commandCount; }
    uint64_t GetCommandBytes() const { return m_stats.commandBytes; }
    uint64_t GetPayloadBytes() const { return m_stats.payloadBytes; }
    bool IsSealed() const { return m_sealed; }
    const RHICommandRecordingStats& GetRecordingStats() const { return m_stats; }
    std::string_view GetError() const { return m_error; }
    bool Seal(std::string& outError);
    bool Replay(RHIEncoder& encoder, RHICommandTranslationStats& outStats, RHICommandFailure& outFailure);

  private:
    friend class RHISoftwareEncoder;
    friend struct RHISoftwareRecordingStorage;
    friend class RHISoftwareRecorder;
    void Reset(uint64_t commandBytes, uint64_t payloadBytes, const own::shared_owner<RHISoftwareRecordingBudget>& budget);

    struct Command
    {
        Command* next{};
        bool (*replay)(RHIEncoder&, const void*){};
        RHISoftwareCommandType type{};
        uint8_t payloadOffset{};
    };

    template<typename Payload>
    bool Append(RHISoftwareCommandType type, const Payload& payload, bool (*replay)(RHIEncoder&, const void*),
                bool stickyFailure = true)
    {
        static_assert(alignof(Payload) <= alignof(std::max_align_t));
        static_assert(std::is_trivially_destructible_v<Payload>);
        constexpr size_t payloadOffset = (sizeof(Command) + alignof(Payload) - 1) & ~(alignof(Payload) - 1);
        static_assert(payloadOffset <= UINT8_MAX);
        constexpr size_t bytes = payloadOffset + sizeof(Payload);
        constexpr size_t alignment = (std::max)(alignof(Command), alignof(Payload));
        if (!CanWrite() || bytes > m_maxCommandBytes - m_stats.commandBytes)
        {
            if (stickyFailure)
            {
                Fail("Software command budget exceeded or list is not writable.");
            }
            return false;
        }
        if (m_budget &&
            !RHISoftwareRecordingBudget::TryReserve(m_budget->commandBytes, m_budget->maxCommandBytes, bytes))
        {
            if (stickyFailure)
            {
                Fail("Aggregate software command budget exceeded.");
            }
            return false;
        }
        auto* memory = static_cast<std::byte*>(Allocate(bytes, alignment, stickyFailure));
        if (!memory)
        {
            if (m_budget)
            {
                m_budget->commandBytes -= bytes;
            }
            return false;
        }
        try
        {
            new (memory + payloadOffset) Payload(payload);
            auto* command = new (memory) Command{};
            command->type = type;
            command->payloadOffset = static_cast<uint8_t>(payloadOffset);
            command->replay = replay;
            if (m_tail)
            {
                m_tail->next = command;
            }
            else
            {
                m_head = command;
            }
            m_tail = command;
            ++m_stats.commandCount;
            m_stats.commandBytes += bytes;
            return true;
        }
        catch (...)
        {
            if (m_budget)
            {
                m_budget->commandBytes -= bytes;
            }
            if (stickyFailure)
            {
                Fail("Software command construction failed.");
            }
            return false;
        }
    }

    template<typename Payload>
    bool TryAppend(RHISoftwareCommandType type, const Payload& payload, bool (*replay)(RHIEncoder&, const void*))
    {
        return Append(type, payload, replay, false);
    }

    // Bool commands must leave a usable fallback path. Reserve command and
    // copied span together, then publish both only after one allocation succeeds.
    // No arena rollback, temporary container or per-command shared owner is needed.
    template<typename Payload, typename T>
    bool TryAppendWithSpan(RHISoftwareCommandType type, Payload payload, std::span<const T> source,
                           std::span<const T> Payload::* member, bool (*replay)(RHIEncoder&, const void*))
    {
        static_assert(std::is_trivially_copyable_v<Payload> && std::is_trivially_copyable_v<T>);
        static_assert(alignof(Payload) <= alignof(std::max_align_t) && alignof(T) <= alignof(std::max_align_t));
        constexpr size_t payloadOffset = (sizeof(Command) + alignof(Payload) - 1) & ~(alignof(Payload) - 1);
        static_assert(payloadOffset <= UINT8_MAX);
        constexpr size_t commandBytes = payloadOffset + sizeof(Payload);
        constexpr size_t spanOffset = (commandBytes + alignof(T) - 1) & ~(alignof(T) - 1);
        constexpr size_t alignment = (std::max)({alignof(Command), alignof(Payload), alignof(T)});
        if (!CanWrite() ||
            !RHISoftwareRecordingBudget::CanReserve(m_stats.commandBytes, m_maxCommandBytes, commandBytes) ||
            m_stats.payloadBytes > m_maxPayloadBytes ||
            source.size() > (m_maxPayloadBytes - m_stats.payloadBytes) / sizeof(T) ||
            source.size() > (SIZE_MAX - spanOffset) / sizeof(T))
        {
            return false;
        }
        const size_t payloadBytes = source.size_bytes();
        if (m_budget &&
            (!RHISoftwareRecordingBudget::CanReserve(m_budget->commandBytes, m_budget->maxCommandBytes, commandBytes) ||
             !RHISoftwareRecordingBudget::CanReserve(m_budget->payloadBytes, m_budget->maxPayloadBytes, payloadBytes)))
        {
            return false;
        }
        auto* memory = static_cast<std::byte*>(Allocate(spanOffset + payloadBytes, alignment, false));
        if (!memory)
        {
            return false;
        }
        auto* copied = reinterpret_cast<T*>(memory + spanOffset);
        if (payloadBytes)
        {
            std::memcpy(copied, source.data(), payloadBytes);
        }
        payload.*member = std::span<const T>(copied, source.size());
        new (memory + payloadOffset) Payload(payload);
        auto* command = new (memory) Command{};
        command->type = type;
        command->payloadOffset = static_cast<uint8_t>(payloadOffset);
        command->replay = replay;
        if (m_tail)
        {
            m_tail->next = command;
        }
        else
        {
            m_head = command;
        }
        m_tail = command;
        ++m_stats.commandCount;
        m_stats.commandBytes += commandBytes;
        m_stats.payloadBytes += payloadBytes;
        if (m_budget)
        {
            m_budget->commandBytes += commandBytes;
            m_budget->payloadBytes += payloadBytes;
        }
        return true;
    }

    template<typename T>
    std::span<const T> CopySpan(std::span<const T> source)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(alignof(T) <= alignof(std::max_align_t));
        if (!CanWrite() || source.size() > (m_maxPayloadBytes - m_stats.payloadBytes) / sizeof(T))
        {
            Fail("Software payload budget exceeded or list is not writable.");
            return {};
        }
        if (source.empty())
        {
            return {};
        }
        if (m_budget && !RHISoftwareRecordingBudget::TryReserve(m_budget->payloadBytes, m_budget->maxPayloadBytes,
                                                                source.size_bytes()))
        {
            Fail("Aggregate software payload budget exceeded.");
            return {};
        }
        auto* copy = static_cast<T*>(Allocate(source.size_bytes(), alignof(T)));
        if (!copy)
        {
            if (m_budget)
            {
                m_budget->payloadBytes -= source.size_bytes();
            }
            return {};
        }
        std::memcpy(copy, source.data(), source.size_bytes());
        m_stats.payloadBytes += source.size_bytes();
        return {copy, source.size()};
    }

    bool CanWrite() const { return !m_sealed && !m_consumed && m_error.empty(); }
    void* Allocate(size_t bytes, size_t alignment, bool stickyFailure = true);
    void Fail(const char* message);
    void Clear() noexcept;
    void MoveFrom(RHISoftwareCommandList&& other) noexcept;

    RHISoftwareCommandArena m_arena;
    own::shared_owner<RHISoftwareRecordingBudget> m_budget;
    Command* m_head{};
    Command* m_tail{};
    uint64_t m_maxCommandBytes{};
    uint64_t m_maxPayloadBytes{};
    RHICommandRecordingStats m_stats;
    std::string_view m_error;
    bool m_sealed{};
    bool m_consumed{};
};
