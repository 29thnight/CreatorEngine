#pragma once
#include "LogEntry.h"
#include <spdlog/details/log_msg.h>
#include <spdlog/formatter.h>

namespace editor
{
    // Compatibility presentation for the existing log window. Formatting lives
    // in the Editor; the store only keeps the original fields and payload.
    inline std::string FormatOutputLogEntry(const LogEntry& entry, spdlog::formatter& formatter)
    {
        spdlog::details::log_msg message;
        message.time = entry.timestamp;
        message.level = entry.level;
        message.thread_id = entry.threadId;
        message.logger_name = { entry.loggerName.data(), entry.loggerName.size() };
        message.payload = { entry.message.data(), entry.message.size() };
        message.source = { entry.source.file.c_str(), entry.source.line, entry.source.function.c_str() };
        spdlog::memory_buf_t formatted;
        formatter.format(message, formatted);
        return { formatted.data(), formatted.size() };
    }
}
