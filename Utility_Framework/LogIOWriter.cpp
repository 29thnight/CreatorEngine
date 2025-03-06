#include "LogIOWriter.h"
#include <stdexcept>
#include <io.h>
#include <string>

LogIOWriter::LogIOWriter(const std::string_view& filename)
	: running(true), db(filename.data())
{
	db << "CREATE TABLE IF NOT EXISTS Logs ("
		"ID INTEGER PRIMARY KEY AUTOINCREMENT, "
		"Timestamp TEXT NOT NULL, "
		"LogLevel TEXT NOT NULL, "
		"Message TEXT NOT NULL"
		");";

	ioQueue = new std::queue<LogMessage>();
}

LogIOWriter::~LogIOWriter()
{
	delete ioQueue;
}

void LogIOWriter::ioWorker()
{
	while (running)
	{
		if (!ioQueue->empty())
		{
			if(0 < ioQueue->front().level.size())
			{
				db << "INSERT INTO Logs (Timestamp, LogLevel, Message) VALUES (?, ?, ?);"
					<< ioQueue->front().timestamp
					<< ioQueue->front().level
					<< ioQueue->front().message;

				ioQueue->pop();
			}

		}
		else
		{
			std::this_thread::yield();
		}
	}
}

void LogIOWriter::ExchangeQueue(std::atomic<std::queue<LogMessage>*>& atomicQueue)
{
	bool exchanged = false;
	while (!exchanged)
	{
		if (ioQueue->empty())
		{
			auto localQueue = atomicQueue.load();
			atomicQueue.store(ioQueue);
			ioQueue = localQueue;

			exchanged = true;
		}
	}
}

bool LogIOWriter::isRunning() const
{
	return running;
}

void LogIOWriter::stop()
{
	while (!ioQueue->empty())
	{
		//wait
	}

	running = false;
}
