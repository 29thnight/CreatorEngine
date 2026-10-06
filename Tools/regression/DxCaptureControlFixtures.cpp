// Source fixture only: UNEXECUTED. For an isolated Windows test executable,
// compile this file with the real DxCaptureService.cpp and DxCaptureFile.cpp.
// Build both CE_DX_TIMING_CAPTURE=1/CE_SHIPPING=0 and the disabled variant.
// Do NOT link DxCaptureProcess.cpp, DxCaptureSubmission.cpp or the ETW SDK:
// this file supplies a deterministic collector, never a real helper process.
#include "DxCaptureService.h"
#include "DxCaptureSubmission.h"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <utility>

namespace dx_capture_control_fixtures
{
    using namespace ce::dx_capture;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "DxCaptureControlFixtures: %s\n", message);
            std::abort();
        }
    }

    std::atomic_bool allow_capture{false};
    std::atomic<process_state> collector_result{process_state::capturing};

    template<class Predicate>
    capture_status await_status(capture_service& service, Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;)
        {
            const auto status = service.status();
            if (predicate(status))
            {
                return status;
            }
            check(std::chrono::steady_clock::now() < deadline, "bounded fake-collector wait completed");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
namespace ce::dx_capture
{
    struct process_client::state
    {
        process_state result = process_state::capturing;
        bool stop = false;
        unsigned next = 0;
    };

    process_client::process_client() : state_(std::make_shared<state>()) {}
    process_client::~process_client() = default;

    bool process_client::start(bool allow_elevation)
    {
        dx_capture_control_fixtures::check(!allow_elevation, "shared controls never request elevation");
        state_->result = dx_capture_control_fixtures::collector_result.load();
        return true;
    }

    void process_client::request_stop() { state_->stop = true; }
    void process_client::shutdown() { state_->stop = true; }

    bool process_client::try_pop(record& value)
    {
        if (state_->result != process_state::capturing || !dx_capture_control_fixtures::allow_capture.load())
        {
            return false;
        }
        value = {};
        value.process_id = 123;
        value.qpc_begin = 1000;
        if (!state_->next)
        {
            value.kind = record_kind::session;
            value.qpc_frequency = 10000000;
            value.process_creation_time = 456789;
            value.windows_session_id = 2;
            value.flags = raw_api_marker_ids | pix_markers_unavailable;
            ++state_->next;
            return true;
        }
        if (state_->stop && state_->next == 1)
        {
            value.kind = record_kind::status;
            value.status = status_code::stopped;
            value.qpc_begin = 2500;
            ++state_->next;
            return true;
        }
        return false;
    }

    process_status process_client::status() const
    {
        process_status result;
        if (state_->result != process_state::capturing)
        {
            result.state = state_->result;
            result.win32_error = state_->result == process_state::elevation_unavailable ? 5 : 1;
        }
        else if (state_->stop && (state_->next == 2 || !dx_capture_control_fixtures::allow_capture.load()))
        {
            result.state = process_state::stopped;
        }
        else
        {
            result.state = state_->stop ? process_state::stopping :
                dx_capture_control_fixtures::allow_capture.load() ? process_state::capturing : process_state::starting;
        }
        return result;
    }

    submission_session begin_submission_capture() noexcept { return 0; }
    void stop_submission_capture(submission_session) noexcept {}
    bool try_pop_submission(submission_session, record&) noexcept { return false; }
    std::uint64_t submission_loss_count(submission_session) noexcept { return 0; }
}
#endif

namespace dx_capture_control_fixtures
{
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
    void concurrent_admission_and_session_stop()
    {
        allow_capture.store(false);
        collector_result.store(process_state::capturing);
        capture_service service;
        std::array<control_result, 2> results;
        std::barrier start_line(3);
        std::thread cli([&]
        {
            start_line.arrive_and_wait();
            results[0] = service.start_capture();
        });
        std::thread viewer([&]
        {
            start_line.arrive_and_wait();
            results[1] = service.start_capture();
        });
        start_line.arrive_and_wait();
        cli.join();
        viewer.join();
        check(results[0].accepted != results[1].accepted, "exactly one concurrent producer wins admission");
        const auto& accepted = results[results[0].accepted ? 0 : 1];
        const auto& rejected = results[results[0].accepted ? 1 : 0];
        check(accepted.status.state == capture_state::accepted && accepted.status.session_id != 0,
              "admission is not collector startup");
        check(rejected.error == control_error::busy && rejected.status.session_id == accepted.status.session_id,
              "the loser observes the same admitted session");
        check(accepted.status.path.is_absolute() && accepted.status.path.extension() == ".cedx",
              "Start publishes the engine-owned artifact path");
        check(!service.stop_capture(0).accepted, "zero never means stop current capture");
        check(!service.stop_capture(accepted.status.session_id + 1).accepted, "a foreign session cannot stop capture");
        check(service.status().state == capture_state::accepted, "invalid stops preserve the current status");
        check(service.stop_capture(accepted.status.session_id).accepted, "exact session accepts stop during startup");
        check(service.stop_capture(accepted.status.session_id).accepted, "duplicate stop is accepted");
        const auto finished = await_status(service, [](const auto& status) { return !status.busy; });
        check(finished.state == capture_state::failed && !finished.recording.finalized,
              "cancellation before a session cannot invent a finalized artifact");
        check(service.stop_capture(accepted.status.session_id).accepted, "terminal stop is idempotent");
        service.shutdown();
        check(!service.start_capture().accepted, "shutdown closes future admission");
    }

    void successful_finalization_and_stale_stop()
    {
        allow_capture.store(true);
        collector_result.store(process_state::capturing);
        capture_service service;
        const auto first = service.start_capture();
        check(first.accepted, "first request admitted");
        await_status(service, [](const auto& status)
        {
            return status.state == capture_state::in_progress && status.recording.record_count != 0;
        });
        check(service.stop_capture(first.status.session_id).accepted, "first stop admitted");
        const auto finished = await_status(service, [](const auto& status) { return !status.busy; });
        check(finished.state == capture_state::finalized && finished.recording.finalized,
              "finalized follows actual collector stop and writer footer");
        check(read_recording(finished.path).has_value(), "the advertised finalized artifact is readable");
        allow_capture.store(false);
        const auto second = service.start_capture();
        check(second.accepted && second.status.session_id > first.status.session_id, "worker is reused with a new session");
        check(!service.stop_capture(first.status.session_id).accepted, "old stop cannot affect the next capture");
        check(service.status().state == capture_state::accepted, "stale stop does not mutate the new capture");
        check(service.stop_capture(second.status.session_id).accepted, "new exact session can stop");
        await_status(service, [](const auto& status) { return !status.busy; });
        service.shutdown();
        std::filesystem::remove(finished.path);
    }

    void actual_collector_failures()
    {
        constexpr std::array cases{
            std::pair{process_state::elevation_unavailable, capture_state::permission_denied},
            std::pair{process_state::unavailable, capture_state::unavailable},
            std::pair{process_state::protocol_error, capture_state::failed},
            std::pair{process_state::failed, capture_state::failed}
        };
        capture_service service;
        for (const auto [source, expected] : cases)
        {
            collector_result.store(source);
            const auto started = service.start_capture();
            check(started.accepted && started.status.state == capture_state::accepted,
                  "admission does not predict the collector outcome");
            const auto failed = await_status(service, [](const auto& status) { return !status.busy; });
            check(failed.state == expected && failed.process.state == source && failed.process.win32_error != 0,
                  "terminal status preserves the real collector result and OS error");
            check(!failed.recording.finalized && service.stop_capture(failed.session_id).accepted,
                  "failed session is not finalized and repeated stop is harmless");
        }
    }
#else
    void disabled_build_is_unavailable()
    {
        capture_service service;
        const auto result = service.start_capture();
        check(!result.accepted && result.error == control_error::unavailable &&
              result.status.state == capture_state::unavailable && !result.status.busy && !result.status.session_id,
              "SDK-disabled/Shipping path rejects before any helper or worker is created");
        check(!service.stop_capture(1).accepted, "disabled service cannot stop a fabricated session");
    }
#endif
}

int main()
{
    using namespace dx_capture_control_fixtures;
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
    concurrent_admission_and_session_stop();
    successful_finalization_and_stale_stop();
    actual_collector_failures();
#else
    disabled_build_is_unavailable();
#endif
    return 0;
}
