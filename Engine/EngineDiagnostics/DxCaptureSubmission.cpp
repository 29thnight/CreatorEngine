#include "DxCaptureSubmission.h"

#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE

#include <atomic>
#include <limits>
#include <Windows.h>

namespace ce::dx_capture
{
    namespace
    {
        // Static storage and SRWLOCK have no teardown that can race late RHI
        // singleton destruction. No session-owned pointer reaches a producer.
        SRWLOCK submission_lock = SRWLOCK_INIT;
        std::array<record, maximum_queued_records> submission_records{};
        std::atomic<submission_session> active_session{ 0 };
        std::uint32_t latest_session = 0;
        std::uint32_t queue_head = 0;
        std::uint32_t queue_count = 0;
        thread_local submission_context thread_context{};

        // Tag and count share one CAS: an old producer cannot charge a new
        // session after stop/restart. The closed bit makes stop's final count
        // stable without waiting for a producer inside ExecuteCommandLists.
        // Sessions never wrap; loss saturates at INT32_MAX.
        std::atomic<std::uint64_t> tagged_loss{ 0 };
        constexpr std::uint64_t loss_mask = (std::numeric_limits<std::int32_t>::max)();
        constexpr std::uint64_t loss_closed = std::uint64_t{ 1 } << 31;
        constexpr std::uint32_t maximum_session = (std::numeric_limits<std::uint32_t>::max)();
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

        class worker_queue_lock final
        {
        public:
            worker_queue_lock() noexcept { AcquireSRWLockExclusive(&submission_lock); }
            ~worker_queue_lock() { ReleaseSRWLockExclusive(&submission_lock); }
            worker_queue_lock(const worker_queue_lock&) = delete;
            worker_queue_lock& operator=(const worker_queue_lock&) = delete;
        };

        void record_loss(submission_session session) noexcept
        {
            std::uint64_t value = tagged_loss.load(std::memory_order_relaxed);
            while ((value >> 32) == session && (value & loss_closed) == 0 && (value & loss_mask) != loss_mask)
            {
                if (tagged_loss.compare_exchange_weak(value, value + 1,
                    std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    return;
                }
            }
        }

        void publish_submission(submission_session session, const record& value) noexcept
        {
            if (active_session.load(std::memory_order_acquire) != session)
            {
                return;
            }
            if (!TryAcquireSRWLockExclusive(&submission_lock))
            {
                record_loss(session);
                return;
            }
            if (active_session.load(std::memory_order_relaxed) == session)
            {
                if (queue_count < maximum_queued_records)
                {
                    const std::uint32_t tail = (queue_head + queue_count) % maximum_queued_records;
                    submission_records[tail] = value;
                    ++queue_count;
                }
                else
                {
                    record_loss(session);
                }
            }
            ReleaseSRWLockExclusive(&submission_lock);
        }
    }

    submission_session begin_submission_capture() noexcept
    {
        const worker_queue_lock lock;
        if (active_session.load(std::memory_order_relaxed) != 0 || latest_session == maximum_session)
        {
            return 0;
        }
        ++latest_session;
        queue_head = 0;
        queue_count = 0;
        tagged_loss.store(static_cast<std::uint64_t>(latest_session) << 32, std::memory_order_relaxed);
        active_session.store(latest_session, std::memory_order_release);
        return latest_session;
    }

    void stop_submission_capture(submission_session session) noexcept
    {
        if (session == 0)
        {
            return;
        }
        submission_session expected = session;
        if (active_session.compare_exchange_strong(expected, 0, std::memory_order_acq_rel))
        {
            tagged_loss.fetch_or(loss_closed, std::memory_order_relaxed);
            // Only the session worker waits here. After this returns no record
            // from an in-flight scope can be appended to this stopped session.
            const worker_queue_lock lock;
        }
    }

    bool try_pop_submission(submission_session session, record& value) noexcept
    {
        const worker_queue_lock lock;
        if (session == 0 || session != latest_session || queue_count == 0)
        {
            return false;
        }
        value = submission_records[queue_head];
        queue_head = (queue_head + 1) % maximum_queued_records;
        --queue_count;
        return true;
    }

    std::uint64_t submission_loss_count(submission_session session) noexcept
    {
        const std::uint64_t value = tagged_loss.load(std::memory_order_relaxed);
        return session != 0 && (value >> 32) == session ? value & loss_mask : 0;
    }

    submission_context current_submission_context() noexcept
    {
        return thread_context;
    }

    submission_context_scope::submission_context_scope(submission_context context) noexcept
        : previous_(thread_context)
    {
        thread_context = context;
    }

    submission_context_scope::~submission_context_scope()
    {
        thread_context = previous_;
    }

    submission_scope::submission_scope(const void* api_queue, std::uint32_t command_list_count,
        submission_context context) noexcept
        : session_(active_session.load(std::memory_order_acquire))
    {
        if (session_ == 0)
        {
            return;
        }
        thread_id_ = GetCurrentThreadId();
        api_queue_id_ = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(api_queue));
        command_list_count_ = command_list_count;
        engine_frame_id_ = context.engine_frame_id;
        submission_id_ = context.submission_id;
        render_view_id_ = context.render_view_id;
        has_query_token_ = context.has_query_token;
        LARGE_INTEGER begin{};
        if (!QueryPerformanceCounter(&begin))
        {
            record_loss(session_);
            session_ = 0;
            return;
        }
        qpc_begin_ = static_cast<std::uint64_t>(begin.QuadPart);
        // cpu_submit_qpc is intentionally not GpuFrameToken::cpuSubmitTick.
        // Preserve that query-recording timestamp and its existing semantics.
    }

    submission_scope::~submission_scope()
    {
        if (session_ == 0)
        {
            return;
        }
        LARGE_INTEGER end{};
        if (!QueryPerformanceCounter(&end) || static_cast<std::uint64_t>(end.QuadPart) < qpc_begin_)
        {
            record_loss(session_);
            return;
        }
        record value{};
        value.kind = record_kind::submission;
        value.flags = has_query_token_ ? engine_correlated : no_flags;
        value.qpc_begin = qpc_begin_;
        value.qpc_end = static_cast<std::uint64_t>(end.QuadPart);
        value.process_id = GetCurrentProcessId();
        value.thread_id = thread_id_;
        value.api_queue_id = api_queue_id_;
        value.command_list_count = command_list_count_;
        value.engine_frame_id = engine_frame_id_;
        value.submission_id = submission_id_;
        value.render_view_id = render_view_id_;
        publish_submission(session_, value);
    }
}

#endif
