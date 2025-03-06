#include "LogMessage.h"
#include <chrono>
#include <iostream>
#include <string>
#include <format>

std::string formatTimestampWithZonedTime(const std::chrono::system_clock::time_point& tp)
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream ss;
    tm timeinfo;

    localtime_s(&timeinfo, &in_time_t);

    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");


    return ss.str();
}


LogMessage::LogMessage(const std::string_view& level, const std::string_view& message) :
    message(message),
    level(level),
    timestamp(formatTimestampWithZonedTime(std::chrono::system_clock::now()))
{
}
