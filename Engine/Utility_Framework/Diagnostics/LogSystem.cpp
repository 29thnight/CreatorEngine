#include "../../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "LogSystem.h"

#include <spdlog/sinks/msvc_sink.h>

#include <float.h>
#include <stdio.h>
#include "../Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

namespace
{
    // 세션 로그 파일 보존 개수. 초과분은 오래된 것부터 삭제한다.
    constexpr size_t kMaxRetainedLogFiles = 20;

    // 이 레벨 이상은 기록 즉시 디스크까지 밀어낸다.
    // 크래시 직전 경고/오류가 유실되지 않도록 하는 1차 방어선.
    constexpr spdlog::level::level_enum kImmediateFlushLevel = spdlog::level::warn;

    // 나머지 레벨(info/debug/trace)의 주기적 flush 간격.
    constexpr std::chrono::seconds kPeriodicFlushInterval{ 1 };

    std::atomic<bool> g_guardsInstalled{ false };
    std::atomic<bool> g_crashReported{ false };

    // 로그 시스템 생존 여부.
    //
    // 전역 Debug 포인터(LogSystem.h의 static)는 각 번역 단위에 복사된 원시 포인터라
    // DebugClass::Destroy() 이후에도 파괴된 인스턴스의 주소를 그대로 들고 있다.
    // 따라서 nullptr 검사만으로는 유효성을 판단할 수 없고, 종료 단계의 훅
    // (atexit / 크래시 핸들러)이 이를 역참조하면 use-after-free가 된다.
    std::atomic<bool> g_logSystemAlive{ false };

std::terminate_handler g_previousTerminate{ nullptr };

    // 크래시 덤프를 남길 곳. 로그 시스템은 후크 설치만 맡고 실제 기록은 여기에 위임한다.
    // 예전에는 CoreWindow가 같은 후크를 따로 걸어 서로 덮어썼고, 설치 순서에 따라
    // 덤프가 남기도 하고 안 남기도 했다(실제로 접근 위반 하나가 덤프 없이 지나갔다).
    Log::CrashDumpWriter g_crashDumpWriter{ nullptr };
    std::atomic<bool> g_crashDumpWritten{ false };

    // 크래시 경로는 중복 진입할 수 있으므로(terminate -> abort) 덤프도 1회로 막는다.
    void WriteCrashDumpOnce(void* exceptionPointers, ce::utf8_string_view reason)
    {
        if (nullptr == g_crashDumpWriter)
        {
            // 조용히 지나가면 '로그에 CRASH 줄은 있는데 .dmp는 없다'가 되고,
            // 나중에 그 이유를 알 방법이 없어진다. 미등록 자체를 사건으로 남긴다.
            std::fputs("[크래시] 덤프 기록자가 등록되지 않아 .dmp를 남기지 못했다"
                " (EngineBootstrap에서 AppBackendWinAPI::SetDumpType이 불렸는지 확인할 것).\n", stdout);
            std::fflush(stdout);

            if (g_logSystemAlive.load(std::memory_order_acquire))
            {
                Debug::PrintLog(spdlog::level::err, "크래시 덤프 기록자 미등록 - 이번 크래시는 덤프 없이 지나간다.");
                DebugClass::GetInstance()->FlushNow();
            }
            return;
        }

        bool expected = false;
        if (!g_crashDumpWritten.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return;
        }

        const ce::utf8_string text(reason);
        g_crashDumpWriter(exceptionPointers, text.c_str());
    }
    LPTOP_LEVEL_EXCEPTION_FILTER g_previousSehFilter{ nullptr };

    // 크래시 경로는 중복 진입할 수 있다(예: terminate -> abort).
    // 최초 1회만 기록해 로그가 어지러워지는 것을 막는다.
    void ReportCrashOnce(ce::utf8_string_view reason)
    {
        bool expected = false;
        if (!g_crashReported.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return;
        }

        if (!g_logSystemAlive.load(std::memory_order_acquire))
        {
            return;
        }

        DebugClass::GetInstance()->NotifyCrash(std::string_view(reason.data(), reason.size()));
    }

