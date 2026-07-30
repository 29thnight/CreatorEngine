#pragma once
#include "Core.Definition.h"
#include "EngineSetting.h"
#include "PathFinder.h"
#include "LogSystem.h"
#include <DbgHelp.h>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <crtdbg.h>

#pragma comment(lib, "dbghelp.lib")

enum DUMP_TYPE
{
    DUNP_TYPE_MINI = MiniDumpWithDataSegs | MiniDumpWithCodeSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
    DUMP_TYPE_FULL = MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory
};

// 예외 코드를 사람이 읽을 수 있는 이름으로.
inline const char* ExceptionCodeName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case 0xE06D7363:                         return "C++_EXCEPTION";
    default:                                 return "UNKNOWN";
    }
}

/// 주어진 컨텍스트에서 시작해 스택을 심볼과 함께 문자열로 만든다.
///
/// 덤프 파일만 남기면 열어 볼 디버거가 있어야 한다. CLI나 CI에서 돌릴 때는
/// 그 자리에서 원인을 알아야 쓸모가 있어서, dbghelp로 직접 걸어 텍스트로 남긴다.
/// 크래시 처리 중이므로 새 할당을 최대한 피하고 고정 버퍼만 쓴다.
///
/// SEH가 아닌 경로(abort·terminate·CRT 잘못된 인자)에서는 예외 컨텍스트가 없어서,
/// 호출 지점에서 RtlCaptureContext로 뜬 현재 컨텍스트를 그대로 넘긴다.
/// <param name="skipFrames">
/// 맨 위에서 버릴 프레임 수. 이상 종료 경로는 크래시 핸들러 자신이 스택 위쪽을 차지해
/// 실제 원인이 아래로 밀리므로, 호출부가 자기 프레임 수만큼 걷어 내라고 알려 준다.
/// </param>
inline std::string BuildStackTrace(const CONTEXT& capturedContext, int skipFrames = 0)
{
    std::string report;
    report.reserve(4096);

    char line[512]{};
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);

    // StackWalk64가 컨텍스트를 고치므로 사본을 넘긴다.
    CONTEXT context = capturedContext;

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    // SYMBOL_INFO는 이름을 구조체 뒤에 이어 붙이는 형태라 넉넉히 잡아 둔다.
    constexpr DWORD kMaxNameLength = 255;
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + kMaxNameLength + 1]{};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = kMaxNameLength;

    constexpr int kMaxFrames = 48;
    int printed = 0;

    for (int walked = 0; walked < kMaxFrames + skipFrames; ++walked)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
            nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
        {
            break;
        }

        if (0 == frame.AddrPC.Offset) break;
        if (walked < skipFrames) continue;

        const int depth = printed++;
        DWORD64 displacement = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)
            ? symbol->Name : "(심볼 없음)";

        IMAGEHLP_LINE64 sourceLine{};
        sourceLine.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisplacement = 0;

        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &sourceLine))
        {
            std::snprintf(line, sizeof(line), "  #%02d %s  (%s:%lu)\n",
                depth, name, sourceLine.FileName, sourceLine.LineNumber);
        }
        else
        {
            std::snprintf(line, sizeof(line), "  #%02d %s + 0x%llX\n",
                depth, name, static_cast<unsigned long long>(displacement));
        }
        report += line;
    }

    // SymCleanup은 부르지 않는다 — 크래시 경로에서 정리하다 2차 크래시가 나는 편이 손해다.
    return report;
}

