#pragma once
#include "LogEntry.h"
#include "RingBuffer.h"
#include "Benchmark.hpp"
#include <iostream>
#include <spdlog/sinks/base_sink.h>
#include <mutex>

class DebugClass;
class LogSink : public spdlog::sinks::base_sink<std::mutex> 
{
public:
    explicit LogSink(size_t maxEntries = 1000)
        : ringBuffer_(maxEntries) 
	{
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
		m_backLogMessage = fmt::to_string(formatted);
        ringBuffer_.push({ msg.level, m_backLogMessage });
    }

    void flush_() override {}

private:
	friend class DebugClass;
    RingBuffer<LogEntry> ringBuffer_;
	std::string m_backLogMessage;
};
