// LogSystem.h
#pragma once
#include "LogSink.h"
#include "ClassProperty.h"
#include "DLLAcrossSingleton.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

class DebugClass : public DLLCore::Singleton<DebugClass>
{
private:
	friend class DLLCore::Singleton<DebugClass>;
    DebugClass() = default;
	~DebugClass() = default;

    std::shared_ptr<LogSink> logSink{};
	std::shared_ptr<spdlog::sinks::basic_file_sink_mt> fileSink{};
public:
	void Initialize();
	void Finalize();

	void LogWarning(std::string_view message)
	{
		spdlog::warn(message);
	}

	void Log(std::string_view message)
	{
		spdlog::info(message);
	}

	void LogError(std::string_view message)
	{
		spdlog::error(message);
	}

	void LogDebug(std::string_view message)
	{
		spdlog::debug(message);
	}

	void LogTrace(std::string_view message)
	{
		spdlog::trace(message);
	}

	void LogCritical(std::string_view message)
	{
		spdlog::critical(message);
	}

	void Flush()
	{
		spdlog::get("multi_logger")->flush();
	}

	void Clear()
	{
		logSink->ringBuffer_.clear();
	}

	bool IsClear() const
	{
		return logSink->ringBuffer_.IsClear();
	}

	void toggleClear()
	{
		logSink->ringBuffer_.toggleClear();
	}

	std::string GetBackLogMessage() const
	{
		return logSink->m_backLogMessage;
	}

	std::string& WriteBackLogMessage()
	{
		return logSink->m_backLogMessage;
	}

	std::vector<LogEntry> get_entries()
	{
		return logSink->ringBuffer_.get_all();
	}
};

static auto Debug = DebugClass::GetInstance();

namespace Log
{
	inline void Initialize() 
    {
		DebugClass::GetInstance();
		Debug->Initialize();
	}

	inline void Finalize()
	{
		Debug->Finalize();
		DebugClass::Destroy();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}