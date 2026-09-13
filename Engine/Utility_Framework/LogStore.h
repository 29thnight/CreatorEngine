#pragma once
#include "LogEntry.h"
#include <cstddef>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

struct LogStoreLimits
{
    // Keep the existing UI history length for the storage migration. The text
    // budget is an initial memory guard, not a measured performance target.
    std::size_t maxEntries{ 500 };
    std::size_t maxTextBytes{ 8 * 1024 * 1024 };
};

struct LogSnapshot
{
    std::vector<LogEntry> entries;
    std::uint64_t revision{};
    std::uint64_t clearGeneration{};
    std::uint64_t evictedEntries{};
    std::uint64_t rejectedEntries{};
    std::size_t textBytes{};
};

// The sole mutable owner of console history. No borrowed entries or callbacks
// escape the lock; a returned snapshot belongs to its reader.
class LogStore
{
public:
    explicit LogStore(LogStoreLimits limits = {}) : m_limits(limits)
    {
        if (limits.maxEntries == 0 || limits.maxTextBytes == 0)
            throw std::invalid_argument("LogStore limits must be positive");
    }

    bool Append(LogEntry entry)
    {
        // Validate with subtraction so a caller-supplied limit cannot overflow.
        std::size_t remaining = m_limits.maxTextBytes;
        bool fits = true;
        for (const auto* text : { &entry.message, &entry.loggerName,
                                 &entry.source.file, &entry.source.function })
        {
            if (text->size() > remaining)
            {
                fits = false;
                break;
            }
            remaining -= text->size();
        }

        std::lock_guard lock(m_mutex);
        if (!fits)
        {
            // Reject the complete UI entry, never silently truncate its payload.
            // Other spdlog sinks still receive the original message.
            ++m_nextSequence;
            ++m_rejectedEntries;
            ++m_revision;
            return false;
        }

        const auto textBytes = m_limits.maxTextBytes - remaining;
        entry.sequence = m_nextSequence;
        // Allocate before evicting old records: allocation failure keeps history.
        m_entries.push_back(std::move(entry));
        ++m_nextSequence;
        while (m_entries.size() > m_limits.maxEntries || m_textBytes > remaining)
        {
            m_textBytes -= TextBytes(m_entries.front());
            m_entries.pop_front();
            ++m_evictedEntries;
        }
        m_textBytes += textBytes;
        ++m_revision;
        return true;
    }

    void Clear()
    {
        std::lock_guard lock(m_mutex);
        m_entries.clear();
        m_textBytes = 0;
        m_evictedEntries = 0;
        m_rejectedEntries = 0;
        ++m_clearGeneration;
        ++m_revision;
        // Keep sequence IDs monotonic across Clear, even when a reader retains
        // an older snapshot. Clear and Append are ordered by this same lock.
    }

    LogSnapshot ReadSnapshot() const
    {
        std::lock_guard lock(m_mutex);
        return SnapshotLocked();
    }

    std::optional<LogSnapshot> ReadSnapshotIfChanged(std::uint64_t revision) const
    {
        std::lock_guard lock(m_mutex);
        if (revision == m_revision) return std::nullopt;
        return SnapshotLocked();
    }

private:
    static std::size_t TextBytes(const LogEntry& entry)
    {
        return entry.message.size() + entry.loggerName.size() +
            entry.source.file.size() + entry.source.function.size();
    }

    LogSnapshot SnapshotLocked() const
    {
        return {
            std::vector<LogEntry>(m_entries.begin(), m_entries.end()),
            m_revision, m_clearGeneration, m_evictedEntries, m_rejectedEntries,
            m_textBytes
        };
    }

    const LogStoreLimits m_limits;
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    std::size_t m_textBytes{};
    std::uint64_t m_nextSequence{ 1 };
    std::uint64_t m_revision{ 1 };
    std::uint64_t m_clearGeneration{};
    std::uint64_t m_evictedEntries{};
    std::uint64_t m_rejectedEntries{};
};
