#pragma once
#include <string>
#include <chrono>

struct LogMessage
{
	std::string level;
	std::string message;
	std::string timestamp;

	LogMessage(const std::string_view& level, const std::string_view& message);
};