    void OnTerminate()
    {
        ReportCrashOnce("std::terminate 호출 - 미처리 C++ 예외 또는 noexcept 위반");
        WriteCrashDumpOnce(nullptr, "std::terminate 호출 - 미처리 C++ 예외 또는 noexcept 위반");

        if (g_previousTerminate && g_previousTerminate != &OnTerminate)
        {
            g_previousTerminate();
        }
        std::abort();
    }

    void OnAbortSignal(int)
    {
        ReportCrashOnce("abort() 호출 - CRT 이상 종료");
        WriteCrashDumpOnce(nullptr, "abort() 호출 - CRT 이상 종료");
    }

    void OnPureCall()
    {
        ReportCrashOnce("순수 가상 함수 호출 - 생성/소멸 중인 객체의 가상 함수 접근");
        WriteCrashDumpOnce(nullptr, "순수 가상 함수 호출");
    }

    void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        ReportCrashOnce("CRT 잘못된 파라미터 감지 - 표준 라이브러리 사전 조건 위반");
        WriteCrashDumpOnce(nullptr, "CRT 잘못된 파라미터");
    }

    void OnProcessExit()
    {
        // 정상 종료 경로에서 Finalize()가 호출되지 않은 채 빠져나가는 경우를 위한 최후 방어.
        // 이미 Finalize된 뒤라면 Debug는 파괴된 인스턴스를 가리키므로 접근하지 않는다.
        if (!g_logSystemAlive.load(std::memory_order_acquire))
        {
            return;
        }

        if (DebugClass::GetInstance()->IsInitialized())
        {
            DebugClass::GetInstance()->FlushNow();
        }
    }

    ce::utf8_string DescribeExceptionCode(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:      return ce::utf8_string(ce::utf8_string_view("ACCESS_VIOLATION (잘못된 메모리 접근 - 널/댕글링 포인터 의심)"));
        case EXCEPTION_STACK_OVERFLOW:        return ce::utf8_string(ce::utf8_string_view("STACK_OVERFLOW (스택 오버플로 - 무한 재귀 의심)"));
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return ce::utf8_string(ce::utf8_string_view("INT_DIVIDE_BY_ZERO (정수 0 나눗셈)"));
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return ce::utf8_string(ce::utf8_string_view("FLT_DIVIDE_BY_ZERO (부동소수 0 나눗셈)"));
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return ce::utf8_string(ce::utf8_string_view("ILLEGAL_INSTRUCTION (잘못된 명령 - 손상된 함수 포인터 의심)"));
        case EXCEPTION_PRIV_INSTRUCTION:      return ce::utf8_string(ce::utf8_string_view("PRIV_INSTRUCTION (특권 명령 실행)"));
        case EXCEPTION_IN_PAGE_ERROR:         return ce::utf8_string(ce::utf8_string_view("IN_PAGE_ERROR (페이지 로드 실패)"));
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return ce::utf8_string(ce::utf8_string_view("ARRAY_BOUNDS_EXCEEDED (배열 범위 초과)"));
        case 0xE06D7363:                      return ce::utf8_string(ce::utf8_string_view("C++ 예외 (미처리 throw)"));
        default:                              break;
        }

        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "예외 코드 0x%08lX", static_cast<unsigned long>(code));
        return ce::utf8_string(ce::utf8_string_view(buffer));
    }

    ce::utf8_string BuildSehReason(EXCEPTION_POINTERS* info)
    {
        ce::utf8_string reason{ce::utf8_string_view("미처리 예외로 프로세스 종료 - ")};
        if (info && info->ExceptionRecord)
        {
            reason.append(DescribeExceptionCode(info->ExceptionRecord->ExceptionCode));

            char address[64]{};
            std::snprintf(address, sizeof(address), " @ 0x%p",
                info->ExceptionRecord->ExceptionAddress);
            reason.append(address);
        }
        else
        {
            reason.append("예외 정보 없음");
        }
        return reason;
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
    {
        const ce::utf8_string sehReason = BuildSehReason(info);
        ReportCrashOnce(sehReason);
        WriteCrashDumpOnce(info, sehReason);

        if (g_previousSehFilter && g_previousSehFilter != &OnUnhandledException)
        {
            return g_previousSehFilter(info);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // 세션마다 새 파일을 만들되, 디렉터리가 무한히 불어나지 않게 오래된 것부터 정리한다.
    void PruneOldLogFiles(const std::filesystem::path& directory)
    {
        std::error_code ec{};
        if (!std::filesystem::exists(directory, ec))
        {
            return;
        }

        ce::dynamic_array<std::filesystem::path> logFiles;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".html") continue;
            logFiles.push_back(entry.path());
        }

        if (logFiles.size() <= kMaxRetainedLogFiles)
        {
            return;
        }

        // 파일명에 타임스탬프가 들어가므로 사전순 정렬이 곧 시간순이다.
        std::sort(logFiles.begin(), logFiles.end());

        const size_t removeCount = logFiles.size() - kMaxRetainedLogFiles;
        for (size_t i = 0; i < removeCount; ++i)
        {
            std::error_code removeError{};
            std::filesystem::remove(logFiles[i], removeError);
        }
    }

    ce::utf8_string BuildLogFileName(ce::utf8_string_view sessionName)
    {
        const std::time_t raw = std::time(nullptr);
        std::tm local{};
        ::localtime_s(&local, &raw);

        char stamp[32]{};
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);

        ce::utf8_string name(sessionName);
        name.push_back('_');
        name.append(stamp);
        name.append(".html");
        return name;
    }
}

