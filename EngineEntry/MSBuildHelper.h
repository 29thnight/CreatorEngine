#pragma once
#include <string>

#ifndef BUILD_FLAG
#include <Windows.h>
#include "ProgressWindow.h"
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

inline void RunMsbuildWithLiveLogAndProgress(const std::wstring& commandLine)
{
    g_progressWindow->Launch();
    g_progressWindow->SetStatusText(L"Game Building...");

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
        g_progressWindow->SetStatusText(L"Build failed");
        g_progressWindow->Close();
        throw std::runtime_error("Build failed");
    }

    CloseHandle(hWrite);

    char buffer[4096]{};
    DWORD bytesRead;
    std::string leftover;

    int taskCount = 0;
    constexpr int estimatedTotalTasks = 100;
    int lastPercent = -1;

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

            if (line.find(".cpp") != std::string::npos ||
                line.find(".c") != std::string::npos ||
                line.find("ClCompile") != std::string::npos ||
                line.find("Linking") != std::string::npos ||
                line.find("Generate") != std::string::npos)
            {
                ++taskCount;
                int percent = std::min((taskCount * 100) / estimatedTotalTasks, 100);
                if (percent != lastPercent)
                {
                    lastPercent = percent;
                }
            }
            g_progressWindow->SetProgress(lastPercent);

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

    if (lastPercent < 100)
    {
        g_progressWindow->SetProgress(100);
	}

    DWORD exitCode = 0;

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    if (exitCode != 0)
    {
        g_progressWindow->Close();
        throw std::runtime_error("Build failed with exit code: " + std::to_string(exitCode));
    }

	g_progressWindow->SetStatusText(L"Game Build Completed");
    g_progressWindow->Close();
}

inline void RunMsbuildWithLiveLogAndProgress(const std::wstring& commandLine, const wchar_t* string)
{
    g_progressWindow->Launch();
    g_progressWindow->SetStatusText(string);

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
        g_progressWindow->SetStatusText(L"Build failed");
        g_progressWindow->Close();
        throw std::runtime_error("Build failed");
    }

    CloseHandle(hWrite);

    char buffer[4096]{};
    DWORD bytesRead;
    std::string leftover;

    int taskCount = 0;
    constexpr int estimatedTotalTasks = 100;
    int lastPercent = -1;

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

            if (line.find(".cpp") != std::string::npos ||
                line.find(".c") != std::string::npos ||
                line.find("ClCompile") != std::string::npos ||
                line.find("Linking") != std::string::npos ||
                line.find("Generate") != std::string::npos)
            {
                ++taskCount;
                int percent = std::min((taskCount * 100) / estimatedTotalTasks, 100);
                if (percent != lastPercent)
                {
                    lastPercent = percent;
                }
            }
            g_progressWindow->SetProgress(lastPercent);

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

    if (lastPercent < 100)
    {
        g_progressWindow->SetProgress(100);
    }

    DWORD exitCode = 0;

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    if (exitCode != 0)
    {
        g_progressWindow->Close();
        throw std::runtime_error("Build failed with exit code: " + std::to_string(exitCode));
    }

    g_progressWindow->SetStatusText(L"Game Build Completed");
    g_progressWindow->Close();
}
#else
inline void RunMsbuildWithLiveLog(const std::wstring& commandLine) {};
inline void RunMsbuildWithLiveLogAndProgress(const std::wstring& commandLine) {};
inline void RunMsbuildWithLiveLogAndProgress(const std::wstring& commandLine, const wchar_t* string) {};
#endif 