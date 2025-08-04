#pragma once
#include <string>

#ifndef BUILD_FLAG
#include <Windows.h>
#include "LogSystem.h"

inline void RunMsbuildWithLiveLog(const std::wstring& commandLine)
{
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    {
        __debugbreak();
    }

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = NULL;

    PROCESS_INFORMATION pi;

    std::wstring fullCommand = commandLine;

    if (!CreateProcessW(NULL, &fullCommand[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        throw std::runtime_error("Build failed");
    }

    CloseHandle(hWrite);

    char buffer[4096]{};
    DWORD bytesRead;
    std::string leftover;

    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr))
    {
        if (bytesRead == 0) break;
        buffer[bytesRead] = '\0';

        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos)
        {
            std::string line = leftover.substr(0, pos);
            leftover.erase(0, pos + 1);

            if (line.empty())
            {
                continue;
            }

            if (line.find("error") != std::string::npos || line.find("error C") != std::string::npos)
            {
                Debug->LogError(line);
            }
            else if (line.find("오류") != std::string::npos || line.find("경고") != std::string::npos)
            {
                Debug->LogDebug(line);
            }
            //else
            //{
            //	std::string strLine = AnsiToUtf8(line);
            //	Debug->LogDebug(strLine);
            //}
        }
    }

    DWORD exitCode = 0;

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    if (exitCode != 0)
    {
        throw std::runtime_error("Build failed with exit code: " + std::to_string(exitCode));
    }
}
#else
inline void RunMsbuildWithLiveLog(const std::wstring& commandLine) {};
#endif 