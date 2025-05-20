// LogSystem.h
#pragma once
#include "LogSink.h"
#include "ClassProperty.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

class DebugClass : public Singleton<DebugClass>
{
private:
	friend class Singleton;
    DebugClass() = default;
	~DebugClass() = default;

    std::shared_ptr<LogSink> logSink{};
	std::shared_ptr<spdlog::sinks::basic_file_sink_mt> fileSink{};
public:
	void Initialize();
	void Finalize();

	void LogWarning(const std::string_view& message)
	{
		spdlog::warn(message);
	}

	void Log(const std::string_view& message)
	{
		spdlog::info(message);
	}

	void LogError(const std::string_view& message)
	{
		spdlog::error(message);
	}

	void LogDebug(const std::string_view& message)
	{
		spdlog::debug(message);
	}

	void LogTrace(const std::string_view& message)
	{
		spdlog::trace(message);
	}

	void LogCritical(const std::string_view& message)
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

inline auto& Debug = DebugClass::GetInstance();

namespace Log
{
	inline void Initialize() 
    {
		Debug->Initialize();
	}

	inline void Finalize()
	{
		Debug->Finalize();

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

    inline ImVec4 GetColorForLevel(spdlog::level::level_enum level) 
    {
        switch (level) 
        {
        case spdlog::level::info: return { 1.0f, 1.0f, 1.0f, 1.0f };
        case spdlog::level::warn: return { 1.0f, 1.0f, 0.0f, 1.0f };
        case spdlog::level::err:  return { 1.0f, 0.3f, 0.3f, 1.0f };
        default: return { 1.0f, 1.0f, 1.0f, 1.0f };
        }
    }
}
