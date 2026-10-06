#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "DxCaptureFile.h"
#include "DxCaptureProcess.h"

namespace ce::dx_capture
{
    enum class capture_state : std::uint32_t
    {
        idle,
        accepted,
        in_progress,
        stopping,
        finalized,
        failed,
        permission_denied,
        unavailable
    };

    struct capture_status
    {
        std::uint64_t session_id = 0;
        capture_state state = capture_state::idle;
        bool busy = false;
        bool opening = false;
        process_status process{};
        recording_summary recording{};
        std::filesystem::path path;
        std::string message;
    };

    enum class control_error
    {
        none,
        busy,
        session_mismatch,
        unavailable,
        failed
    };

    struct control_result
    {
        bool accepted = false;
        control_error error = control_error::none;
        capture_status status;
        std::string message;
    };

    // Editor만 시작한다. 파일 I/O와 immutable index 구성은 worker에 한정한다.
    // profiler_service의 Record/Stop 세대나 .ceprof writer를 공유하지 않는다.
    class capture_service
    {
    public:
        capture_service();
        ~capture_service();
        // CLI/GT and viewer/PT share this admission boundary. No caller-selected
        // path, provider, helper or elevation; success only means queued.
        control_result start_capture();
        control_result stop_capture(std::uint64_t session_id);
        bool start(const std::filesystem::path& path, bool allow_elevation, bool analyze_finalized = true);
        bool open(const std::filesystem::path& path);
        void request_stop();
        void shutdown();
        capture_status status() const;
        recording_snapshot_ptr snapshot() const;

    private:
        // mutex_ must be held. One bounded pending operation; only the persistent
        // worker consumes it, including cleanup before admitting another session.
        bool begin_operation(const std::filesystem::path& path, bool opening, bool analyze_finalized);
        void worker_loop();
        void record_to_file(std::filesystem::path path, bool allow_elevation, bool analyze_finalized);
        void load_file(const std::filesystem::path& path);
        void finish_operation(std::string message, capture_state state);
        void publish_process_status(process_status process);

        mutable std::mutex mutex_;
        std::condition_variable wake_;
        capture_status status_;
        std::filesystem::path pending_path_;
        recording_snapshot_ptr snapshot_;
        std::uint64_t next_session_id_ = 0;
        bool closed_ = false;
        bool pending_ = false;
        bool analyze_finalized_ = false;
        std::atomic_bool stop_{ false };
        std::thread worker_;
    };

    capture_service& deep_capture();
    const char* describe(process_state state);
    const char* describe(capture_state state);
}
