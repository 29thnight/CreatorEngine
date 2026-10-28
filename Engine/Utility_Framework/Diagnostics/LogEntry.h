// LogEntry.h
#pragma once
#include <spdlog/common.h>
#include "../Foundation/StandardLibrary.h"

// Own the source strings: spdlog::source_loc only borrows its character buffers.
// A zero line means that the producer did not supply a source location.
struct LogSourceLocation
{
    ce::utf8_string file;
    ce::utf8_string function;
    int line{};
};

struct LogEntry
{
    // Assigned by LogStore. Zero is reserved for an empty UI selection.
    std::uint64_t sequence{};
    // Assigned by LogStore too: which identity this occurrence belongs to.
    // Producers leave it zero; the store fills it in when it hands a record back.
    std::uint64_t groupId{};
    spdlog::level::level_enum level{};
    spdlog::log_clock::time_point timestamp{};
    std::size_t threadId{};
    ce::utf8_string loggerName;
    ce::utf8_string message; // Original UTF-8 bytes, including whitespace and embedded NULs.
    LogSourceLocation source;
};
