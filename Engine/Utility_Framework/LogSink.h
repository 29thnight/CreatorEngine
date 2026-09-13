#pragma once
#include "LogStore.h"
#include "Benchmark.hpp"
#include <iostream>
#include <spdlog/sinks/base_sink.h>
#include <memory>
#include <mutex>

class LogSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit LogSink(std::shared_ptr<LogStore> store) : m_store(std::move(store))
    {
        if (!m_store) throw std::invalid_argument("LogSink requires a LogStore");
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        LogEntry entry;
        entry.level = msg.level;
        entry.timestamp = msg.time;
        entry.threadId = msg.thread_id;
        if (msg.logger_name.size() != 0)
            entry.loggerName.assign(msg.logger_name.data(), msg.logger_name.size());
        if (msg.payload.size() != 0)
            entry.message.assign(msg.payload.data(), msg.payload.size());
        if (msg.source.filename) entry.source.file = msg.source.filename;
        if (msg.source.funcname) entry.source.function = msg.source.funcname;
        entry.source.line = msg.source.line;
        m_store->Append(std::move(entry));
    }

    void flush_() override {}

private:
    const std::shared_ptr<LogStore> m_store;
};
