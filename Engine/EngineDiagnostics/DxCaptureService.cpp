#include "DxCaptureService.h"
#include "DxCaptureSubmission.h"

#include <chrono>
#include <exception>
#include <limits>
#include <utility>

#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")
#endif

namespace ce::dx_capture
{
    namespace
    {
        capture_state failure_state(process_state state)
        {
            switch (state)
            {
            case process_state::permission_denied:
            case process_state::elevation_unavailable:
            case process_state::elevation_cancelled:
                return capture_state::permission_denied;
            case process_state::unavailable:
            case process_state::untrusted_installation:
                return capture_state::unavailable;
            default:
                return capture_state::failed;
            }
        }

#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        std::filesystem::path spool_path()
        {
            wchar_t temporary[MAX_PATH]{};
            const auto length = GetTempPathW(MAX_PATH, temporary);
            session_nonce nonce{};
            if (!length || length >= MAX_PATH ||
                BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            {
                return {};
            }
            std::wstring name = L"CreatorEngine-DX-";
            constexpr wchar_t digits[] = L"0123456789abcdef";
            for (const auto byte : nonce)
            {
                name.push_back(digits[byte >> 4]);
                name.push_back(digits[byte & 15]);
            }
            // Only the ordinary engine writer opens this generated name, using
            // noreplace. No helper/CLI/viewer supplies any filesystem input.
            return std::filesystem::path(temporary) / (name + L".cedx");
        }
#endif
    }

    const char* describe(capture_state state)
    {
        switch (state)
        {
        case capture_state::idle: return "idle";
        case capture_state::accepted: return "accepted";
        case capture_state::in_progress: return "in_progress";
        case capture_state::stopping: return "stopping";
        case capture_state::finalized: return "finalized";
        case capture_state::failed: return "failed";
        case capture_state::permission_denied: return "permission_denied";
        case capture_state::unavailable: return "unavailable";
        }
        return "failed";
    }

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

    capture_service::capture_service()
    {
#if !CE_DX_TIMING_CAPTURE || CE_SHIPPING
        status_.state = capture_state::unavailable;
        status_.process.state = process_state::unavailable;
        status_.message = "Live capture was not built. Enable EngineDxDeepCapture for a development x64 build.";
#endif
    }

    capture_service::~capture_service()
    {
        shutdown();
    }