bool Log::IsAlive() noexcept
{
    return g_logSystemAlive.load(std::memory_order_acquire);
}

void Log::NotifySehCrash(void* exceptionPointers)
{
    ReportCrashOnce(BuildSehReason(static_cast<EXCEPTION_POINTERS*>(exceptionPointers)));
}

void Log::SetCrashDumpWriter(CrashDumpWriter writer)
{
    g_crashDumpWriter = writer;

    // 등록 사실을 시작 로그에 남긴다.
    //
    // 예전에는 등록이 빠져도 아무 흔적이 없었고, 크래시가 난 뒤에야 '덤프가
    // 없네'로 알게 됐다. 이제는 세션 로그 첫머리만 봐도 이번 실행이 덤프를
    // 남길 수 있는 상태인지 판단할 수 있다.
    if (!g_logSystemAlive.load(std::memory_order_acquire)) return;

    if (nullptr != writer)
    {
        Debug::PrintLog(spdlog::level::info, "크래시 덤프 기록자 등록 완료 - 이번 실행의 크래시는 .dmp로 남는다.");
    }
    else
    {
        Debug::PrintLog(spdlog::level::warn, "크래시 덤프 기록자가 해제됐다 - 이후 크래시는 덤프 없이 지나간다.");
    }
}

bool Log::HasCrashDumpWriter() noexcept
{
    return nullptr != g_crashDumpWriter;
}

void Log::InstallCrashGuards()
{
    bool expected = false;
    if (!g_guardsInstalled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return;
    }

    g_previousTerminate = std::set_terminate(&OnTerminate);
    g_previousSehFilter = ::SetUnhandledExceptionFilter(&OnUnhandledException);

    std::signal(SIGABRT, &OnAbortSignal);
    _set_purecall_handler(&OnPureCall);
    _set_invalid_parameter_handler(&OnInvalidParameter);

    // abort()가 CRT 오류 대화상자를 띄우며 멈추지 않도록 해
    // 시그널 핸들러가 확실히 실행되게 한다.
    _set_abort_behavior(0, _WRITE_ABORT_MSG);

    std::atexit(&OnProcessExit);
}

