#pragma once
#include "Core.Definition.h"
#include "PathFinder.h"
#include "LogSystem.h"
#include <DbgHelp.h>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <system_error>
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

/// 보존할 덤프 개수.
///
/// 전체 메모리 덤프는 한 개가 2~3GB라 로그(20개)처럼 넉넉히 둘 수 없다. 그냥 두면
/// 수십 GB까지 불어나고, 디스크가 차는 순간 CreateFile이나 MiniDumpWriteDump가
/// 실패해 '덤프가 없다'로 되돌아간다 — 정리는 덤프를 남기는 능력의 일부다.
constexpr size_t kMaxRetainedDumpFiles = 5;

/// 오래된 덤프를 지운다. 크래시 경로가 아니라 시작 시 1회만 부른다 —
/// 크래시 중에 디렉터리를 순회하다 2차 크래시가 나면 손해가 더 크다.
inline void PruneOldDumpFiles()
{
    std::error_code ec{};
    const file::path directory = PathFinder::DumpPath();
    if (!file::exists(directory, ec)) return;

    std::vector<file::path> dumps;
    for (const auto& entry : file::directory_iterator(directory, ec))
    {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".dmp") continue;
        dumps.push_back(entry.path());
    }

    if (dumps.size() <= kMaxRetainedDumpFiles) return;

    // 파일명에 타임스탬프가 들어가므로 사전순 정렬이 곧 시간순이다.
    std::sort(dumps.begin(), dumps.end());

    const size_t removeCount = dumps.size() - kMaxRetainedDumpFiles;
    for (size_t i = 0; i < removeCount; ++i)
    {
        file::remove(dumps[i], ec);

        // 요약도 같이 지운다. 덤프 없는 .txt만 남으면 열어 볼 수 없는 잔해가 된다.
        file::path report = dumps[i];
        report.replace_extension(L".txt");
        file::remove(report, ec);
    }
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

// ENGINE_VERSION(Core.Definition.h의 수동 버전)은 compile-time 문자열
// 리터럴이라 힙·subsystem 수명과 무관하다 — 크래시 경로가 직접 읽어도 된다.
// (옛 git SHA 주입 체계의 g_crashGitHash 캐시·ADS 기록·D&D 판독은
// 2026-08-24 버전 체계 전환으로 은퇴했다.)

/// 크래시 경로 전용 알림.
///
/// 이 경로의 실패는 절대 조용히 지나가면 안 된다 — 조용한 실패가 곧 '로그에는
/// CRASH 줄이 있는데 .dmp는 없다'는 상황을 만들었다. 로그 시스템이 이미 죽었을
/// 수도 있어 stdout과 디버거 출력에도 같이 흘린다.
///
/// 인자를 std::string이 아니라 const char*로 받는 이유는 힙이 손상된 프로세스에서
/// 불릴 수 있기 때문이다. 호출부는 고정 버퍼에 snprintf로 찍어서 넘긴다.
inline void CrashNotify(const char* message)
{
    if (nullptr == message) return;

    std::fputs(message, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);

    ::OutputDebugStringA(message);
    ::OutputDebugStringA("\n");

    if (Log::IsAlive() && Debug) Debug->LogError(message);
}

