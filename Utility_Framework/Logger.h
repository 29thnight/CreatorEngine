#pragma once
#include <string>
#include <queue>
#include <atomic>
#include <thread>
#include "LogIOWriter.h"
#include "LogMessage.h"


namespace Log
{
    class Logger
    {
    private:
        Logger() = default;
        ~Logger();
        static Logger& getInstance();

        void IOInitialize(const std::string_view& dbPath);
        void logMessage(const LogMessage& message);
        void startIOWorker();
        void stopIOWorker();
        void setBatchSize(size_t size);
        void remainingMessagesProcess();

    private:
        friend static inline void Warning(const std::string_view& message);
        friend static inline void Info(const std::string_view& message);
        friend static inline void Error(const std::string_view& message);
        friend static inline void startLOG(const std::string_view& dbPath);

    private:
        static Logger instance;
        std::thread ioThread;
		std::mutex ioMutex;
        std::atomic<std::queue<LogMessage>*> logQueue;
        std::atomic<bool> batchInProgress{ false };
        LogIOWriter* pIoWriter{};
        size_t batchSize = 10;
    };
}

inline void Log::Warning(const std::string_view& message)
{
    LogMessage logMessage("WARNING", message);

    Logger::getInstance().logMessage(logMessage);
}

inline void Log::Info(const std::string_view& message)
{
    LogMessage logMessage("INFO", message);
    Logger::getInstance().logMessage(logMessage);
}

inline void Log::Error(const std::string_view& message)
{
    LogMessage logMessage("ERROR", message);
    Logger::getInstance().logMessage(logMessage);
}

// Logger 인스턴스와 관련된 IO 스레드를 시작 및 종료
inline void Log::startLOG(const std::string_view& dbPath)
{
    Logger::getInstance().IOInitialize(dbPath);
}