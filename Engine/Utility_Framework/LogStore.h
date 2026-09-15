#pragma once
#include "LogEntry.h"
#include "LogGroup.h"
#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct LogStoreLimits
{
    // Occurrence history keeps the existing UI length. Groups and their text
    // are budgeted separately so a message repeated thousands of times cannot
    // push every other message out of the console.
    std::size_t maxOccurrences{ 500 };
    std::size_t maxGroups{ 500 };
    std::size_t maxTextBytes{ 8 * 1024 * 1024 };
};

// Per-level running totals since the last Clear. These count what was
// appended, not what is still retained: a message evicted from the occurrence
// buffer still happened. Readers that want "how many are still in the list"
// read the groups instead.
//
// The buckets are the three a reader acts on, not the six spdlog levels.
// Trace/debug/info all mean "the program is telling you something"; err and
// critical both mean "this is broken".
struct LogLevelTotals
{
    std::uint64_t messages{};  // trace, debug, info
    std::uint64_t warnings{};  // warn
    std::uint64_t errors{};    // err, critical

    std::uint64_t Total() const { return messages + warnings + errors; }
    bool operator==(const LogLevelTotals&) const = default;
};

// Which bucket a level falls in. One definition, so the window, the status bar
// and the gate cannot disagree about what "errors" counts.
inline void AddToLevelTotals(LogLevelTotals& totals, spdlog::level::level_enum level)
{
    if (level == spdlog::level::warn) ++totals.warnings;
    else if (level == spdlog::level::err || level == spdlog::level::critical) ++totals.errors;
    else if (level != spdlog::level::off) ++totals.messages;
}

struct LogSnapshot
{
    // Occurrence order, payload restored from the owning group. This is the
    // shape the current log window consumes; the grouped view below is what
    // a collapsed list will read.
    std::vector<LogEntry> entries;
    std::vector<LogGroup> groups; // Ascending groupId.
    std::uint64_t revision{};
    std::uint64_t clearGeneration{};
    std::uint64_t evictedEntries{};
    std::uint64_t rejectedEntries{};
    std::uint64_t evictedGroups{};
    // Sum over groups, so repeats of one message are counted once.
    std::size_t textBytes{};
    LogLevelTotals levelTotals;
};

// What a reader hands back to ask "what changed since I last looked".
struct LogCursor
{
    std::uint64_t revision{};
    std::uint64_t clearGeneration{};
    std::uint64_t lastSequence{};
};

struct LogDelta
{
    // True when the payload is the complete retained state: the reader must
    // replace what it holds rather than merge it. Set for a reader with no
    // state, after Clear, and whenever removals have aged out of the log.
    bool resynchronized{};
    std::vector<LogGroup> changedGroups; // Ascending groupId.
    std::vector<std::uint64_t> removedGroups;
    std::vector<LogOccurrence> appendedOccurrences;
    // Occurrences before this sequence are gone; a merging reader drops them.
    std::uint64_t oldestRetainedSequence{};
    LogCursor cursor; // Hand this back on the next call.
    std::uint64_t evictedEntries{};
    std::uint64_t rejectedEntries{};
    std::uint64_t evictedGroups{};
    std::size_t textBytes{};
    LogLevelTotals levelTotals;
};

// The sole mutable owner of console history. No borrowed entries or callbacks
// escape the lock; a returned snapshot or delta belongs to its reader.
//
// Invariant: every retained occurrence's group is present. A group is only
// removed once it holds no retained occurrence, which is also what keeps
// removal off the occurrence history's back.
class LogStore
{
public:
    explicit LogStore(LogStoreLimits limits = {}) : m_limits(limits)
    {
        if (limits.maxOccurrences == 0 || limits.maxGroups == 0 || limits.maxTextBytes == 0)
            throw std::invalid_argument("LogStore limits must be positive");
    }