/// 미니덤프 파일만 남긴다. 성공하면 그 경로를, 실패하면 빈 경로를 돌려준다.
///
/// 크래시 처리에서 가장 먼저 불러야 하는 함수다. 심볼 해석(BuildStackTrace)은
/// dbghelp를 부르고 힙을 쓰는데, 힙 손상이나 스택 고갈로 죽은 프로세스에서는
/// 바로 그 자리가 2차 크래시 지점이 된다. 예전에는 요약을 먼저 만들고 덤프를
/// 나중에 썼기 때문에, 요약을 만들다 죽으면 덤프가 통째로 사라졌다.
inline file::path WriteMinidumpFile(EXCEPTION_POINTERS* pExceptionPointers, DUMP_TYPE dumpType)
{
    char line[1024]{};

    file::path fileName = MakeDumpFilePath();
    if (fileName.empty())
    {
        CrashNotify("[크래시] 모듈 경로를 얻지 못해 덤프를 만들 수 없습니다.");
        return {};
    }

    // 디렉터리가 없으면 CreateFile이 그냥 실패한다. 예전에는 그 실패가 조용한
    // return으로 끝나서 '덤프가 없다'는 사실만 남았다.
    std::error_code ec{};
    file::create_directories(fileName.parent_path(), ec);

    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (INVALID_HANDLE_VALUE == hFile)
    {
        std::snprintf(line, sizeof(line), "[크래시] 덤프 파일 생성 실패 (GetLastError=%lu): %ls",
            static_cast<unsigned long>(GetLastError()), fileName.c_str());
        CrashNotify(line);
        return {};
    }

    MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ExceptionPointers = pExceptionPointers;
    dumpInfo.ClientPointers = FALSE;   // 같은 프로세스에서 뜨므로 포인터는 우리 주소 공간이다

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

    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
        miniDumpType, dumpInfoPtr, nullptr, nullptr);
    const DWORD writeError = written ? 0 : GetLastError();

    CloseHandle(hFile);

    if (!written)
    {
        // 실패 코드는 HRESULT로 온다. 0바이트 껍데기를 남기면 다음 사람이
        // '덤프는 있는데 안 열린다'로 헤매므로 지운다.
        std::snprintf(line, sizeof(line), "[크래시] MiniDumpWriteDump 실패 (HRESULT=0x%08lX): %ls",
            static_cast<unsigned long>(writeError), fileName.c_str());
        CrashNotify(line);

        file::remove(fileName, ec);
        return {};
    }

    std::snprintf(line, sizeof(line), "[크래시] 덤프 기록 완료: %ls", fileName.c_str());
    CrashNotify(line);

    return fileName;
}

/// 덤프 옆에 사람이 바로 읽을 수 있는 요약(.txt)과 엔진 버전을 남긴다.
///
/// 덤프가 이미 디스크에 있는 뒤에 불린다. 여기서 죽어도 덤프는 남는다는 것이
/// 이 분리의 목적이다.
inline void WriteCrashReportArtifacts(const file::path& dumpPath, const std::string& report, HWND handle)
{
    if (!dumpPath.empty())
    {
        // 디버거를 열지 않고도 콘솔이나 CI 로그에서 원인 함수를 볼 수 있어야 한다.
        file::path reportPath = dumpPath;
        reportPath.replace_extension(L".txt");

        if (std::ofstream out(reportPath, std::ios::binary); out)
        {
            out << "EngineVersion: " << ENGINE_VERSION << "\n\n" << report;
        }

        std::printf("[크래시] 요약: %ls\n", reportPath.c_str());
    }

    // 콘솔이 붙어 있으면(=CLI 실행) 그 자리에서 보여 준다.
    std::fputs(report.c_str(), stdout);
    std::fflush(stdout);

    if (Log::IsAlive() && Debug)
    {
        Debug->LogError(report);
    }

    // 크래시 중에는 spdlog를 shutdown하지 않는다.
    // 다른 스레드가 아직 로깅 중일 수 있어 로거 파괴는 2차 크래시를 부른다.
    // 디스크 반영만 수행하고, 파일 마무리는 프로세스 종료 시 싱크 소멸자가 처리한다.
    Log::FlushNow();

    // 창이 없을 수도 있다(무인 실행 등). 그때는 종료를 호출자에게 맡긴다.
    if (nullptr != handle) PostMessage(handle, WM_CLOSE, 0, 0);
}

/// 덤프 → 요약 순서로 남긴다. 순서가 계약이다.
inline void WriteDumpArtifacts(EXCEPTION_POINTERS* pExceptionPointers, const std::string& report,
    DUMP_TYPE dumpType, HWND handle)
{
    // 덤프 기록 중 다시 죽더라도 로그는 남도록 먼저 밀어낸다.
    Log::FlushNow();

    const file::path dumpPath = WriteMinidumpFile(pExceptionPointers, dumpType);
    WriteCrashReportArtifacts(dumpPath, report, handle);
}

inline void CreateDump(EXCEPTION_POINTERS* pExceptionPointers, DUMP_TYPE dumpType, HWND handle)
{
    // 덤프부터 쓰고 나서 요약을 만든다 — WriteDumpArtifacts에 넘기면 요약을
    // 만드는 동안(인자 평가) 죽었을 때 덤프까지 잃는다.
    Log::FlushNow();

    const file::path dumpPath = WriteMinidumpFile(pExceptionPointers, dumpType);
    WriteCrashReportArtifacts(dumpPath, BuildCrashReport(pExceptionPointers), handle);
}

