import "Creator.WinAPI.h";
#include "AppBackendWinAPI.Crash.h"
#include "../Crash/DumpHandler.h"
#include "../Crash/CrashReporterClient.h"

namespace
{
    DUMP_TYPE g_dumpType = DUMP_TYPE::DUNP_TYPE_MINI;
    bool g_unattended = false;
    void* (*g_windowProvider)() = nullptr;
}

void ce::platform::winapi::crash::WriteDump(void* exceptionPointers, const char* reason)
{
    auto* pointers = static_cast<EXCEPTION_POINTERS*>(exceptionPointers);
    HWND window = static_cast<HWND>(g_windowProvider ? g_windowProvider() : nullptr);

    // The launcher runs again as a small, independent crash reporter. Keep
    // this thread alive only until it has copied the exception and written
    // the dump; the reporter can keep its dialog open after this process dies.
    Log::FlushNow();
    const std::filesystem::path externalDumpPath = MakeDumpFilePath();
    crash_report::Session reporter;
    if (!externalDumpPath.empty() && reporter.WriteDump(pointers, externalDumpPath,
        reason, g_dumpType == DUMP_TYPE_FULL ? 2 : 1, g_unattended))
    {
        // Preserve the existing symbol-rich report. If this unstable process
        // fails while building it, the independent reporter still has the dump
        // and will write a small fallback report of its own.
        std::string report = (nullptr != pointers)
            ? BuildCrashReport(pointers)
            : BuildAbnormalExitReport(reason, nullptr);
        report += "[크래시] DumpWriterProcessId: " +
            std::to_string(reporter.ReporterProcessId()) + "\n";
        WriteCrashReportArtifacts(externalDumpPath, report, window);
        reporter.ReportReady();
        return;
    }

    // An old or missing launcher must not turn a crash into "no dump".
    CrashNotify("[크래시] 외부 기록자를 시작하지 못해 프로세스 내부에서 덤프를 기록합니다.");
    const std::filesystem::path dumpPath = WriteMinidumpFile(pointers, g_dumpType);
    const std::string report = (nullptr != pointers)
        ? BuildCrashReport(pointers)
        : BuildAbnormalExitReport(reason, nullptr);

    WriteCrashReportArtifacts(dumpPath, report, window);
}

void ce::platform::winapi::crash::SetUnattended(bool unattended) { g_unattended = unattended; }
bool ce::platform::winapi::crash::IsUnattended() { return g_unattended; }
void ce::platform::winapi::crash::EnableHeapValidation() { ::EnableHeapValidation(); }
void ce::platform::winapi::crash::SuppressCrtDialogs() { ::SuppressCrtDialogs(); }
void ce::platform::winapi::crash::CopyStackTrace(const void* capturedContext, int skipFrames, void* output, TextSink sink)
{
    const auto trace = ::BuildStackTrace(*static_cast<const CONTEXT*>(capturedContext), skipFrames);
    sink(output, trace.data(), static_cast<unsigned long long>(trace.size()));
}
void ce::platform::winapi::crash::InstallWriter(bool full, void* (*windowProvider)())
{
    g_dumpType = full ? DUMP_TYPE_FULL : DUNP_TYPE_MINI;
    g_windowProvider = windowProvider;

    // The CLI sets this flag after editor initialization. Crash handling is
    // installed before that, so detect automation arguments here as well.
    int argumentCount{};
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments)
    {
        for (int i = 1; i < argumentCount; ++i)
        {
            const std::wstring_view argument(arguments[i]);
            if (argument == L"--script" || argument == L"--exec" ||
                argument == L"--exec-args" || argument == L"--console" ||
                argument == L"--commandlet" ||
                argument == L"--commandlet-script")
            {
                g_unattended = true;
                break;
            }
        }
        LocalFree(arguments);
    }

    // 덤프 종류가 정해지는 유일한 지점이라 여기서 기록자를 등록한다.
    // 후크 자체는 LogSystem이 걸어 두었고, 크래시 경로 전부(SEH·terminate·abort·
    // purecall·CRT 잘못된 인자)가 이 콜백으로 모인다.
    //
    // 부팅 초반(EngineBootstrap::InitializeRuntime)에서 한 번 부르는 것이 계약이다.
    // 예전에는 App::Initialize 중반에서만 불려서, 그 전에 죽으면 로그만 남고
    // 덤프는 없었다.
    Log::SetCrashDumpWriter(&WriteDump);


    // 쌓인 덤프를 지금 정리한다. 디스크가 차면 다음 크래시의 덤프가 실패한다.
    PruneOldDumpFiles();
}
