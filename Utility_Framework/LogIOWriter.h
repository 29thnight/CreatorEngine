#pragma once
#include <string>
#include <queue>
#include <atomic>
#include <fstream>
#include <mutex>
#include <filesystem>
#include "LogMessage.h"
#include <sqlite3.h>
#include "sqlite_modern_cpp.h"

namespace file = std::filesystem;

class LogIOWriter
{
private:
    std::mutex ioMutex;
    std::queue<LogMessage>* ioQueue;
    std::atomic<bool> running{ true };
    sqlite::database db;

public:
    LogIOWriter(const std::string_view& filename = "logs.db");
    ~LogIOWriter();

    void ioWorker();
    void ExchangeQueue(std::atomic<std::queue<LogMessage>*>& atomicQueue);
    bool isRunning() const;
    void stop();
};
