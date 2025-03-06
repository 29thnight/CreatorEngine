#include "Logger.h"
#include <iostream>
#include <thread>

using Logger = Log::Logger;

Logger Logger::instance;

Log::Logger::~Logger()
{
    if (pIoWriter)
    {
        remainingMessagesProcess();
        pIoWriter->stop();

        delete logQueue;
    }
}

Logger& Log::Logger::getInstance()
{
    return instance;
}

void Log::Logger::IOInitialize(const std::string_view& dbPath)
{
    pIoWriter = new LogIOWriter(dbPath);
    logQueue = new std::queue<LogMessage>();

    startIOWorker();
}

void Log::Logger::logMessage(const LogMessage& message)
{
	std::unique_lock lock(ioMutex);
    {
        std::cout << "[" << message.timestamp << "] "
            << "[" << message.level << "] "
            << message.message << std::endl;

        logQueue.load()->push(message);
    }

    if (logQueue.load()->size() >= batchSize && !batchInProgress.exchange(true))
    {
        pIoWriter->ExchangeQueue(logQueue);
        batchInProgress.store(false);
    }
}

void Log::Logger::startIOWorker()
{
    ioThread = std::thread(&LogIOWriter::ioWorker, pIoWriter);
    ioThread.detach();
}

void Log::Logger::stopIOWorker()
{
    pIoWriter->stop();
}

void Log::Logger::setBatchSize(size_t size)
{
    batchSize = size;
}

void Log::Logger::remainingMessagesProcess()
{
    while (!logQueue.load()->empty())
    {
        pIoWriter->ExchangeQueue(logQueue);
    }
}
