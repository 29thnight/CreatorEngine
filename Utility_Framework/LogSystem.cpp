#include "LogSystem.h"

void DebugClass::Initialize()
{
    logSink = std::make_shared<LogSink>(2000);
	fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Editor.log", true);

	std::vector<spdlog::sink_ptr> sinks{ logSink, fileSink };

    auto logger = std::make_shared<spdlog::logger>("multi_logger", sinks.begin(), sinks.end() );
    logger->set_level(spdlog::level::trace); // 모든 로그 출력
    spdlog::set_default_logger(logger);
}

void DebugClass::Finalize()
{
    spdlog::get("multi_logger")->flush();
}