void DebugClass::Initialize(std::string_view sessionName)
{
    if (m_initialized.load(std::memory_order_acquire))
    {
        return;
    }

    const std::filesystem::path logDirectory = ce::paths::log_path();

    std::error_code ec{};
    std::filesystem::create_directories(logDirectory, ec);
    PruneOldLogFiles(logDirectory);

    const auto logName = BuildLogFileName(ce::utf8_string_view(sessionName.data(), sessionName.size()));
    const std::filesystem::path logFile = logDirectory /
        std::filesystem::path(std::u8string(logName.data(), logName.data() + logName.size()));
    const auto utf8Path = logFile.u8string();
    m_logFilePath.resize_and_overwrite(utf8Path.size(), [&](char* data, std::size_t size) {
        if (size != 0) std::memcpy(data, utf8Path.data(), size);
        return size;
    });

    m_logStore->Clear();
    logSink = std::make_shared<LogSink>(m_logStore);
    htmlSink = std::make_shared<HtmlFileSink>(logFile, false);

    ce::dynamic_array<spdlog::sink_ptr> sinks{ logSink, htmlSink };

#ifdef _DEBUG
    // 디버거 출력 창에도 함께 흘려보내면 IDE에서 바로 확인할 수 있다.
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

    auto logger = std::make_shared<spdlog::logger>(
        "multi_logger", sinks.begin(), sinks.end()
    );
    logger->set_level(spdlog::level::trace);

    // 유실 방지 1차: 경고 이상은 기록 즉시 디스크로.
    logger->flush_on(kImmediateFlushLevel);

    spdlog::set_default_logger(logger);

    // 유실 방지 2차: 나머지 레벨도 주기적으로 디스크에 반영.
    spdlog::flush_every(kPeriodicFlushInterval);

    m_initialized.store(true, std::memory_order_release);
    g_logSystemAlive.store(true, std::memory_order_release);

    // 유실 방지 3차: 비정상 종료 경로 전반에 flush 훅 설치.
    Log::InstallCrashGuards();

    spdlog::info("로그 세션 시작: {}", std::string_view(m_logFilePath.data(), m_logFilePath.size()));
}

void DebugClass::AbortInitialization() noexcept
{
    // Cleanup paths consult Log::IsAlive before dereferencing the TU-local Debug
    // pointer. Lower this first so a partially initialized logger can never expose a
    // dangling Debug pointer after the singleton is destroyed.
    m_initialized.store(false, std::memory_order_release);
    g_logSystemAlive.store(false, std::memory_order_release);

    try
    {
        spdlog::shutdown();
    }
    catch (...) {}

    htmlSink.reset();
    logSink.reset();
    m_logFilePath.clear();
}

void DebugClass::FlushNow()
{
    if (!m_initialized.load(std::memory_order_acquire))
    {
        return;
    }

    // 로거를 경유해 flush하면 모든 싱크가 함께 밀린다.
    // 로거가 이미 교체/파괴된 상황에 대비해 싱크에도 직접 지시한다.
    if (auto logger = spdlog::default_logger_raw())
    {
        logger->flush();
    }

    if (htmlSink) htmlSink->flush();
    if (logSink)  logSink->flush();
}

void DebugClass::NotifyCrash(std::string_view reason)
{
    if (!m_initialized.load(std::memory_order_acquire))
    {
        return;
    }

    // 크래시 상황에서는 spdlog 로거 자체가 손상되었을 수 있으므로
    // 싱크에 직접 기록한 뒤, 가능한 경우에만 로거 경유 flush를 시도한다.
    if (htmlSink)
    {
        htmlSink->WriteCrashBanner(reason);
    }

    if (auto logger = spdlog::default_logger_raw())
    {
        logger->flush();
    }
}

void DebugClass::Finalize()
{
    if (!m_initialized.load(std::memory_order_acquire))
    {
        return;
    }

    spdlog::info("로그 세션 정상 종료");

    if (auto logger = spdlog::default_logger_raw())
    {
        logger->flush();
    }

    // 정상 종료 마커. 이 표시가 없으면 뷰어가 비정상 종료로 판정한다.
    if (htmlSink)
    {
        htmlSink->MarkGracefulShutdown();
        htmlSink->flush();
    }
    if (logSink)
    {
        logSink->flush();
    }

    m_initialized.store(false, std::memory_order_release);

    // 이 시점 이후 DebugClass::Destroy()가 인스턴스를 파괴하므로,
    // 종료 훅들이 더 이상 Debug를 역참조하지 않도록 생존 플래그를 내린다.
    g_logSystemAlive.store(false, std::memory_order_release);

    // shutdown()은 주기 flush 스레드를 정지시키고 모든 로거를 파괴한다.
    // 이후 htmlSink 소멸자가 푸터를 기록하며 파일을 닫는다.
    spdlog::shutdown();

    htmlSink.reset();
    logSink.reset();
}

extern "C" void creator_report_delegate_exception(const char* phase, const char* what) noexcept
{
    try
    {
        Debug::PrintLog(spdlog::level::err, "[Delegate] {} 중 예외: {}", phase, what);
    }
    catch (...)
    {
    }
}
