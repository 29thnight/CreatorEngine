// LogEntry.h
#pragma once
#include <spdlog/common.h>
#include <cstddef>
#include <cstdint>
#include <string>

// Own the source strings: spdlog::source_loc only borrows its character buffers.
// A zero line means that the producer did not supply a source location.
struct LogSourceLocation
{
    std::string file;
    std::string function;
    int line{};
};

struct LogEntry
{
    // Assigned by LogStore. Zero is reserved for an empty UI selection.
    std::uint64_t sequence{};
    spdlog::level::level_enum level{};
    spdlog::log_clock::time_point timestamp{};
    std::size_t threadId{};
    std::string loggerName;
    std::string message; // Original payload, including whitespace and embedded NULs.
    LogSourceLocation source;
};