/// SEH 예외에서 사람이 읽을 수 있는 요약을 만든다.
inline std::string BuildCrashReport(EXCEPTION_POINTERS* pExceptionPointers)
{
    if (nullptr == pExceptionPointers || nullptr == pExceptionPointers->ExceptionRecord
        || nullptr == pExceptionPointers->ContextRecord)
    {
        return "[크래시] 예외 정보가 없습니다.\n";
    }

    const EXCEPTION_RECORD& record = *pExceptionPointers->ExceptionRecord;

    std::string report;
    report.reserve(4096);

    char line[512]{};
    std::snprintf(line, sizeof(line), "[크래시] %s (0x%08lX) at 0x%p\n",
        ExceptionCodeName(record.ExceptionCode),
        static_cast<unsigned long>(record.ExceptionCode),
        record.ExceptionAddress);
    report += line;

    // 접근 위반은 어느 주소를 어떻게 건드렸는지가 곧 단서다.
    if (EXCEPTION_ACCESS_VIOLATION == record.ExceptionCode && record.NumberParameters >= 2)
    {
        const ULONG_PTR operation = record.ExceptionInformation[0];
        const char* what = (0 == operation) ? "읽기" : (1 == operation) ? "쓰기" : "실행";
        std::snprintf(line, sizeof(line), "[크래시] %s 시도 주소 0x%llX\n",
            what, static_cast<unsigned long long>(record.ExceptionInformation[1]));
        report += line;
    }

    std::snprintf(line, sizeof(line), "[크래시] 스레드 %lu\n", GetCurrentThreadId());
    report += line;

    return report + BuildStackTrace(*pExceptionPointers->ContextRecord);
}

/// SEH가 아닌 이상 종료(abort·terminate·CRT 잘못된 인자)용 요약.
/// 예외 컨텍스트가 없으므로 지금 이 자리의 스택을 뜬다.
inline std::string BuildAbnormalExitReport(const char* reason, const char* detail)
{
    std::string report;
    report.reserve(4096);

    char line[768]{};
    std::snprintf(line, sizeof(line), "[크래시] %s%s%s\n[크래시] 스레드 %lu\n",
        (nullptr != reason) ? reason : "이상 종료",
        (nullptr != detail && '\0' != detail[0]) ? " — " : "",
        (nullptr != detail) ? detail : "",
        GetCurrentThreadId());
    report += line;

    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&context);

    // 이 함수 → CoreWindow::WriteCrashDump → LogSystem의 WriteCrashDumpOnce →
    // LogSystem의 핸들러. 네 겹을 걷어 내야 abort()/terminate()를 부른 쪽이 맨 위에 온다
    // (실측으로 맞춘 값이라, 호출 경로를 바꾸면 다시 재 봐야 한다).
    constexpr int kHandlerFrames = 4;
    return report + BuildStackTrace(context, kHandlerFrames);
}

