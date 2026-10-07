#include "DxCaptureTransport.h"
#include "DxTimingCollector.h"

#include <Windows.h>

#include <new>

#if !CE_DX_TIMING_CAPTURE || CE_SHIPPING
#error CreatorDxCaptureHelper requires an explicitly enabled, non-shipping Deep Capture build.
#endif

namespace
{
    bool is_unelevated_process()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return false;
        }
        TOKEN_ELEVATION elevation{};
        DWORD bytes = sizeof(elevation);
        const bool queried = GetTokenInformation(token, TokenElevation, &elevation, bytes, &bytes) != FALSE;
        CloseHandle(token);
        return queried && elevation.TokenIsElevated == 0;
    }
}

int wmain(int argumentCount, wchar_t** arguments)
{
    using namespace ce::dx_capture;

    // 인증 전에는 ETW·사용자 DLL·파일 출력 등 권한이 필요한 작업을 시작하지 않는다.
    // 정적 CRT + OS import만 사용하며 임의의 DLL 탐색 경로를 추가하지 않는다.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
    {
        return ERROR_NOT_SUPPORTED;
    }
    transport channel;
    try
    {
        if (!channel.connect(argumentCount, arguments))
        {
            return static_cast<int>(channel.last_error());
        }

        const ULONGLONG deadline = GetTickCount64() + connection_timeout_ms;
        while (GetTickCount64() < deadline && channel.parent_alive())
        {
            switch (channel.try_read_command())
            {
            case command::start:
            {
                // upstream decoder의 악성/손상 입력 검증이 끝나기 전에는 상승 실행을
                // 허용하지 않는다. 수동으로 상승한 Editor도 이 안전 경계를 넘지 못한다.
                if (!is_unelevated_process())
                {
                    record unavailable{};
                    unavailable.status = status_code::unsupported;
                    unavailable.win32_error = ERROR_NOT_SUPPORTED;
                    unavailable.process_id = channel.target_process_id();
                    channel.send_record(unavailable);
                    return ERROR_NOT_SUPPORTED;
                }
                timing_collector collector;
                return static_cast<int>(collector.run(channel));
            }
            case command::stop:
                return ERROR_SUCCESS;
            case command::disconnected:
                return ERROR_BROKEN_PIPE;
            case command::none:
                Sleep(5);
                break;
            }
        }
        return ERROR_TIMEOUT;
    }
    catch (const std::bad_alloc&)
    {
        record failure{};
        failure.status = status_code::provider_failure;
        failure.win32_error = ERROR_NOT_ENOUGH_MEMORY;
        channel.send_record(failure);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    catch (...)
    {
        record failure{};
        failure.status = status_code::provider_failure;
        failure.win32_error = ERROR_GEN_FAILURE;
        channel.send_record(failure);
        return ERROR_GEN_FAILURE;
    }
}
