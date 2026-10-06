#pragma once

#include <cstdint>
#include <memory>

#include "DxCaptureProtocol.h"

namespace ce::dx_capture
{
    enum class process_state : std::uint32_t
    {
        stopped,
        starting,
        capturing,
        stopping,
        permission_denied,
        elevation_unavailable,
        elevation_cancelled,
        untrusted_installation,
        unavailable,
        protocol_error,
        failed
    };

    struct process_status
    {
        process_state state = process_state::stopped;
        std::uint32_t win32_error = 0;
        std::uint64_t dropped_records = 0;
        bool elevated = false;
    };

    // UI/GT는 작업을 기다리지 않는다. pipe/프로세스 수명은 전용 worker가
    // 소유하고 소비자가 밀리면 고정 크기 큐에서 누락을 명시적으로 센다.
    class process_client
    {
    public:
        process_client();
        ~process_client();
        process_client(const process_client&) = delete;
        process_client& operator=(const process_client&) = delete;

        // 현재 slice는 decoder 검증 전이라 true여도 승격하지 않는다. 권한 실패는
        // elevation_unavailable로 끝나며 일반 프로파일링에는 영향을 주지 않는다.
        // 이미 실행 중이면 false다. 성공은 요청 접수이며 실제 결과는 status()에 있다.
        bool start(bool allow_elevation);
        void request_stop();
        // start와 try_pop은 같은 단일 소비자 스레드에서 직렬화한다.
        bool try_pop(record& value);
        process_status status() const;
        void shutdown();

    private:
        struct state;
        std::shared_ptr<state> state_;
    };
}
