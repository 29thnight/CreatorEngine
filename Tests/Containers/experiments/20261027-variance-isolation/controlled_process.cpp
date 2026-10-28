#define NOMINMAX
#include <Windows.h>
#include <string>
#include <cstdio>

static std::wstring quote(const wchar_t* value)
{
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (; *value; ++value)
    {
        if (*value == L'\\') { ++slashes; continue; }
        if (*value == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result += *value;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    const bool high = std::wstring(argv[1]) == L"high";
    if (!high && std::wstring(argv[1]) != L"normal") return 3;
    std::wstring command;
    for (int i = 2; i < argc; ++i)
    {
        if (i != 2) command += L' ';
        command += quote(argv[i]);
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 4;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    const DWORD priority = high ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    if (!CreateProcessW(argv[2], command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | priority, nullptr, nullptr, &startup, &process))
    {
        CloseHandle(job);
        return 5;
    }
    if (!AssignProcessToJobObject(job, process.hProcess))
    {
        TerminateProcess(process.hProcess, 6);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
        return 6;
    }
    const auto observed = GetPriorityClass(process.hProcess);
    std::printf("process_control,priority_class,%lu,pid,%lu\n", observed, process.dwProcessId);
    std::fflush(stdout);
    if (observed != priority || ResumeThread(process.hThread) == DWORD(-1))
    {
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
        return 7;
    }
    const auto waited = WaitForSingleObject(process.hProcess, 600000);
    DWORD code = 8;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job);
    return static_cast<int>(code);
}
