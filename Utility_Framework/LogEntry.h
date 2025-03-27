// LogEntry.h
#pragma once
#include <spdlog/spdlog.h>
#include <string>

struct LogEntry {
    spdlog::level::level_enum level;
    std::string message;
};
