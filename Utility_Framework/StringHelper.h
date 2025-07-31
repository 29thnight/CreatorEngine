#pragma once
#include <string>
#include <Windows.h>

std::string AnsiToUtf8(const std::string& ansiStr)
{
    // ANSI ¡æ Wide
    int wideLen = MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, &wide[0], wideLen);

    // Wide ¡æ UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Len, nullptr, nullptr);

    return utf8;
}
