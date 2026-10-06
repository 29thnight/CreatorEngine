#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "DxCaptureFile.h"
#include "DxCaptureProcess.h"

namespace ce::dx_capture
{
    struct capture_status
    {
        bool busy = false;
        bool opening = false;
        process_status process{};
        recording_summary recording{};
        std::filesystem::path path;
        std::string message;
    };

    // Editor만 시작한다. 파일 I/O와 immutable index 구성은 worker에 한정한다.
    // profiler_service의 Record/Stop 세대나 .ceprof writer를 공유하지 않는다.
    class capture_service
    {
    public:
        ~capture_service();
        bool start(const std::filesystem::path& path, bool allow_elevation);
        bool open(const std::filesystem::path& path);
        void request_stop();
        void shutdown();
        capture_status status() const;
        recording_snapshot_ptr snapshot() const;

    private:
        bool begin_operation(const std::filesystem::path& path, bool opening);
        void record_to_file(std::filesystem::path path, bool allow_elevation);
        void load_file(const std::filesystem::path& path);
        void finish_operation(std::string message);

        mutable std::mutex mutex_;
        capture_status status_;
        recording_snapshot_ptr snapshot_;
        std::atomic_bool stop_{ false };
        std::thread worker_;
    };

    capture_service& deep_capture();
    const char* describe(process_state state);
}
