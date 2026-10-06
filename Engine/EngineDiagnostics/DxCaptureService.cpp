#include "DxCaptureService.h"
#include "DxCaptureSubmission.h"

#include <chrono>
#include <exception>
#include <utility>

namespace ce::dx_capture
{
    const char* describe(process_state state)
    {
        switch (state)
        {
        case process_state::stopped: return "Stopped";
        case process_state::starting: return "Starting ordinary-privilege helper";
        case process_state::capturing: return "Capturing DX12 ETW";
        case process_state::stopping: return "Draining helper";
        case process_state::permission_denied: return "ETW permission denied";
        case process_state::elevation_cancelled: return "UAC cancelled";
        case process_state::elevation_unavailable: return "Elevation disabled pending decoder security validation";
        case process_state::untrusted_installation: return "Elevation blocked: writable or untrusted installation";
        case process_state::unavailable: return "Capture helper unavailable";
        case process_state::protocol_error: return "Capture protocol error";
        case process_state::failed: return "Capture helper failed";
        }
        return "Unknown helper state";
    }

    capture_service& deep_capture()
    {
        static capture_service instance;
        return instance;
    }

    capture_service::~capture_service()
    {
        shutdown();
    }

    bool capture_service::begin_operation(const std::filesystem::path& path, bool opening)
    {
        // UI owner serializes start/open. Join only the previous completed job.
        {
            std::lock_guard lock(mutex_);
            if (status_.busy || path.empty())
            {
                return false;
            }
        }
        if (worker_.joinable())
        {
            worker_.join();
        }
        stop_.store(false, std::memory_order_release);
        std::lock_guard lock(mutex_);
        status_ = {};
        status_.busy = true;
        status_.opening = opening;
        status_.path = path;
        status_.message = opening ? "Opening DX12 capture..." : "Starting separate capture helper...";
        snapshot_.reset();
        return true;
    }

    bool capture_service::start(const std::filesystem::path& path, bool allow_elevation, bool analyze_finalized)
    {
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        if (!begin_operation(path, false))
        {
            return false;
        }
        try
        {
            worker_ = std::thread([this, path, allow_elevation, analyze_finalized]
            {
                record_to_file(path, allow_elevation, analyze_finalized);
            });
        }
        catch (const std::exception& error)
        {
            finish_operation(error.what());
            return false;
        }
        return true;
#else
        (void)path;
        (void)allow_elevation;
        (void)analyze_finalized;
        std::lock_guard lock(mutex_);
        status_.message = "Live capture was not built. Enable EngineDxDeepCapture for a development x64 build.";
        return false;
#endif
    }

    bool capture_service::open(const std::filesystem::path& path)
    {
        if (!begin_operation(path, true))
        {
            return false;
        }
        try
        {
            worker_ = std::thread([this, path]
            {
                load_file(path);
            });
        }
        catch (const std::exception& error)
        {
            finish_operation(error.what());
            return false;
        }
        return true;
    }

    void capture_service::request_stop()
    {
        stop_.store(true, std::memory_order_release);
    }

