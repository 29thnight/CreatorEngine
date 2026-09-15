// LogGroup.h
#pragma once
#include "LogEntry.h"
#include <cstddef>
#include <cstdint>
#include <string>

// One log identity. Repeats collapse by identity, never by similarity: the
// payload is compared byte for byte and nothing is normalised away, so
// "HP=10" and "HP=20" stay two groups. Time, thread and sequence describe an
// occurrence, not an identity, and are kept out of the key on purpose --
// putting them in would stop a message repeated across frames from collapsing.
struct LogGroup
{
    // Assigned by LogStore and never reused. Zero means "no group", which is
    // also the empty UI selection. A group that is evicted and then logged
    // again receives a new id so a stale selection cannot latch onto it.
    std::uint64_t groupId{};
    spdlog::level::level_enum level{};
    std::string loggerName;
    std::string message; // Original payload, including whitespace and embedded NULs.
    LogSourceLocation source;

    // Survives occurrence eviction. This is the count a collapsed row shows,
    // so a message repeated far beyond the history length still reports it.
    std::uint64_t totalCount{};
    // How many of those occurrences the store still holds. Always <= totalCount.
    std::uint64_t retainedOccurrences{};
    spdlog::log_clock::time_point firstTimestamp{};
    spdlog::log_clock::time_point lastTimestamp{};
    // Sequence of the newest occurrence. Doubles as the eviction order: the
    // group with the smallest value is the least recently logged one.
    std::uint64_t lastSequence{};
    // Store revision at the last change, so a delta reader can ship the groups
    // that moved past its cursor instead of the whole table.
    std::uint64_t revision{};
};

// One repeat of a group. Carries no strings -- the payload lives in the group,
// which is what keeps a spamming message from copying its text per frame.
struct LogOccurrence
{
    std::uint64_t sequence{};
    std::uint64_t groupId{};
    spdlog::log_clock::time_point timestamp{};
    std::size_t threadId{};
};