    bool capture_service::begin_operation(const std::filesystem::path& path, bool opening, bool analyze_finalized)
    {
        if (closed_ || status_.busy || path.empty() ||
            (!opening && next_session_id_ == std::numeric_limits<std::uint64_t>::max()))
        {
            return false;
        }
        capture_status next;
        next.busy = true;
        next.opening = opening;
        next.path = path;
        if (!opening)
        {
            next.session_id = next_session_id_ + 1;
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
            if (!next_session_id_)
            {
                // A fresh process does not recycle a small session ID that an
                // old HTTP client could accidentally stop after an engine restart.
                if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&next.session_id),
                                    static_cast<ULONG>(sizeof(next.session_id)), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
                    !next.session_id || next.session_id == std::numeric_limits<std::uint64_t>::max())
                {
                    return false;
                }
            }
#endif
        }
        next.state = opening ? capture_state::in_progress : capture_state::accepted;
        next.message = opening ? "Opening DX12 capture..." : "DX12 capture request accepted; helper has not confirmed capture";
        auto pending_path = path;
        // The worker is created once under the admission lock. Completed work
        // remains on this same thread; neither GT nor PT ever joins a prior job.
        try
        {
            if (!worker_.joinable())
            {
                worker_ = std::thread([this] { worker_loop(); });
            }
        }
        catch (const std::exception& error)
        {
            next.busy = false;
            next.session_id = 0;
            next.state = capture_state::failed;
            next.message = error.what();
            status_ = std::move(next);
            return false;
        }
        stop_.store(false, std::memory_order_release);
        status_ = std::move(next);
        pending_path_ = std::move(pending_path);
        if (!opening)
        {
            next_session_id_ = status_.session_id;
        }
        analyze_finalized_ = analyze_finalized;
        pending_ = true;
        snapshot_.reset();
        wake_.notify_one();
        return true;
    }

    control_result capture_service::start_capture()
    {
        std::lock_guard lock(mutex_);
        if (closed_)
        {
            return {false, control_error::unavailable, status_, "Deep capture service has shut down"};
        }
        if (status_.busy)
        {
            return {false, control_error::busy, status_, "A deep capture operation is already active"};
        }
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        try
        {
            const auto path = spool_path();
            if (path.empty())
            {
                return {false, control_error::failed, status_, "Cannot allocate an engine-owned DX12 capture spool path"};
            }
            if (!begin_operation(path, false, false))
            {
                return {false, control_error::failed, status_, "Cannot admit the DX12 capture worker"};
            }
            return {true, control_error::none, status_, "DX12 capture request accepted; poll profile.deep.status for collector progress"};
        }
        catch (const std::exception& error)
        {
            return {false, control_error::failed, status_, error.what()};
        }
#else
        return {false, control_error::unavailable, status_,
                "Live capture was not built. Enable EngineDxDeepCapture for a development x64 build."};
#endif
    }

    control_result capture_service::stop_capture(std::uint64_t session_id)
    {
        std::lock_guard lock(mutex_);
        if (!session_id || session_id != status_.session_id || status_.opening)
        {
            return {false, control_error::session_mismatch, status_, "Stop requires the current nonzero deep capture session ID"};
        }
        if (!status_.busy)
        {
            return {true, control_error::none, status_, "This deep capture session has already finished"};
        }
        stop_.store(true, std::memory_order_release);
        if (status_.state == capture_state::accepted || status_.state == capture_state::in_progress)
        {
            status_.state = capture_state::stopping;
        }
        return {true, control_error::none, status_, "Stop requested; collector drain and file finalization are asynchronous"};
    }

    bool capture_service::start(const std::filesystem::path& path, bool allow_elevation, bool analyze_finalized)
    {
        // Compatibility callers still use the same per-service admission gate.
        // Elevation remains disabled even when requested by an old caller.
        (void)allow_elevation;
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        std::lock_guard lock(mutex_);
        return begin_operation(path, false, analyze_finalized);
#else
        (void)path;
        (void)analyze_finalized;
        return false;
#endif
    }

    bool capture_service::open(const std::filesystem::path& path)
    {
        std::lock_guard lock(mutex_);
        return begin_operation(path, true, false);
    }

    void capture_service::worker_loop()
    {
        std::unique_lock lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [this] { return closed_ || pending_; });
            if (!pending_)
            {
                return;
            }
            auto path = std::move(pending_path_);
            const bool opening = status_.opening;
            const bool analyze_finalized = analyze_finalized_;
            pending_ = false;
            lock.unlock();
            try
            {
                if (opening)
                {
                    load_file(path);
                }
                else
                {
                    record_to_file(std::move(path), false, analyze_finalized);
                }
            }
            catch (const std::exception& error)
            {
                finish_operation(error.what(), capture_state::failed);
            }
            catch (...)
            {
                finish_operation("Unexpected deep capture worker failure", capture_state::failed);
            }
            lock.lock();
            // Keep admission closed until every old writer/helper is destroyed.
            // A new session can never be overwritten by the previous cleanup.
            status_.busy = false;
            status_.opening = false;
        }
    }

    void capture_service::request_stop()
    {
        std::lock_guard lock(mutex_);
        stop_.store(true, std::memory_order_release);
        if (status_.busy && !status_.opening &&
            (status_.state == capture_state::accepted || status_.state == capture_state::in_progress))
        {
            status_.state = capture_state::stopping;
        }
    }

    void capture_service::shutdown()
    {
        std::thread worker;
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            stop_.store(true, std::memory_order_release);
            worker = std::move(worker_);
            wake_.notify_one();
        }
        // Lifecycle owner only. Neither start/stop/status nor either command
        // transport enters this blocking shutdown path.
        if (worker.joinable())
        {
            worker.join();
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

    void capture_service::finish_operation(std::string message, capture_state state)
    {
        std::lock_guard lock(mutex_);
        status_.state = state;
        status_.message = std::move(message);
    }

    void capture_service::publish_process_status(process_status process)
    {
        std::lock_guard lock(mutex_);
        status_.process = process;
        switch (process.state)
        {
        case process_state::starting:
            status_.state = stop_.load(std::memory_order_acquire) ? capture_state::stopping : capture_state::accepted;
            break;
        case process_state::capturing:
            status_.state = stop_.load(std::memory_order_acquire) ? capture_state::stopping : capture_state::in_progress;
            break;
        case process_state::stopping:
        case process_state::stopped:
            status_.state = capture_state::stopping;
            break;
        default:
            status_.state = failure_state(process.state);
            break;
        }
        status_.message = describe(process.state);
    }

    void capture_service::load_file(const std::filesystem::path& path)
    {
        try
        {
            auto loaded = read_recording(path);
            if (!loaded)
            {
                finish_operation(std::string("Open failed: ") + describe(loaded.error()), capture_state::failed);
                return;
            }
            {
                std::lock_guard lock(mutex_);
                snapshot_ = *loaded;
                status_.recording = snapshot_->summary();
            }
            finish_operation((*loaded)->summary().complete ? "Capture opened" : "Capture opened with incomplete data",
                             (*loaded)->summary().finalized ? capture_state::finalized : capture_state::failed);
        }
        catch (const std::exception& error)
        {
            finish_operation(std::string("Open failed: ") + error.what(), capture_state::failed);
        }
        catch (...)
        {
            finish_operation("Open failed: unexpected capture error", capture_state::failed);
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
        capture_state outcome = capture_state::failed;
        std::string message;
        try
        {
            // No path is passed to the helper. Only this ordinary process opens the file.
            if (!client.start(allow_elevation))
            {
                const auto process = client.status();
                publish_process_status(process);
                finish_operation("Capture helper could not start", failure_state(process.state));
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
                    stop_.store(true, std::memory_order_release);
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
                publish_process_status(process);
                {
                    std::lock_guard lock(mutex_);
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
            publish_process_status(client.status());
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
                    if (!writer->append(loss))
                    {
                        append_failed = true;
                        error = "Loss record could not be written; captured prefix retained";
                    }
                }
                const bool terminal = process.state == process_state::stopped;
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
                outcome = !error.empty() || !finalized ? capture_state::failed :
                    terminal ? capture_state::finalized : failure_state(process.state);
                message = outcome == capture_state::finalized ? "DX12 capture artifact finalized" : describe(process.state);
            }
            else
            {
                outcome = failure_state(client.status().state);
                message = client.status().state == process_state::stopped ?
                    "Capture stopped before producing an artifact" : describe(client.status().state);
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
        publish_process_status(client.status());
        if (writer)
        {
            // A thrown write retains only its recoverable prefix. Never report
            // an artifact as finalized based on the writer destructor's fallback.
            std::lock_guard lock(mutex_);
            status_.recording = writer->status();
        }
        if (!error.empty())
        {
            finish_operation(error, capture_state::failed);
        }
        else if (analyze_finalized && outcome == capture_state::finalized)
        {
            load_file(path);
        }
        else
        {
            finish_operation(std::move(message), outcome);
        }
#else
        (void)path;
        (void)allow_elevation;
        (void)analyze_finalized;
        finish_operation("Live DX12 capture is unavailable in this build", capture_state::unavailable);
#endif
    }
}