    void capture_service::shutdown()
    {
        request_stop();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    capture_status capture_service::status() const
    {
        std::lock_guard lock(mutex_);
        return status_;
    }

    recording_snapshot_ptr capture_service::snapshot() const
    {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void capture_service::finish_operation(std::string message)
    {
        std::lock_guard lock(mutex_);
        status_.busy = false;
        status_.opening = false;
        status_.message = std::move(message);
    }

    void capture_service::load_file(const std::filesystem::path& path)
    {
        try
        {
            auto loaded = read_recording(path);
            if (!loaded)
            {
                finish_operation(std::string("Open failed: ") + describe(loaded.error()));
                return;
            }
            {
                std::lock_guard lock(mutex_);
                snapshot_ = *loaded;
                status_.recording = snapshot_->summary();
            }
            finish_operation((*loaded)->summary().complete ? "Capture opened" : "Capture opened with incomplete data");
        }
        catch (const std::exception& error)
        {
            finish_operation(std::string("Open failed: ") + error.what());
        }
        catch (...)
        {
            finish_operation("Open failed: unexpected capture error");
        }
    }

    void capture_service::record_to_file(std::filesystem::path path, bool allow_elevation, bool analyze_finalized)
    {
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        process_client client;
        submission_session submissions = 0;
        std::unique_ptr<recording_writer> writer;
        bool source_stopped = false;
        bool append_failed = false;
        std::string error;
        const auto publish_process_status = [&]
        {
            const auto process = client.status();
            std::lock_guard lock(mutex_);
            status_.process = process;
        };
        try
        {
            // No path is passed to the helper. Only this ordinary process opens the file.
            if (!client.start(allow_elevation))
            {
                publish_process_status();
                finish_operation("Capture helper could not start");
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            bool stop_sent = false;
            bool accepting_submissions = true;
            auto stop_at = start;
            const auto consume = [&](const record& value)
            {
                if (value.kind == record_kind::session)
                {
                    if (writer)
                    {
                        append_failed = true;
                        error = "Duplicate capture session rejected";
                        return;
                    }
                    auto opened = recording_writer::open(path, value);
                    if (!opened)
                    {
                        append_failed = true;
                        error = std::string("Record failed: ") + describe(opened.error());
                        return;
                    }
                    writer = std::move(*opened);
                    if (accepting_submissions && !stop_sent && !stop_.load(std::memory_order_acquire))
                    {
                        submissions = begin_submission_capture();
                    }
                }
                else if (writer)
                {
                    if (!writer->append(value))
                    {
                        append_failed = true;
                        error = "Recording limit or write error; captured prefix retained";
                    }
                    if (value.kind == record_kind::status && value.status == status_code::stopped)
                    {
                        source_stopped = (value.flags & record_flags::incomplete) == 0;
                    }
                }
            };
            for (;;)
            {
                const auto now = std::chrono::steady_clock::now();
                if (!stop_sent && (stop_.load(std::memory_order_acquire) || append_failed ||
                                   now - start >= std::chrono::seconds(60)))
                {
                    accepting_submissions = false;
                    if (submissions)
                    {
                        stop_submission_capture(submissions);
                    }
                    client.request_stop();
                    stop_sent = true;
                    stop_at = now;
                }
                record value;
                while (client.try_pop(value))
                {
                    consume(value);
                }
                if (writer && submissions)
                {
                    while (try_pop_submission(submissions, value))
                    {
                        consume(value);
                    }
                }
                const process_status process = client.status();
                {
                    std::lock_guard lock(mutex_);
                    status_.process = process;
                    status_.message = describe(process.state);
                    if (writer)
                    {
                        status_.recording = writer->status();
                    }
                }
                const bool running = process.state == process_state::starting ||
                                     process.state == process_state::capturing ||
                                     process.state == process_state::stopping;
                if (!running || (stop_sent && now - stop_at >= std::chrono::seconds(5)))
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            accepting_submissions = false;
            if (submissions)
            {
                stop_submission_capture(submissions);
            }
            client.shutdown();
            record value;
            while (client.try_pop(value))
            {
                consume(value);
            }
            if (submissions)
            {
                while (try_pop_submission(submissions, value))
                {
                    consume(value);
                }
            }
            publish_process_status();
            if (writer)
            {
                const auto process = client.status();
                const auto lost = process.dropped_records + submission_loss_count(submissions);
                if (lost)
                {
                    record loss;
                    loss.kind = record_kind::status;
                    loss.status = status_code::events_lost;
                    loss.flags = record_flags::incomplete | record_flags::transport_loss;
                    loss.loss_count = lost;
                    (void)writer->append(loss);
                }
                const bool terminal = process.state == process_state::stopped;
                {
                    std::lock_guard lock(mutex_);
                    status_.process = process;
                }
                const auto finalized = writer->finalize(terminal && source_stopped && !append_failed && lost == 0);
                if (!finalized)
                {
                    error = std::string("Finalize failed: ") + describe(finalized.error());
                }
                {
                    std::lock_guard lock(mutex_);
                    status_.recording = writer->status();
                }
                writer.reset();
                if (analyze_finalized)
                {
                    load_file(path);
                }
                else
                {
                    finish_operation(finalized ? "DX12 capture finalized for viewer analysis" : "DX12 finalization failed");
                }
                if (!error.empty())
                {
                    finish_operation(error);
                }
            }
            else
            {
                finish_operation(error.empty() ? describe(client.status().state) : error);
            }
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
        catch (...)
        {
            error = "Unexpected capture error";
        }
        if (submissions)
        {
            stop_submission_capture(submissions);
        }
        client.shutdown();
        publish_process_status();
        if (!error.empty())
        {
            finish_operation(error);
        }
#else
        (void)path;
        (void)allow_elevation;
        (void)analyze_finalized;
#endif
    }
}