    bool Append(LogEntry entry)
    {
        const std::size_t entryText = EntryTextBytes(entry);

        std::lock_guard lock(m_mutex);
        ++m_revision;
        if (entryText > m_limits.maxTextBytes)
        {
            // Reject the complete UI record, never silently truncate its
            // payload. Other spdlog sinks still receive the original message.
            ++m_nextSequence;
            ++m_rejectedEntries;
            AddToLevelTotals(m_levelTotals, entry.level);
            return false;
        }

        const std::size_t hash = KeyHash(entry.level, entry.loggerName, entry.message, entry.source);
        std::uint64_t groupId = FindGroupLocked(hash, entry);
        if (groupId == 0)
        {
            ReleaseUntilLocked([&] { return m_textBytes + entryText <= m_limits.maxTextBytes; });
            groupId = m_nextGroupId++;
            LogGroup group;
            group.groupId = groupId;
            group.level = entry.level;
            group.loggerName = std::move(entry.loggerName);
            group.message = std::move(entry.message);
            group.source = std::move(entry.source);
            group.firstTimestamp = entry.timestamp;
            m_groups.emplace(groupId, std::move(group));
            m_groupsByHash[hash].push_back(groupId);
            m_textBytes += entryText;
        }

        AddToLevelTotals(m_levelTotals, entry.level);

        LogGroup& group = m_groups.at(groupId);
        const std::uint64_t sequence = m_nextSequence++;
        ++group.totalCount;
        ++group.retainedOccurrences;
        group.lastTimestamp = entry.timestamp;
        group.lastSequence = sequence;
        group.revision = m_revision;
        m_occurrences.push_back(LogOccurrence{ sequence, groupId, entry.timestamp, entry.threadId });

        while (m_occurrences.size() > m_limits.maxOccurrences) DropOldestOccurrenceLocked();
        ReleaseUntilLocked([&] { return m_groups.size() <= m_limits.maxGroups; });
        return true;
    }

    void Clear()
    {
        std::lock_guard lock(m_mutex);
        m_occurrences.clear();
        m_groups.clear();
        m_groupsByHash.clear();
        m_idleGroups.clear();
        m_idleMarks.clear();
        m_removedGroups.clear();
        m_droppedRemovalRevision = 0;
        m_textBytes = 0;
        m_evictedEntries = 0;
        m_rejectedEntries = 0;
        m_evictedGroups = 0;
        m_levelTotals = {};
        ++m_clearGeneration;
        ++m_revision;
        // Sequence and group ids stay monotonic across Clear, even when a
        // reader retains an older snapshot. Clear and Append share this lock.
    }

