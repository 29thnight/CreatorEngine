#pragma once

#include "DxCaptureProtocol.h"

#ifndef CE_DX_TIMING_CAPTURE
#define CE_DX_TIMING_CAPTURE 0
#endif

namespace ce::dx_capture
{
    using submission_session = std::uint64_t;

    // Only the renderer that owns a GpuFrameToken can assert has_query_token.
    // A zero submission_id can be a valid first query submission, not just unknown.
    struct submission_context
    {
        std::uint64_t engine_frame_id = 0;
        std::uint64_t submission_id = 0;
        std::uint64_t render_view_id = 0;
        bool has_query_token = false;
    };

#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
    // A single session worker owns start/stop/drain. The producer never waits,
    // allocates, calls the helper, or touches the profiler's current frame.
    // Zero means unavailable/already active. Stop preserves accepted records for
    // draining; a subsequent start clears them and rejects old in-flight scopes.
    submission_session begin_submission_capture() noexcept;
    void stop_submission_capture(submission_session session) noexcept;
    bool try_pop_submission(submission_session session, record& value) noexcept;
    std::uint64_t submission_loss_count(submission_session session) noexcept;

    submission_context current_submission_context() noexcept;

    class submission_context_scope final
    {
    public:
        explicit submission_context_scope(submission_context context) noexcept;
        ~submission_context_scope();
        submission_context_scope(const submission_context_scope&) = delete;
        submission_context_scope& operator=(const submission_context_scope&) = delete;

    private:
        submission_context previous_{};
    };

    // Keep this scope immediately around ExecuteCommandLists, before Signal.
    // The QPC boundaries measure that CPU API call, not GPU execution duration.
    class submission_scope final
    {
    public:
        submission_scope(const void* api_queue, std::uint32_t command_list_count,
            submission_context context = {}) noexcept;
        ~submission_scope();
        submission_scope(const submission_scope&) = delete;
        submission_scope& operator=(const submission_scope&) = delete;

    private:
        submission_session session_ = 0;
        // Leave payload storage uninitialized on the disabled fast path.
        std::uint64_t qpc_begin_;
        std::uint64_t api_queue_id_;
        std::uint64_t engine_frame_id_;
        std::uint64_t submission_id_;
        std::uint64_t render_view_id_;
        std::uint32_t command_list_count_;
        std::uint32_t thread_id_;
        bool has_query_token_;
    };
#else
    inline submission_session begin_submission_capture() noexcept { return 0; }
    inline void stop_submission_capture(submission_session) noexcept {}
    inline bool try_pop_submission(submission_session, record&) noexcept { return false; }
    inline std::uint64_t submission_loss_count(submission_session) noexcept { return 0; }
    inline submission_context current_submission_context() noexcept { return {}; }

    class submission_context_scope final
    {
    public:
        explicit submission_context_scope(submission_context) noexcept {}
        submission_context_scope(const submission_context_scope&) = delete;
        submission_context_scope& operator=(const submission_context_scope&) = delete;
    };

    class submission_scope final
    {
    public:
        submission_scope(const void*, std::uint32_t, submission_context = {}) noexcept {}
        submission_scope(const submission_scope&) = delete;
        submission_scope& operator=(const submission_scope&) = delete;
    };
#endif
}