/// 덤프 파일 경로를 만든다. 시각을 넣어 이전 크래시를 덮어쓰지 않게 한다
/// (예전에는 이름이 고정이라 연속으로 돌리면 직전 것이 사라졌다).
inline file::path MakeDumpFilePath()
{
    wchar_t moduleFileName[MAX_PATH]{};
    if (0 == GetModuleFileNameW(nullptr, moduleFileName, MAX_PATH)) return {};

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t stamp[64]{};
    std::swprintf(stamp, std::size(stamp), L"_%04d%02d%02d_%02d%02d%02d",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    file::path result(PathFinder::DumpPath());
    result /= file::path(moduleFileName).filename().replace_extension(L"").wstring() + stamp + L".dmp";
    return result;
}

/// 덤프 파일과 요약 텍스트를 남긴다.
///
/// 요약(report)을 인자로 받는 이유는 SEH 경로와 CRT 이상 종료 경로가 이 함수를 함께
/// 쓰기 때문이다. 후자는 예외 포인터가 없어 pExceptionPointers가 널로 들어온다
/// (MiniDumpWriteDump는 예외 정보 없이도 쓸 수 있다).
inline void WriteDumpArtifacts(EXCEPTION_POINTERS* pExceptionPointers, const std::string& report,
    DUMP_TYPE dumpType, HWND handle)
{
    // 덤프 기록 중 다시 죽더라도 로그는 남도록 먼저 밀어낸다.
    Log::FlushNow();

    file::path fileName = MakeDumpFilePath();
    if (fileName.empty())
    {
        Debug->LogError("Failed to get module file name for dump creation.");
        return;
	}

    HANDLE hFile = CreateFile(fileName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ExceptionPointers = pExceptionPointers;
    dumpInfo.ClientPointers = TRUE;
    MINIDUMP_TYPE miniDumpType = MiniDumpNormal;

    if (dumpType == DUNP_TYPE_MINI)
    {
        miniDumpType = MINIDUMP_TYPE(
            MiniDumpWithDataSegs |
            MiniDumpWithCodeSegs |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory
        );
    }
    else if (dumpType == DUMP_TYPE_FULL)
    {
        miniDumpType = MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory);
    }

    // 예외 정보가 없으면(abort 등) NULL을 넘긴다 — 그래도 스레드 스택은 다 담긴다.
    MINIDUMP_EXCEPTION_INFORMATION* dumpInfoPtr = (nullptr != pExceptionPointers) ? &dumpInfo : nullptr;

    if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, miniDumpType, dumpInfoPtr, NULL, NULL))
    {
        DWORD err = GetLastError();
    }


    CloseHandle(hFile);

    std::wstring adsName = fileName.wstring() + L":GitHash";
    std::ofstream ads(adsName, std::ios::binary);
    if (ads)
    {
        ads << EngineSettingInstance->GetGitVersionHash(); // 또는 g_EngineGitHash
        ads.close();
    }

    // 덤프 옆에 사람이 바로 읽을 수 있는 요약을 남긴다. 디버거를 열지 않고도
    // 콘솔이나 CI 로그에서 원인 함수를 볼 수 있어야 한다.
    file::path reportPath = fileName;
    reportPath.replace_extension(L".txt");

    if (std::ofstream out(reportPath, std::ios::binary); out)
    {
        out << "GitHash: " << EngineSettingInstance->GetGitVersionHash() << "\n\n" << report;
    }

    // 콘솔이 붙어 있으면(=CLI 실행) 그 자리에서 보여 준다.
    std::fputs(report.c_str(), stdout);
    std::printf("[크래시] 덤프: %ls\n[크래시] 요약: %ls\n",
        fileName.c_str(), reportPath.c_str());
    std::fflush(stdout);

    Debug->LogError(report);
    Debug->LogError("[크래시] 덤프: " + fileName.string());

    // 크래시 중에는 spdlog를 shutdown하지 않는다.
    // 다른 스레드가 아직 로깅 중일 수 있어 로거 파괴는 2차 크래시를 부른다.
    // 디스크 반영만 수행하고, 파일 마무리는 프로세스 종료 시 싱크 소멸자가 처리한다.
    Log::FlushNow();

    // 창이 없을 수도 있다(무인 실행 등). 그때는 종료를 호출자에게 맡긴다.
    if (nullptr != handle) PostMessage(handle, WM_CLOSE, 0, 0);
}

inline void CreateDump(EXCEPTION_POINTERS* pExceptionPointers, DUMP_TYPE dumpType, HWND handle)
{
    WriteDumpArtifacts(pExceptionPointers, BuildCrashReport(pExceptionPointers), dumpType, handle);
}

// SEH가 아닌 이상 종료(abort·terminate·purecall·CRT 잘못된 인자)를 받는 후크는
// LogSystem::InstallCrashGuards가 전부 걸어 둔다. 예전에는 여기서도 같은 후크를 걸어
// 서로 덮어썼고, 설치 순서에 따라 덤프가 남기도 하고 안 남기도 했다.
// 이제 그 경로들은 Log::SetCrashDumpWriter로 등록된 기록자를 통해 여기 도달한다.

// 무인 실행에서 CRT 대화상자가 프로세스를 멈춰 세우지 않게 한다.
// abort 메시지 창과 디버그 assert 창을 파일/stderr로 돌린다.
inline void SuppressCrtDialogs()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#ifdef _DEBUG
    for (int mode : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(mode, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(mode, _CRTDBG_FILE_STDERR);
    }
#endif
}

inline std::wstring GetDumpGitHashADS(const std::wstring& dumpFilePath)
{
    std::wstring adsPath = dumpFilePath + L":GitHash";
    std::ifstream ads(adsPath, std::ios::binary);

    if (!ads.is_open())
        return L"";

    std::string hashData((std::istreambuf_iterator<char>(ads)), std::istreambuf_iterator<char>());
    ads.close();

    // UTF-8 to wide string 변환 (필요 시)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, hashData.c_str(), -1, nullptr, 0);
    std::wstring result(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, hashData.c_str(), -1, &result[0], wlen);
    return result;
}