// SEH가 아닌 이상 종료(abort·terminate·purecall·CRT 잘못된 인자)를 받는 후크는
// LogSystem::InstallCrashGuards가 전부 걸어 둔다. 예전에는 여기서도 같은 후크를 걸어
// 서로 덮어썼고, 설치 순서에 따라 덤프가 남기도 하고 안 남기도 했다.
// 이제 그 경로들은 Log::SetCrashDumpWriter로 등록된 기록자를 통해 여기 도달한다.

/// CRT 디버그 힙의 전수 검사를 켠다(`--heapcheck`).
///
/// 힙 손상(0xC0000374)은 망가뜨린 코드와 죽는 지점이 멀리 떨어져 있어서, 그냥
/// 두면 '종료 시 간헐 크래시'로만 보인다. 이 플래그를 켜면 모든 할당·해제마다
/// 힙 전체를 검사하므로, 망가뜨린 그 호출에서 바로 멈춘다 — 간헐이 결정적이 된다.
///
/// 대가는 속도다. 프레임이 수십 배 느려지므로 평소 실행에는 켜지 않는다.
/// Release 빌드에는 CRT 디버그 힙 자체가 없어 아무 일도 하지 않는다.
inline void EnableHeapValidation()
{
#ifdef _DEBUG
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF;
    _CrtSetDbgFlag(flags);

    std::fputs("[힙검사] CRT 디버그 힙 전수 검사 켜짐 - 매우 느려진다.\n", stdout);
    std::fflush(stdout);
#else
    std::fputs("[힙검사] Release 빌드에는 CRT 디버그 힙이 없어 켤 수 없다.\n", stdout);
    std::fflush(stdout);
#endif
}

// 무인 실행에서 CRT 대화상자가 프로세스를 멈춰 세우지 않게 한다.
// abort 메시지 창과 assert 창을 파일/stderr로 돌린다.
//
// ── 경로가 둘이라는 것 ──
//
// ★ 오래 이 함수가 절반만 덮고 있었다. 창을 띄우는 길이 둘인데 하나만 막혀
//   있었고, 그 사실이 R2b 검증에서야 드러났다:
//
//     _ASSERT · _ASSERTE   CRT 디버그 매크로 → _CrtDbgReport
//                          → _CrtSetReportMode가 돌린다 (아래 루프)
//     assert()             <assert.h> → _wassert
//                          → _set_error_mode가 돌린다 (아래 한 줄)
//
//   둘째 것이 빠져 있어서 assert()는 그대로 메시지 박스를 띄웠다. 실제로
//   dx12.scene의 DirectXMath 어서션(XMMatrixOrthographicOffCenterLH의
//   ViewRight==ViewLeft)이 --script 실행을 멈춰 세웠고, 답할 사람이 없어
//   프로세스가 무한정 서 있었다. 로그에는 아무것도 안 남고 CPU만 도니
//   '멈춘 것'과 '느린 것'이 구분되지 않아, 원인을 찾는 데 25분을 버리고도
//   화면을 직접 보기 전까지 알 수 없었다.
//
//   지금은 파일·줄·식이 stderr에 찍히고 assert()가 abort()로 넘어간다.
//   위 _set_abort_behavior가 abort 쪽 창도 이미 막아 두었으므로, 프로세스는
//   유한 시간에 죽고 무엇 때문에 죽었는지가 로그에 남는다.
//
// ── 호출 범위에 대한 메모 ──
//
// 호출부(ConsoleCommandSystem)는 --script·--exec뿐 아니라 --console에서도
// 이것을 부른다. --console은 사람이 붙어 있어 JIT 디버깅 창이 오히려 쓸모
// 있는 경우인데, 그 구분은 이 변경 전부터 없던 것이라 여기서 바꾸지 않는다.
// 바꾼다면 호출부에서 wantStdinReader로 가르는 것이 맞다.
inline void SuppressCrtDialogs()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    // assert()가 메시지 박스 대신 stderr로 나가게 한다.
    //
    // Release 빌드에도 걸어 둔다. assert()는 NDEBUG로 사라지지만 CRT가
    // 런타임 오류를 보고하는 다른 경로들이 같은 설정을 따르고, 그쪽도
    // 무인 실행에서 창을 띄우면 안 되는 것은 마찬가지다.
    _set_error_mode(_OUT_TO_STDERR);

#ifdef _DEBUG
    for (int mode : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(mode, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(mode, _CRTDBG_FILE_STDERR);
    }
#endif
}
