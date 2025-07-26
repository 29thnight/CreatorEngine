#include "LogSystem.h"
#include <chrono>

void DebugClass::Initialize()
{
    logSink = std::make_shared<LogSink>(500);
	fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        "Log\\Editor.log", 
        true
    );

	std::vector<spdlog::sink_ptr> sinks{ logSink, fileSink };

    auto logger = std::make_shared<spdlog::logger>(
        "multi_logger", sinks.begin(), sinks.end() 
    );
    logger->set_level(spdlog::level::trace); // 모든 로그 출력
    spdlog::set_default_logger(logger);
}

void DebugClass::Finalize()
{
	logSink->flush();
    fileSink->flush();

    spdlog::shutdown();
}