    LogLevelTotals ReadLevelTotals() const
    {
        std::lock_guard lock(m_mutex);
        return m_levelTotals;
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

    // Returns nothing when the reader is already current. Otherwise the reader
    // applies the payload and keeps delta.cursor for its next call.
    std::optional<LogDelta> ReadDeltaSince(LogCursor cursor) const
    {
        std::lock_guard lock(m_mutex);
        if (cursor.clearGeneration == m_clearGeneration && cursor.revision == m_revision)
            return std::nullopt;

        LogDelta delta;
        delta.resynchronized =
            cursor.revision == 0 ||                          // reader holds nothing
            cursor.clearGeneration != m_clearGeneration ||   // history was cleared
            cursor.revision > m_revision ||                  // cursor is not ours
            cursor.revision < m_droppedRemovalRevision;      // removals aged out

        for (const auto& [groupId, group] : m_groups)
        {
            if (delta.resynchronized || group.revision > cursor.revision)
                delta.changedGroups.push_back(group);
        }
        // Map order is unspecified; give readers a stable order to apply.
        std::sort(delta.changedGroups.begin(), delta.changedGroups.end(),
            [](const LogGroup& left, const LogGroup& right) { return left.groupId < right.groupId; });

        if (!delta.resynchronized)
        {
            for (const auto& removed : m_removedGroups)
            {
                if (removed.revision > cursor.revision) delta.removedGroups.push_back(removed.groupId);
            }
        }

        const auto first = delta.resynchronized
            ? m_occurrences.begin()
            : std::upper_bound(m_occurrences.begin(), m_occurrences.end(), cursor.lastSequence,
                [](std::uint64_t sequence, const LogOccurrence& occurrence)
                { return sequence < occurrence.sequence; });
        delta.appendedOccurrences.assign(first, m_occurrences.end());

        delta.oldestRetainedSequence = m_occurrences.empty() ? 0 : m_occurrences.front().sequence;
        delta.cursor = LogCursor{ m_revision, m_clearGeneration,
            m_occurrences.empty() ? cursor.lastSequence : m_occurrences.back().sequence };
        delta.evictedEntries = m_evictedEntries;
        delta.rejectedEntries = m_rejectedEntries;
        delta.levelTotals = m_levelTotals;
        delta.evictedGroups = m_evictedGroups;
        delta.textBytes = m_textBytes;
        return delta;
    }

private:
    struct RemovedGroup
    {
        std::uint64_t revision{};
        std::uint64_t groupId{};
    };

    static std::size_t EntryTextBytes(const LogEntry& entry)
    {
        return entry.message.size() + entry.loggerName.size() +
            entry.source.file.size() + entry.source.function.size();
    }

    static std::size_t GroupTextBytes(const LogGroup& group)
    {
        return group.message.size() + group.loggerName.size() +
            group.source.file.size() + group.source.function.size();
    }

    static std::size_t KeyHash(spdlog::level::level_enum level, const std::string& loggerName,
        const std::string& message, const LogSourceLocation& source)
    {
        std::size_t seed = std::hash<int>{}(static_cast<int>(level));
        for (const auto* text : { &loggerName, &message, &source.file, &source.function })
        {
            // std::hash<std::string> reads the whole size, so an embedded NUL
            // does not cut the payload short the way a C string would.
            seed ^= std::hash<std::string>{}(*text) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }
        seed ^= std::hash<int>{}(source.line) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }

    // A matching hash is only a candidate. The key itself decides, so a
    // collision merges nothing.
    std::uint64_t FindGroupLocked(std::size_t hash, const LogEntry& entry) const
    {
        const auto bucket = m_groupsByHash.find(hash);
        if (bucket == m_groupsByHash.end()) return 0;
        for (const std::uint64_t groupId : bucket->second)
        {
            const LogGroup& group = m_groups.at(groupId);
            if (group.level == entry.level && group.message == entry.message &&
                group.loggerName == entry.loggerName && group.source.line == entry.source.line &&
                group.source.file == entry.source.file && group.source.function == entry.source.function)
                return groupId;
        }
        return 0;
    }

    void MarkIdleLocked(std::uint64_t groupId)
    {
        if (m_idleMarks.insert(groupId).second) m_idleGroups.push_back(groupId);
    }

    void DropOldestOccurrenceLocked()
    {
        const LogOccurrence& front = m_occurrences.front();
        LogGroup& group = m_groups.at(front.groupId);
        // The total count deliberately stays: that is what lets a long-running
        // repeat keep reporting how often it happened.
        --group.retainedOccurrences;
        group.revision = m_revision;
        if (group.retainedOccurrences == 0) MarkIdleLocked(group.groupId);
        m_occurrences.pop_front();
        ++m_evictedEntries;
    }

    // Removes one group that no retained occurrence points at. Returns false
    // when every surviving group is still referenced.
    bool RemoveIdleGroupLocked()
    {
        while (!m_idleGroups.empty())
        {
            const std::uint64_t groupId = m_idleGroups.front();
            m_idleGroups.pop_front();
            m_idleMarks.erase(groupId);
            const auto it = m_groups.find(groupId);
            if (it == m_groups.end()) continue;            // already removed
            if (it->second.retainedOccurrences != 0) continue; // logged again since

            const LogGroup& group = it->second;
            const std::size_t hash = KeyHash(group.level, group.loggerName, group.message, group.source);
            if (const auto bucket = m_groupsByHash.find(hash); bucket != m_groupsByHash.end())
            {
                auto& ids = bucket->second;
                ids.erase(std::remove(ids.begin(), ids.end(), groupId), ids.end());
                if (ids.empty()) m_groupsByHash.erase(bucket);
            }
            m_textBytes -= GroupTextBytes(group);
            m_groups.erase(it);
            ++m_evictedGroups;

            m_removedGroups.push_back(RemovedGroup{ m_revision, groupId });
            while (m_removedGroups.size() > m_limits.maxGroups)
            {
                // A cursor older than a dropped record can no longer be told
                // what vanished, so ReadDeltaSince resynchronises it instead.
                m_droppedRemovalRevision = m_removedGroups.front().revision;
                m_removedGroups.pop_front();
            }
            return true;
        }
        return false;
    }

    // Frees groups until the predicate holds, giving up only when nothing is
    // left to release. Occurrences are dropped only to make a group idle.
    template <typename Predicate>
    void ReleaseUntilLocked(Predicate satisfied)
    {
        while (!satisfied())
        {
            if (RemoveIdleGroupLocked()) continue;
            if (m_occurrences.empty()) break;
            DropOldestOccurrenceLocked();
        }
    }

    LogSnapshot SnapshotLocked() const
    {
        LogSnapshot snapshot;
        snapshot.entries.reserve(m_occurrences.size());
        for (const auto& occurrence : m_occurrences)
        {
            const LogGroup& group = m_groups.at(occurrence.groupId);
            LogEntry entry;
            entry.sequence = occurrence.sequence;
            entry.groupId = occurrence.groupId;
            entry.level = group.level;
            entry.timestamp = occurrence.timestamp;
            entry.threadId = occurrence.threadId;
            entry.loggerName = group.loggerName;
            entry.message = group.message;
            entry.source = group.source;
            snapshot.entries.push_back(std::move(entry));
        }
        snapshot.groups.reserve(m_groups.size());
        for (const auto& [groupId, group] : m_groups) snapshot.groups.push_back(group);
        std::sort(snapshot.groups.begin(), snapshot.groups.end(),
            [](const LogGroup& left, const LogGroup& right) { return left.groupId < right.groupId; });

        snapshot.revision = m_revision;
        snapshot.clearGeneration = m_clearGeneration;
        snapshot.evictedEntries = m_evictedEntries;
        snapshot.rejectedEntries = m_rejectedEntries;
        snapshot.levelTotals = m_levelTotals;
        snapshot.evictedGroups = m_evictedGroups;
        snapshot.textBytes = m_textBytes;
        return snapshot;
    }

    const LogStoreLimits m_limits;
    mutable std::mutex m_mutex;
    std::deque<LogOccurrence> m_occurrences;
    std::unordered_map<std::uint64_t, LogGroup> m_groups;
    std::unordered_map<std::size_t, std::vector<std::uint64_t>> m_groupsByHash;
    // Groups holding no retained occurrence, oldest first. Entries are checked
    // when popped, so a group logged again is skipped instead of removed.
    std::deque<std::uint64_t> m_idleGroups;
    std::unordered_set<std::uint64_t> m_idleMarks;
    std::deque<RemovedGroup> m_removedGroups;
    std::size_t m_textBytes{};
    std::uint64_t m_nextSequence{ 1 };
    std::uint64_t m_nextGroupId{ 1 };
    std::uint64_t m_revision{ 1 };
    std::uint64_t m_clearGeneration{};
    std::uint64_t m_evictedEntries{};
    std::uint64_t m_rejectedEntries{};
    LogLevelTotals m_levelTotals;
    std::uint64_t m_evictedGroups{};
    std::uint64_t m_droppedRemovalRevision{};
};
