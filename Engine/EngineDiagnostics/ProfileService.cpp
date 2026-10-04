#include "ProfileService.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>
#include <thread>

#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <utility>

// 유니티 빌드 때문에 익명 네임스페이스를 쓰지 않는다(계획서 §14).
namespace ce::detail::profile_service_impl
{
    // 서비스 슬롯을 나눠 준다. 서비스가 살아 있는 동안만 자리를 쥔다.
    struct slot_table
    {
        std::mutex lock;
        bool       taken[kMaxLiveServices]{};
    };

    inline slot_table& slots()
    {
        static slot_table instance;
        return instance;
    }

    inline std::uint32_t acquire_slot()
    {
        slot_table& table = slots();
        std::lock_guard<std::mutex> guard(table.lock);
        for (std::uint32_t i = 0; i < kMaxLiveServices; ++i)
        {
            if (!table.taken[i])
            {
                table.taken[i] = true;
                return i;
            }
        }
        // 자리가 없으면 마지막 자리를 함께 쓴다. 조용히 덮어쓰는 것보다
        // 나쁘지만, 여기까지 오면 이미 설계 밖이다.
        return kMaxLiveServices - 1;
    }

    inline void release_slot(std::uint32_t index)
    {
        slot_table& table = slots();
        std::lock_guard<std::mutex> guard(table.lock);
        if (index < kMaxLiveServices)
        {
            table.taken[index] = false;
        }
    }

    // ★ 서비스마다 한 칸. 옛 코어처럼 하나를 공유하지 않으므로 검사용
    //   서비스가 라이브 캡처를 교란하지 않는다.
    // ★ 포인터만 두면 **남의 스레드의 자리가 매달린다.** shutdown 은 부른
    //   스레드의 자리만 끊을 수 있고, 다른 스레드의 thread_local 은 죽은
    //   스트림을 계속 가리킨다. 그래서 자리마다 세대를 같이 적어 두고, 서비스가
    //   들고 있는 세대와 다르면 **없는 것으로 읽는다.**
    struct tls_slot
    {
        thread_stream* stream = nullptr;
        std::uint64_t  epoch = 0;
    };
    thread_local tls_slot t_streams[kMaxLiveServices] = {};

    // 서비스 인스턴스마다, 그리고 shutdown 마다 새로 나가는 번호.
    std::atomic<std::uint64_t> g_slotEpoch{ 1 };

    // ★ 주인이 아직 도는 스트림은 파괴할 수 없다. 여기에 스트림과 **그 풀**을
    //   함께 놓아 둔다 — 풀을 먼저 접으면 놓아 둔 의미가 없다. 일부러 해제하지
    //   않는다: 종료 전에 unregister_thread 를 부르지 않은 스레드가 있다는
    //   뜻이고, 그 스레드는 여전히 돌고 있다.
    struct retained_stream
    {
        std::unique_ptr<thread_stream> stream;
        std::shared_ptr<chunk_pool>    pool;
    };

    std::mutex& retained_lock()
    {
        static std::mutex* lock = new std::mutex();
        return *lock;
    }

    std::vector<retained_stream>& retained_streams()
    {
        static std::vector<retained_stream>* list = new std::vector<retained_stream>();
        return *list;
    }
}

namespace ce
{
    using namespace ce::detail::profile_service_impl;

    profiler_service::profiler_service()
        : m_serviceSlot(acquire_slot())
        , m_slotEpoch(g_slotEpoch.fetch_add(1, std::memory_order_relaxed) + 1)
    {
    }

    profiler_service::~profiler_service()
    {
        shutdown();
        release_slot(m_serviceSlot);
    }

    profile_tick profiler_service::ticks_per_second()
    {
        static const profile_tick frequency = []() -> profile_tick
        {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            return static_cast<profile_tick>(value.QuadPart);
        }();
        return frequency;
    }

    profile_tick profiler_service::now()
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return static_cast<profile_tick>(value.QuadPart);
    }

    void profiler_service::initialize(const profiler_config& config)
    {
        if (m_initialized.load(std::memory_order_acquire))
        {
            return;
        }

        m_liveCaptureIntervalMs = config.live_capture_interval_ms;
        m_collectorTiming = {};
        m_maxQueuedFrames = config.max_queued_frames > 0 ? config.max_queued_frames : 600;
        m_memoryBudget = config.memory_budget > 0 ? config.memory_budget : kDefaultMemoryBudget;
        m_accountedSourceDropped = m_retiredDropped.load(std::memory_order_relaxed)
            + m_queueDroppedEvents.load(std::memory_order_relaxed);
        m_queueDroppedCounters.store(0, std::memory_order_relaxed);
        m_accountedCounterQueueDropped = 0;
        m_pendingGapTick.store(0, std::memory_order_relaxed);
        m_droppedFrameBaseline.store(
            m_droppedFrameBoundaries.load(std::memory_order_relaxed), std::memory_order_relaxed);
        // 보존 링의 예산은 생산자 큐의 상한이 아니다. 수집이 늦으면
        // 제출 작업과 페이지가 자라며, 캡처 보존 시에만 오래된 프레임을 정리한다.
        m_maxChunkCount = config.max_chunk_count > 0 ? config.max_chunk_count : 8192;
        m_pool->initialize(config.chunk_count, m_maxChunkCount);
        while (m_workSignal.try_acquire())
        {
        }
        m_pool->set_signal(&m_workSignal);
        m_ring.configure(config.retained_frames, config.memory_budget);
        m_ring.set_archive_enabled(true);
        m_frameBeginTick = now();
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            m_stopWorker = false;
        }
        m_initialized.store(true, std::memory_order_release);
        m_collectorWorker = std::thread([this] { collector_loop(); });
        m_collectorThread.store(m_collectorWorker.get_id(), std::memory_order_release);
    }

    void profiler_service::shutdown()
    {
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        m_state.store(recorder_state::stopped, std::memory_order_release);
        m_pool->set_signal(nullptr);
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            m_stopWorker = true;
        }
        m_workSignal.release();
        if (m_collectorWorker.joinable())
        {
            m_collectorWorker.join();
        }
        m_controlAppliedCv.notify_all();

        std::string abandonedNames;
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (std::size_t i = 0; i < m_streams.size(); ++i)
            {
                // 놓아 두게 될 것의 이름을 먼저 적는다. 표를 비운 뒤에는 어느
                // 스레드였는지 알 길이 없고, 그러면 수만 남아 고칠 자리를
                // 못 짚는다.
                const bool willAbandon = m_streams[i].stream &&
                    (m_streams[i].stream->owner_thread() != std::this_thread::get_id());
                if (willAbandon && i < m_threadInfo.size())
                {
                    if (!abandonedNames.empty())
                    {
                        abandonedNames += ", ";
                    }
                    abandonedNames += m_threadInfo[i].name;
                }

                destroy_stream_locked(i);
            }
            m_streams.clear();

            // 표가 비었으므로 레인 포인터도 놓는다. 안 놓으면 다음 초기화까지
            // 죽은 스트림을 가리킨 채로 남는다.
            m_gpuStream.store(nullptr, std::memory_order_release);
        }

        // 봉인된 것을 마저 거둔 뒤 풀을 접는다. 여기서 빼먹으면 종료 직전
        // 프레임이 통째로 사라진다.
        finalizing_recording_ = !recording_finalized_;
        collect_sealed();
        if (!recording_finalized_)
        {
            const profile_tick end = now();
            if (end > m_frameBeginTick)
            {
                const auto tailFrame = m_firstRecordBoundaryPending
                    ? m_engineFrame.load(std::memory_order_relaxed) : m_lastEngineFrame + 1;
                close_frame(tailFrame, m_frameBeginTick, end);
            }
            m_ring.discard_pending();
            finalize_recording(false, static_cast<std::uint32_t>(m_abandonedStreams.load()));
        }
        if (recording_writer_)
        {
            recording_writer_->wait();
        }
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            m_threadInfo.clear();
        }

        // 얼린 캡처와 주인이 아직 살아 있는 스트림은 옛 풀의 페이지를
        // 붙잡을 수 있다. 풀을 강제로 비우지 않고 새 서비스용 풀로 바꾼다.
        // 마지막 페이지·스트림 참조가 사라질 때 옛 풀이 저절로 정리된다.
        m_pool = std::make_shared<chunk_pool>();

        {
            std::lock_guard<std::mutex> guard(m_captureLock);
            m_capture.reset();
        }
        m_ring.clear();
        publish_ring_stats();

        // ★ 세대를 올린다. 다른 스레드의 thread_local 자리는 여기서 끊을 수
        //   없으므로, 끊는 대신 **읽히지 않게** 한다.
        m_slotEpoch.store(g_slotEpoch.fetch_add(1, std::memory_order_relaxed) + 1,
                          std::memory_order_release);
        m_collectorThread.store(std::thread::id{}, std::memory_order_release);
        m_collectorOsThreadId.store(0, std::memory_order_release);

        {
            std::lock_guard<std::mutex> guard(m_workLock);
            m_workQueue.clear();
            m_queuedFrames = 0;
        }

        // ★ 종료가 어떻게 끝났는지 **밖에서 읽을 수 있어야** 한다. 놓아 둔
        //   스트림 수는 요약에 있지만, 요약을 읽을 수 있는 시점에는 이미
        //   서비스가 내려가 있다. 한 줄을 남긴다 — 0 이 아니면 종료 전에
        //   unregister_thread 를 부르지 않은 스레드가 있다는 뜻이다.
        std::fprintf(stderr,
            "[profiler] shutdown abandoned=%llu retained=%zu foreign=%llu%s%s\n",
            static_cast<unsigned long long>(m_abandonedStreams.load(std::memory_order_relaxed)),
            retained_stream_count(),
            static_cast<unsigned long long>(m_retiredForeign.load(std::memory_order_relaxed)),
            abandonedNames.empty() ? "" : " - ",
            abandonedNames.c_str());
        std::fflush(stderr);
    }

    void profiler_service::destroy_stream_locked(std::size_t index)
    {
        stream_entry& entry = m_streams[index];
        if (!entry.stream)
        {
            return;
        }

        // ★ 주인만 자기 스트림을 닫는다. 남이 finish() 를 부르면 주인이 쓰고
        //   있는 저장소를 만진다 — 그것이 여기서 죽던 길이다. 못 닫은 것은
        //   **세어 둔다.** 조용히 넘기면 "그 스레드가 조용했다" 로 읽힌다.
        const bool owned = (entry.stream->owner_thread() == std::this_thread::get_id());

        m_retiredDropped.fetch_add(entry.stream->dropped_events(), std::memory_order_relaxed);
        m_retiredUnbalanced.fetch_add(entry.stream->unbalanced_scopes(), std::memory_order_relaxed);
        m_retiredForeign.fetch_add(entry.stream->foreign_touches(), std::memory_order_relaxed);

        // ★ 이 스레드가 이 서비스에서 쓰던 자리를 끊는다. 옛 코어가 죽은
        //   thread_local 을 계속 가리켜 이후 모든 수집이 UAF 였던 자리다.
        if (tls_stream() == entry.stream.get())
        {
            t_streams[m_serviceSlot] = tls_slot{};
        }

        if (owned)
        {
            entry.stream->finish(now());
            entry.stream.reset();
        }
        else
        {
            // 주인이 아직 돈다. 닫지도, **해제하지도** 않는다 — 소멸자가
            // 소유 검사 없이 청크를 만지는 것을 피해도 저장소가 사라지면
            // 주인이 쓰는 자리가 없어진다. 풀과 함께 놓아 두고 센다.
            std::lock_guard<std::mutex> guard(retained_lock());
            retained_streams().push_back(retained_stream{ std::move(entry.stream), m_pool });
            m_abandonedStreams.fetch_add(1, std::memory_order_relaxed);
        }

        entry.live = false;
    }

    void profiler_service::register_thread(const char* name, track_kind kind,
                                           std::uint32_t track_order)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            return;
        }

        if (tls_stream())
        {
            return;   // 이미 등록됨
        }

        const std::uint32_t os_id = static_cast<std::uint32_t>(::GetCurrentThreadId());

        std::lock_guard<std::mutex> guard(m_streamLock);

        thread_info info;
        info.os_thread_id = os_id;
        info.slot = static_cast<std::uint32_t>(m_streams.size());
        info.kind = kind;
        info.track_order = track_order;
        if (name && *name)
        {
            info.name = name;
        }
        else
        {
            info.name = "Thread " + std::to_string(info.slot);
        }

        auto stream = std::make_unique<thread_stream>(*m_pool, info);
        stream->set_generation(m_generation.load(std::memory_order_acquire));
        t_streams[m_serviceSlot] = tls_slot{
            stream.get(), m_slotEpoch.load(std::memory_order_acquire) };

        stream_entry entry;
        entry.stream = std::move(stream);
        entry.os_thread_id = os_id;
        entry.live = true;

        m_streams.push_back(std::move(entry));
        m_threadInfo.push_back(std::move(info));
    }

    void profiler_service::unregister_thread()
    {
        thread_stream* stream = tls_stream();
        if (!stream)
        {
            return;
        }

        std::lock_guard<std::mutex> guard(m_streamLock);
        for (std::size_t i = 0; i < m_streams.size(); ++i)
        {
            if (m_streams[i].stream.get() == stream)
            {
                destroy_stream_locked(i);
                break;
            }
        }

        // ★ 슬롯은 남기고 스트림만 끊는다. 이미 수집된 이벤트가 thread_slot
        //   으로 귀속을 표현하므로, 자리를 지우면 지난 프레임의 스레드가
        //   어긋난다. 이름표는 캡처가 사는 동안 그대로 둔다.
        t_streams[m_serviceSlot] = tls_slot{};
    }

    std::uint32_t profiler_service::thread_count() const
    {
        std::lock_guard<std::mutex> guard(m_streamLock);
        return static_cast<std::uint32_t>(m_threadInfo.size());
    }

    std::vector<thread_info> profiler_service::threads() const
    {
        std::lock_guard<std::mutex> guard(m_streamLock);
        return m_threadInfo;
    }

    thread_stream* profiler_service::tls_stream() const
    {
        const tls_slot& slot = t_streams[m_serviceSlot];

        // 세대가 다르면 이 자리는 **지난 서비스의 것**이다. 그 스트림은 이미
        // 없어졌으므로 따라가면 use-after-free 다.
        return (slot.epoch == m_slotEpoch.load(std::memory_order_acquire))
            ? slot.stream : nullptr;
    }

    thread_stream* profiler_service::current_stream()
    {
        thread_stream* stream = tls_stream();
        if (stream)
        {
            return stream;
        }

        // 등록하지 않은 스레드가 마커를 찍으면 그 자리에서 등록한다.
        // 이름이 없으면 슬롯 번호로 붙는다 — 잃는 것보다 낫다.
        register_thread(nullptr);
        return tls_stream();
    }

    void profiler_service::publish_thread()
    {
        if (thread_stream* stream = tls_stream())
        {
            stream->publish_frame();
        }
    }

    // ★ 상태 관문은 **여는 쪽에만** 있고, 그때도 짝을 예약하고 나간다.
    //
    //   두 쪽에 같은 관문을 걸면 pause·record 가 스코프 한가운데서 일어날 때
    //   짝이 어긋난다. 여는 쪽만 재고 닫는 쪽이 얼어 버리면 스택에 한 칸이 남아
    //   **그 뒤의 모든 구간이 한 칸씩 깊어진다.** 거꾸로, 얼린 채 열고 녹화가
    //   다시 열린 뒤에 닫으면 스택에서 남의 구간을 닫는다.
    //
    //   둘 다 조용하다 — 깊이만 밀릴 뿐 아무것도 실패하지 않는다. 툴바의
    //   Pause 단추도 다른 스레드가 구간 안에 있을 때 눌리므로, CLI 를 붙이기
    //   전에도 있던 결함이다.
    void profiler_service::begin_scope(marker_id id, const cpu_span_context& cpu)
    {
        thread_stream* stream = current_stream();
        if (!stream)
        {
            return;
        }

        if (m_state.load(std::memory_order_relaxed) != recorder_state::recording)
        {
            stream->skip_scope();
            return;
        }

        stream->begin_scope(id, now(), m_engineFrame.load(std::memory_order_relaxed), cpu);
    }

    void profiler_service::end_scope()
    {
        // 상태를 보지 않는다. 여는 쪽이 이미 재놓았거나 짝을 예약해 둥으므로,
        // 닫는 쪽은 언제나 스트림까지 가 닿아야 스택이 제자리로 돌아온다.
        thread_stream* stream = tls_stream();
        if (!stream)
        {
            return;
        }

        stream->end_scope(now());
    }

    void profiler_service::mark_instant(marker_id id, const cpu_span_context& cpu)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            return;
        }
        if (m_state.load(std::memory_order_relaxed) != recorder_state::recording)
        {
            return;
        }

        thread_stream* stream = current_stream();
        if (!stream)
        {
            return;
        }

        stream->write_instant(id, now(), m_engineFrame.load(std::memory_order_relaxed), cpu);
    }

    void profiler_service::collect_sealed()
    {
        event_chunk* sealed = m_pool->take_sealed();
        if (!sealed)
        {
            return;
        }

        ingest_pages(sealed, m_frameBeginTick);
    }

    void profiler_service::ingest_pages(event_chunk* sealed, profile_tick frame_begin_tick)
    {
        if (!sealed)
        {
            return;
        }
        const profile_tick start = now();
        m_ring.ingest(sealed, m_pool, m_generation.load(std::memory_order_acquire),
                      frame_begin_tick);
        m_collectorTiming.page_ingest_ticks += now() - start;
        ++m_collectorTiming.ingest_batches;
    }

    void profiler_service::close_frame(std::uint32_t engine_frame,
                                       profile_tick begin, profile_tick end)
    {
        const profile_tick start = now();
        m_ring.close_frame(engine_frame, begin, end);
        collect_recording_frames();
        m_collectorTiming.frame_close_ticks += now() - start;
        ++m_collectorTiming.frames_closed;
    }

    void profiler_service::account_counter_queue_loss()
    {
        const std::uint64_t total = m_queueDroppedCounters.load(std::memory_order_relaxed);
        for (std::uint64_t i = m_accountedCounterQueueDropped; i < total; ++i)
        {
            m_ring.note_dropped_counter();
        }
        m_accountedCounterQueueDropped = total;
    }

    void profiler_service::replenish_pages()
    {
        const profile_tick start = now();
        m_pool->replenish(64, m_maxChunkCount);
        m_collectorTiming.replenish_ticks += now() - start;
    }

    thread_stream* profiler_service::gpu_stream()
    {
        thread_stream* existing = m_gpuStream.load(std::memory_order_acquire);
        if (existing)
        {
            return existing;
        }

        std::lock_guard<std::mutex> guard(m_streamLock);
        existing = m_gpuStream.load(std::memory_order_relaxed);
        if (existing)
        {
            return existing;
        }

        thread_info info;
        info.os_thread_id = 0;   // OS 스레드가 아니다 — 큐다.
        info.slot = static_cast<std::uint32_t>(m_streams.size());
        info.name = kGpuLaneName;
        info.kind = track_kind::gpu_graphics;

        auto stream = std::make_unique<thread_stream>(*m_pool, info);
        stream->set_generation(m_generation.load(std::memory_order_acquire));
        thread_stream* created = stream.get();

        stream_entry entry;
        entry.stream = std::move(stream);
        entry.os_thread_id = 0;
        entry.live = true;

        m_streams.push_back(std::move(entry));
        m_threadInfo.push_back(std::move(info));

        // 표에 올린 **뒤에** 공개한다. 먼저 공개하면 다른 스레드가 아직
        // 등록되지 않은 스트림에 적을 수 있다.
        m_gpuStream.store(created, std::memory_order_release);
        return created;
    }

    void profiler_service::submit_gpu_span(marker_id id, profile_tick begin,
                                           profile_tick end, std::uint32_t frame,
                                           const gpu_span_context& gpu)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            return;
        }
        if (m_state.load(std::memory_order_relaxed) != recorder_state::recording)
        {
            return;
        }

        // 끝이 시작보다 앞선 구간은 받지 않는다. 길이 0 은 받는다 — 그 패스는
        // 실제로 돌았고 비용이 timestamp 분해능 아래일 뿐이다.
        if (end < begin)
        {
            return;
        }

        thread_stream* stream = gpu_stream();
        if (!stream)
        {
            return;
        }

        // 깊이는 0 이다. 한 큐의 구간들은 중첩하지 않는다 — BeginPass/EndPass
        // 가 커맨드 리스트에 순서대로 놓이기 때문이다.
        stream->write_span(id, begin, end, frame, 0, gpu);
    }

    void profiler_service::publish_gpu_spans()
    {
        thread_stream* stream = m_gpuStream.load(std::memory_order_acquire);
        if (!stream)
        {
            return;
        }
        stream->publish_frame();
    }

    void profiler_service::retire_gpu_lane()
    {
        thread_stream* stream = m_gpuStream.load(std::memory_order_acquire);
        if (!stream)
        {
            return;
        }

        // ★ 주인만 닫는다. 아니면 그냥 둔다 — 종료 경로가 세어서 알려 준다.
        if (stream->owner_thread() != std::this_thread::get_id())
        {
            return;
        }

        // 남은 것을 먼저 넘긴다. 닫고 나면 이 레인의 꼬리를 아무도 못 준다.
        stream->publish_frame();

        std::lock_guard<std::mutex> guard(m_streamLock);
        for (std::size_t i = 0; i < m_streams.size(); ++i)
        {
            if (m_streams[i].stream.get() != stream)
            {
                continue;
            }

            // 표에서 먼저 내린다. 닫는 동안 다른 스레드가 이 포인터로
            // 들어오면 이미 끝난 저장소를 만진다.
            m_gpuStream.store(nullptr, std::memory_order_release);
            destroy_stream_locked(i);
            break;
        }
    }

    void profiler_service::publish_counter(std::uint32_t engine_frame,
                                           profile_counter_id id, double value)
    {
        if (!m_initialized.load(std::memory_order_acquire) ||
            m_state.load(std::memory_order_relaxed) != recorder_state::recording)
        {
            return;
        }
        if (engine_frame < m_recordStartFrame.load(std::memory_order_acquire))
        {
            return;
        }
        if (!std::isfinite(value) ||
            (get_counter_mask() & counter_category_bit(id)) == 0)
        {
            return;
        }
        collector_work work;
        work.kind = work_kind::counter;
        work.frame = engine_frame;
        work.counter = { id, value };
        work.generation = m_generation.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            if (m_stopWorker ||
                m_state.load(std::memory_order_acquire) != recorder_state::recording ||
                engine_frame < m_recordStartFrame.load(std::memory_order_acquire))
            {
                return;
            }
            if (m_workQueue.size() >= kMaximumQueuedCounterWork)
            {
                m_queueDroppedCounters.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            try { m_workQueue.push_back(std::move(work)); }
            catch (const std::bad_alloc&)
            {
                m_queueDroppedCounters.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        m_workSignal.release();
    }

    void profiler_service::publish_counters(std::uint32_t engine_frame,
                                            counter_category category,
                                            std::span<const profile_counter_sample> samples)
    {
        if (samples.empty() || !m_initialized.load(std::memory_order_acquire) ||
            m_state.load(std::memory_order_acquire) != recorder_state::recording ||
            engine_frame < m_recordStartFrame.load(std::memory_order_acquire) ||
            (get_counter_mask() & counter_bit(category)) == 0)
        {
            return;
        }
        std::size_t queued = 0;
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            if (m_stopWorker ||
                m_state.load(std::memory_order_acquire) != recorder_state::recording ||
                engine_frame < m_recordStartFrame.load(std::memory_order_acquire))
            {
                return;
            }
            for (const profile_counter_sample sample : samples)
            {
                if (!std::isfinite(sample.value) ||
                    counter_category_bit(sample.id) != counter_bit(category))
                {
                    continue;
                }
                collector_work work;
                work.kind = work_kind::counter;
                work.frame = engine_frame;
                work.counter = sample;
                work.generation = m_generation.load(std::memory_order_acquire);
                if (m_workQueue.size() >= kMaximumQueuedCounterWork)
                {
                    m_queueDroppedCounters.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                try { m_workQueue.push_back(std::move(work)); }
                catch (const std::bad_alloc&)
                {
                m_queueDroppedCounters.fetch_add(1, std::memory_order_relaxed);
                continue;
                }
                ++queued;
            }
        }
        if (queued != 0)
        {
            m_workSignal.release(static_cast<std::ptrdiff_t>(queued));
        }
    }

    void profiler_service::publish_frame(std::uint32_t engine_frame)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            return;
        }

        // 제어 요청과 프레임 경계의 순서를 고정한다. pause/clear 뒤에
        // 오래 걸린 publish_frame 이 옛 프레임을 넣지 못하게 한다.
        std::unique_lock<std::mutex> workGuard(m_workLock);
        if (m_stopWorker)
        {
            return;
        }
        const recorder_state current = m_state.load(std::memory_order_acquire);
        if (current == recorder_state::pausing)
        {
            // 프레임 제출자도 기록자다. 얼림 요청 뒤 다음 안전 지점이
            // publish_frame 이면 자기 꼬리를 여기서 직접 넘긴다.
            if (thread_stream* self = tls_stream())
            {
                self->freeze_self(m_freezeTick.load(std::memory_order_acquire));
            }
            return;
        }
        if (current != recorder_state::recording)
        {
            return;
        }
        // 등록된 모든 스트림이 자기 청크를 봉인한다. 열린 스코프는 닫지
        // 않는다 — 프레임을 넘는 구간을 잃지 않기 위해서다.
        thread_stream* const self = tls_stream();
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (stream_entry& entry : m_streams)
            {
                if (!entry.stream)
                {
                    continue;
                }

                // ★ 남의 스트림을 **건드리지 않는다.** 예전에는 여기서 모든
                //   스트림의 현재 청크를 직접 봉인했고, writer 는 그 잠금을
                //   잡지 않으므로 쓰기와 봉인이 그대로 겹쳤다. 실측: 워커 넷이
                //   적는 동안 이 루프를 돌리자 Debug·Release 모두
                //   ACCESS_VIOLATION 으로 죽었다(§0.5.15).
                //
                //   이제는 요청만 올린다. 봉인은 주인 스레드가 자기 write()
                //   첫머리에서 한다. 내가 곧 이 스레드의 주인이면 — 게임
                //   스레드가 자기 스트림을 가진 경우 — 아래에서 직접 봉인한다.
                if (entry.stream.get() == self)
                {
                    entry.stream->publish_frame();
                }
                else
                {
                    entry.stream->request_seal();
                }
            }
        }
        // 이 경계까지 봉인된 청크만 함께 넘긴다. 소비자가 늦게 깨어나도
        // 다음 프레임에 쓰인 청크를 앞 프레임으로 당겨 읽지 않는다.
        event_chunk* sealed = m_pool->take_sealed();
        // 호출자의 프레임 경계 시각을 사용한다. 소비자 스케줄링 지연을
        // 프레임 길이로 잘못 기록하지 않는다.
        const profile_tick tick = now();
        m_engineFrame.store(engine_frame + 1, std::memory_order_relaxed);
        auto drop_frame = [&]
        {
            std::uint64_t lost = 0;
            for (event_chunk* chunk = sealed; chunk; chunk = chunk->next)
            {
                lost += chunk->count;
            }
            m_queueDroppedEvents.fetch_add(lost, std::memory_order_relaxed);
            m_droppedFrameBoundaries.fetch_add(1, std::memory_order_relaxed);
            m_pendingGapTick.store(tick, std::memory_order_release);
            m_pool->release(sealed);
        };
        if (m_maxQueuedFrames != 0 && m_queuedFrames >= m_maxQueuedFrames)
        {
            drop_frame();
            return;
        }
        collector_work work;
        work.kind = work_kind::frame;
        work.frame = engine_frame;
        work.tick = tick;
        work.gap_before_tick = m_pendingGapTick.exchange(0, std::memory_order_acq_rel);
        work.sealed = sealed;
        try
        {
            m_workQueue.push_back(std::move(work));
        }
        catch (const std::bad_alloc&)
        {
            // 큐 자체를 더 늘릴 메모리가 없으면 해당 경계를 명시적으로 잃는다.
            drop_frame();
            return;
        }
        ++m_queuedFrames;
        m_workSignal.release();
    }

    void profiler_service::report_gpu_issue(std::uint32_t frame,
        std::uint32_t lost_passes, bool collect_failed, const char* reason)
    {
        if (m_state.load(std::memory_order_relaxed) != recorder_state::recording)
        {
            return;
        }
        if (lost_passes == 0 && !collect_failed)
        {
            return;
        }
        if (lost_passes > 0)
        {
            m_gpuQueryOverflowPasses.fetch_add(lost_passes, std::memory_order_relaxed);
        }
        if (collect_failed)
        {
            m_gpuCollectFailures.fetch_add(1, std::memory_order_relaxed);
        }
        {
            std::lock_guard<std::mutex> guard(m_gpuIssueLock);
            m_gpuIssueLastFrame.store(frame, std::memory_order_relaxed);
            m_gpuIssueLastError = reason ? reason : "";
        }
    }

    void profiler_service::wait_until_idle()
    {
        if (!m_initialized.load(std::memory_order_acquire) || on_collector())
        {
            return;
        }
        auto completion = std::make_shared<std::promise<void>>();
        std::future<void> finished = completion->get_future();
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            if (m_stopWorker)
            {
                return;
            }
            collector_work work;
            work.kind = work_kind::barrier;
            work.completion = std::move(completion);
            m_workQueue.push_back(std::move(work));
            m_workSignal.release();
        }
        finished.wait();
    }

    void profiler_service::collect_frame(std::uint32_t engine_frame, profile_tick tick,
                                         profile_tick gap_before_tick, event_chunk* sealed)
    {
        if (m_firstRecordBoundaryPending)
        {
            // The requested start frame can already be behind the game thread.
            // Only the first boundary actually collected defines the capture start.
            m_recordStartFrame.store(engine_frame, std::memory_order_release);
            m_ring.discard_deferred_counters_before(engine_frame);
            m_firstRecordBoundaryPending = false;
        }
        const profile_tick started = now();
        if (started >= tick)
        {
            m_collectorTiming.queue_delay_ticks += started - tick;
        }
        ++m_collectorTiming.queued_frames_started;
        // 버린 프레임의 시간은 다음 살아남은 프레임에 더하지 않는다.
        // 번호도 건너뛰므로 화면에서 구멍을 볼 수 있다.
        if (gap_before_tick > m_frameBeginTick && gap_before_tick < tick)
        {
            m_frameBeginTick = gap_before_tick;
        }
        if (sealed)
        {
            ingest_pages(sealed, m_frameBeginTick);
        }
        std::uint64_t sourceDropped = m_queueDroppedEvents.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (const stream_entry& entry : m_streams)
            {
                if (entry.stream)
                {
                    sourceDropped += entry.stream->dropped_events();
                }
            }
            sourceDropped += m_retiredDropped.load(std::memory_order_relaxed);
        }
        // producer·제출 큐 누락만 이전 절대값과 비교한다. 링의 손상 페이지
        // 누락까지 기준에 섞으면 두 출처가 같은 수만큼 빠질 수 있다.
        if (sourceDropped > m_accountedSourceDropped)
        {
            m_ring.note_dropped(sourceDropped - m_accountedSourceDropped);
            m_accountedSourceDropped = sourceDropped;
        }

        account_counter_queue_loss();
        close_frame(engine_frame, m_frameBeginTick, tick);
        // Process-owned counters are sampled by the collector, never by the UI.
        // Reuse the last sample between 100 ms polls so the graph remains continuous.
        if ((get_counter_mask() & counter_bit(counter_category::process)) != 0 &&
            (m_lastProcessSampleTick == 0 ||
             tick - m_lastProcessSampleTick >= ticks_per_second() / 10))
        {
            const profile_tick providerStarted = now();
            FILETIME created{}, exited{}, kernel{}, user{}, wall{};
            auto as_u64 = [](const FILETIME& value) -> std::uint64_t
            {
                return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) |
                       value.dwLowDateTime;
            };
            ::GetSystemTimeAsFileTime(&wall);
            if (::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
            {
                const std::uint64_t cpu = as_u64(kernel) + as_u64(user);
                const std::uint64_t elapsed = as_u64(wall);
                if (m_lastProcessWall100ns != 0 && elapsed > m_lastProcessWall100ns &&
                    cpu >= m_lastProcessCpu100ns)
                {
                    const std::uint32_t cores = (std::max)(std::uint32_t{ 1 },
                        static_cast<std::uint32_t>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
                    m_processCpuPercent = (std::min)(100.0,
                        100.0 * static_cast<double>(cpu - m_lastProcessCpu100ns) /
                        (static_cast<double>(elapsed - m_lastProcessWall100ns) * cores));
                    m_hasProcessCpu = true;
                }
                m_lastProcessCpu100ns = cpu;
                m_lastProcessWall100ns = elapsed;
            }
            PROCESS_MEMORY_COUNTERS memory{};
            if (::K32GetProcessMemoryInfo(::GetCurrentProcess(), &memory, sizeof(memory)))
            {
                m_processRamMb = static_cast<double>(memory.WorkingSetSize) / (1024.0 * 1024.0);
                m_hasProcessRam = true;
            }
            m_lastProcessSampleTick = tick;
            const double providerUs = static_cast<double>(now() - providerStarted) * 1000000.0 /
                static_cast<double>(ticks_per_second());
            m_ring.record_counter(engine_frame, { profile_counter_id::provider_process_us, providerUs });
        }
        if ((get_counter_mask() & counter_bit(counter_category::process)) != 0 && m_hasProcessCpu)
        {
            m_ring.record_counter(engine_frame, { profile_counter_id::process_cpu_percent, m_processCpuPercent });
        }
        if ((get_counter_mask() & counter_bit(counter_category::process)) != 0 && m_hasProcessRam)
        {
            m_ring.record_counter(engine_frame, { profile_counter_id::process_ram_mb, m_processRamMb });
        }
        m_lastEngineFrame = engine_frame;

        m_frameBeginTick = tick;

        // 녹화 중 공개. 보는 쪽이 청했을 때만, 정한 간격으로.
        publish_live_capture(tick);
        replenish_pages();
        // 찍어 둔다. 읽는 쪽은 이 사본만 본다 — 링 자체는 수집기의 것이다.
        publish_ring_stats();

    }

    bool profiler_service::on_collector() const
    {
        return m_collectorThread.load(std::memory_order_acquire) == std::this_thread::get_id();
    }

    std::uint64_t profiler_service::dispatch_control(control_op op, std::uint32_t frame)
    {
        if (on_collector())
        {
            switch (op)
            {
            case control_op::record:       record_now(frame); break;
            case control_op::pause:        pause_now();       break;
            case control_op::clear:
                if (state() == recorder_state::stopped || state() == recorder_state::frozen)
                {
                    clear_now();
                }
                break;
            case control_op::finish_pause: finish_pause();    break;
            }
            return 0;
        }

        if (!m_initialized.load(std::memory_order_acquire))
        {
            return 0;
        }
        std::uint64_t ticket = 0;
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            if (m_stopWorker || !m_initialized.load(std::memory_order_acquire))
            {
                return 0;
            }
            ticket = m_controlEnqueued.fetch_add(1, std::memory_order_acq_rel) + 1;
            collector_work work;
            work.kind = work_kind::control;
            work.control = control_request{ op, frame };
            work.control_ticket = ticket;
            m_workQueue.push_back(std::move(work));
            m_workSignal.release();
        }
        m_controlDeferred.fetch_add(1, std::memory_order_relaxed);

        // 시작/지우기도 파일 worker 또는 수집기 진행을 UI에서 기다리지 않는다.
        return ticket;
    }

    void profiler_service::collector_loop()
    {
        m_collectorOsThreadId.store(static_cast<std::uint32_t>(::GetCurrentThreadId()),
                                    std::memory_order_release);
        for (;;)
        {
            const profile_tick waitStart = now();
            m_workSignal.acquire();
            m_collectorTiming.wait_ticks += now() - waitStart;
            collector_work work;
            event_chunk* pages = nullptr;
            bool hasWork = false;
            {
                std::lock_guard<std::mutex> guard(m_workLock);
                if (m_workQueue.empty())
                {
                    if (m_stopWorker)
                    {
                        return;
                    }
                    // 프레임 작업과의 순서만 잠근 채 목록을 떼어 온다.
                    pages = m_pool->take_sealed();
                }
                else
                {
                    work = std::move(m_workQueue.front());
                    m_workQueue.pop_front();
                    if (work.kind == work_kind::frame)
                    {
                        --m_queuedFrames;
                    }
                    hasWork = true;
                }
            }
            if (!hasWork)
            {
                if (pages)
                {
                    ingest_pages(pages, m_frameBeginTick);
                    replenish_pages();
                    publish_ring_stats();
                }
                continue;
            }
            if (work.kind == work_kind::frame)
            {
                collect_frame(work.frame, work.tick, work.gap_before_tick, work.sealed);
                continue;
            }
            if (work.kind == work_kind::counter)
            {
                if (work.generation == m_generation.load(std::memory_order_acquire))
                {
                    if (work.frame >= m_recordStartFrame.load(std::memory_order_acquire))
                    {
                        m_ring.record_counter(work.frame, work.counter);
                    }
                }
                else
                {
                    m_ring.note_dropped_counter();
                }
                publish_ring_stats();
                continue;
            }
            if (work.kind == work_kind::barrier)
            {
                collect_sealed();
                replenish_pages();
                publish_ring_stats();
                work.completion->set_value();
                continue;
            }
            switch (work.control.op)
            {
            case control_op::record:       record_now(work.control.frame); break;
            case control_op::pause:        pause_now();                 break;
            case control_op::clear:
                if (state() == recorder_state::stopped || state() == recorder_state::frozen)
                {
                    clear_now();
                }
                break;
            case control_op::finish_pause: finish_pause();              break;
            }
            // 완료 조건과 wait()의 잠금이 같아야 검사 직후 통지를 놓치지 않는다.
            {
                std::lock_guard<std::mutex> guard(m_controlWaitLock);
                m_controlApplied.store(work.control_ticket, std::memory_order_release);
            }
            m_controlAppliedCv.notify_all();
        }
    }

    void profiler_service::record(std::uint32_t first_frame)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(recording_lock_);
            if (recording_writer_)
            {
                const auto state = recording_writer_->status().state;
                if (state != recording_state::finalized && state != recording_state::failed)
                {
                    return;
                }
            }
        }
        recorder_state previous = m_state.load(std::memory_order_acquire);
        while (previous == recorder_state::stopped || previous == recorder_state::frozen)
        {
            if (m_state.compare_exchange_weak(previous, recorder_state::starting, std::memory_order_acq_rel))
            {
                if (dispatch_control(control_op::record, first_frame) == 0)
                {
                    recorder_state pending = recorder_state::starting;
                    m_state.compare_exchange_strong(pending, recorder_state::stopped, std::memory_order_acq_rel);
                }
                return;
            }
        }
    }

    void profiler_service::pause()
    {
        // ★ 이 절반은 **어느 스레드에서든 그 자리에서** 한다. 상태를 올리고,
        //   자를 시각을 정하고, 자기 스트림을 자기가 잠근다. 링을 만지지
        //   않으므로 수집기와 겹치지 않는다.
        //
        //   자기 스트림을 자기가 잠그는 것이 핵심이다 — 부른 쪽이 완료를
        //   기다리면 기다리는 동안 자기 봉인 요청에 응답할 수 없어, 자기가
        //   자기 꼬리를 못 넘긴다(실측: 미응답 1, 그 스레드의 이벤트 0).
        profile_tick freezeTick = 0;
        {
            // UI와 콘솔의 중복 Stop이 서로 다른 종료 시각을 덮어쓰지 않게 한다.
            std::lock_guard<std::mutex> guard(m_workLock);
            if (m_state.load(std::memory_order_acquire) != recorder_state::recording)
            {
                return;
            }
            freezeTick = now();
            m_freezeTick.store(freezeTick, std::memory_order_release);
            m_state.store(recorder_state::pausing, std::memory_order_release);
        }

        {
            thread_stream* const self = tls_stream();
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (stream_entry& entry : m_streams)
            {
                if (!entry.stream)
                {
                    continue;
                }
                if (entry.stream.get() == self)
                {
                    entry.stream->freeze_self(freezeTick);
                }
                else
                {
                    entry.stream->request_freeze(freezeTick);
                }
            }
        }

        // 마무리는 링을 만지므로 수집기의 일이다.
        dispatch_control(control_op::finish_pause, 0);
    }

    std::uint64_t profiler_service::clear()
    {
        const recorder_state current = state();
        if (current == recorder_state::recording || current == recorder_state::pausing ||
            current == recorder_state::starting)
        {
            return 0;
        }
        return dispatch_control(control_op::clear, 0);
    }

    void profiler_service::record_now(std::uint32_t first_frame)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            return;
        }
        clear_now();
        auto started = continuous_capture_writer::start();
        std::unique_ptr<continuous_capture_writer> previous;
        {
            std::lock_guard<std::mutex> guard(recording_lock_);
            previous = std::move(recording_writer_);
            recording_start_error_.reset();
            if (started)
            {
                recording_writer_ = std::move(*started);
            }
            else
            {
                recording_start_error_ = started.error();
            }
        }
        // 이전 worker는 이미 finalized/failed이다. join과 파괴도 수집기에서만 한다.
        previous.reset();
        if (!started)
        {
            m_state.store(recorder_state::stopped, std::memory_order_release);
            return;
        }
        recording_finalized_ = false;
        recording_batch_.clear();
        recording_batch_bytes_ = 0;
        m_engineFrame.store(first_frame, std::memory_order_relaxed);
        m_recordStartFrame.store(first_frame, std::memory_order_release);
        m_firstRecordBoundaryPending = true;
        m_frameBeginTick = now();
        m_lastProcessSampleTick = 0;
        m_lastProcessCpu100ns = 0;
        m_lastProcessWall100ns = 0;
        m_hasProcessCpu = false;
        m_hasProcessRam = false;

        // 간격 계수기를 되돌린다. 녹화를 새로 열었는데 지난 회차의 시각이
        // 남아 있으면 첫 스냅샷이 한 박자 늦게 선다.
        m_lastLiveCapture = 0;
        m_lastEngineFrame = first_frame > 0 ? first_frame - 1 : 0;

        m_state.store(recorder_state::recording, std::memory_order_release);
        publish_ring_stats();
    }

    void profiler_service::pause_now()
    {
        // 줄을 통해 들어온 pause. 앞절반은 pause() 가 이미 했다.
        if (m_state.load(std::memory_order_acquire) == recorder_state::recording)
        {
            pause();
        }
        finish_pause();
    }

    void profiler_service::finish_pause()
    {
        if (m_state.load(std::memory_order_acquire) != recorder_state::pausing)
        {
            return;
        }

        finalizing_recording_ = true;
        const profile_tick freezeTick = m_freezeTick.load(std::memory_order_acquire);

        // ★ **내 스트림부터 잠근다.** 아래에서 응답을 기다리는 동안 이 스레드는
        //   자기 봉인 요청에 응답할 자리를 지나지 못한다 — 부른 쪽에서 겪은
        //   것과 같은 일이 수집기에서 되풀이된다(미응답 1). 기다리기 전에
        //   내 것을 끝낸다.
        {
            thread_stream* const self = tls_stream();
            if (self)
            {
                self->freeze_self(freezeTick);
            }
        }

        // ★ 남의 청크를 만지지 않는다. 응답을 **짧게** 기다린 뒤, 아직
        //   응답하지 않은 스트림을 **센다**. 잠든 워커는 다음에 깨어날 때
        //   봉인하므로 그때까지의 꼬리가 이 캡처에 없을 수 있고, 그것을
        //   성공처럼 덮으면 "얼린 캡처가 온전하다" 가 거짓이 된다.
        // 응답을 기다린다. 상한을 두는 이유는 잠든 producer 가 영영 안 깨어날
        // 수 있기 때문이다 — 관측 도구가 관측 대상을 기다리며 멈추면 안 된다.
        std::uint32_t unacked = 0;
        for (int attempt = 0; attempt < kPauseAckAttempts; ++attempt)
        {
            unacked = 0;
            {
                std::lock_guard<std::mutex> guard(m_streamLock);
                for (stream_entry& entry : m_streams)
                {
                    if (!entry.stream)
                    {
                        continue;
                    }
                    // ★ **얼림** 응답만 센다. 평범한 프레임 봉인 요청까지 함께
                    //   세면 등록만 해 두고 조용한 스레드가 언제나 미응답으로
                    //   잡혀, 얼린 캡처가 늘 "온전하지 않다" 가 된다.
                    //   넘길 것이 없는 스트림도 응답할 일이 없으므로 뺀다.
                    if (entry.stream->freeze_ack() != entry.stream->freeze_request()
                        && entry.stream->pending_work())
                    {
                        ++unacked;
                    }
                }
            }
            if (0 == unacked)
            {
                break;
            }
            std::this_thread::yield();
        }
        m_pauseUnacked.store(unacked, std::memory_order_relaxed);

        // ★ 온전하지 않다는 것만으로는 고칠 자리를 못 짚는다. 누가 응답하지
        //   않았는지 적는다 - 그 스레드가 안전한 자리를 안 지났다는 뜻이다.
        if (0 != unacked)
        {
            std::string names;
            {
                std::lock_guard<std::mutex> guard(m_streamLock);
                for (std::size_t i = 0; i < m_streams.size(); ++i)
                {
                    const stream_entry& entry = m_streams[i];
                    if (!entry.stream)
                    {
                        continue;
                    }
                    if (entry.stream->freeze_ack() == entry.stream->freeze_request())
                    {
                        continue;
                    }
                    if (!entry.stream->pending_work())
                    {
                        continue;
                    }
                    if (!names.empty())
                    {
                        names += ", ";
                    }
                    names += (i < m_threadInfo.size()) ? m_threadInfo[i].name : std::string("?");
                }
            }
            std::fprintf(stderr, "[profiler] freeze incomplete - unacked=%u: %s\n",
                         unacked, names.c_str());
            std::fflush(stderr);
        }

        collect_sealed();
        const profile_tick gap = m_pendingGapTick.exchange(0, std::memory_order_acq_rel);
        if (gap > m_frameBeginTick && gap < freezeTick)
        {
            m_frameBeginTick = gap;
        }
        // 마지막 프레임 제출이 과부하로 버려졌어도 손실을 얼린 캡처에 남긴다.
        std::uint64_t sourceDropped = m_queueDroppedEvents.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (const stream_entry& entry : m_streams)
            {
                if (entry.stream)
                {
                    sourceDropped += entry.stream->dropped_events();
                }
            }
            sourceDropped += m_retiredDropped.load(std::memory_order_relaxed);
        }
        if (sourceDropped > m_accountedSourceDropped)
        {
            m_ring.note_dropped(sourceDropped - m_accountedSourceDropped);
            m_accountedSourceDropped = sourceDropped;
        }

        // ★ 마지막 프레임을 닫는다. 닫지 않으면 마지막 publish_frame 이후에
        //   모인 것 — pause 에서 잘라 남긴 열린 구간이 바로 그것이다 — 이
        //   얼린 캡처에 들어가지 못한다. freeze() 는 닫힌 프레임만 본다.
        account_counter_queue_loss();
        if (freezeTick > m_frameBeginTick)
        {
            const std::uint32_t tailFrame = m_firstRecordBoundaryPending
                ? m_engineFrame.load(std::memory_order_relaxed)
                : (std::max)(m_lastEngineFrame + 1, m_engineFrame.load(std::memory_order_relaxed));
            close_frame(tailFrame, m_frameBeginTick, freezeTick);
            m_lastEngineFrame = tailFrame;
            m_frameBeginTick = freezeTick;
        }

        m_ring.discard_pending();

        std::vector<thread_info> threads;
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            threads = m_threadInfo;
        }

        const profile_tick snapshotStart = now();
        capture_session_ptr frozen = m_ring.freeze(
            threads, capture_environment{ ticks_per_second() }, 0 == unacked, unacked);
        {
            std::lock_guard<std::mutex> guard(m_captureLock);
            m_capture = std::move(frozen);
        }
        m_collectorTiming.snapshot_ticks += now() - snapshotStart;
        ++m_collectorTiming.snapshots_built;
        publish_ring_stats();

        // 공개까지 끝난 뒤에야 frozen 이다. 그 전에는 pausing 이고, 읽는 쪽은
        // "아직 손에 없다" 를 그 상태로 안다.
        finalize_recording(0 == unacked, unacked);
        m_state.store(recorder_state::frozen, std::memory_order_release);
    }

    void profiler_service::request_live_capture()
    {
        m_liveCaptureRequested.store(true, std::memory_order_relaxed);
    }

    // 수집기에서만 부른다. 링을 만지는 것은 이 스레드뿐이다.
    void profiler_service::publish_live_capture(profile_tick tick)
    {
        // ★ 청하지 않으면 만들지 않는다. "늘 만들어 두고 안 보면 버린다" 로
        //   두면 창이 닫혀 있는 동안에도 프레임마다 링을 복사하게 되고,
        //   그 비용은 프로파일러가 스스로 만들어 낸 것이라 어느 마커에도
        //   안 잡힌다.
        if (!m_liveCaptureRequested.load(std::memory_order_relaxed))
        {
            return;
        }

        const double intervalMs = m_liveCaptureIntervalMs;
        if (intervalMs > 0.0 && 0 != m_lastLiveCapture)
        {
            const profile_tick interval = static_cast<profile_tick>(
                static_cast<double>(ticks_per_second()) * intervalMs / 1000.0);
            if (tick - m_lastLiveCapture < interval)
            {
                return;   // 아직 이르다. 요청은 그대로 두고 다음 프레임에 다시 본다.
            }
        }

        m_lastLiveCapture = tick;
        m_liveCaptureRequested.store(false, std::memory_order_relaxed);

        std::vector<thread_info> threads;
        std::uint32_t unacked = 0;
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            threads = m_threadInfo;
            for (const stream_entry& entry : m_streams)
            {
                if (!entry.stream || !entry.stream->pending_work())
                {
                    continue;
                }
                if (entry.stream->seal_ack() != entry.stream->seal_request())
                {
                    ++unacked;
                }
            }
        }
        // ack 를 먼저 본 뒤 페이지를 거둔다. ack 가 선 페이지는 이미 seal
        // 목록에 있으므로 이 순서라야 complete=true 가 실제 수집을 뜻한다.
        // 아직 응답하지 않은 스레드가 있으면 스냅샷을 미확정으로 공개한다.
        collect_sealed();

        // ★ 가장 최근 몇 프레임은 **아직 확정이 아니다.** 늦게 오는 GPU
        //   구간이 닫힌 프레임에 나중에 들어가기 때문이다(실측 제출→수집
        //   최대 94 ms). 다음 스냅샷에 그것이 담긴다.
        const profile_tick snapshotStart = now();
        capture_session_ptr live = m_ring.freeze(
            threads, capture_environment{ ticks_per_second() }, 0 == unacked, unacked);
        {
            std::lock_guard<std::mutex> guard(m_captureLock);
            m_capture = std::move(live);
        }
        m_collectorTiming.snapshot_ticks += now() - snapshotStart;
        ++m_collectorTiming.snapshots_built;
    }

    void profiler_service::clear_now()
    {
        m_recordStartFrame.store(m_engineFrame.load(std::memory_order_acquire), std::memory_order_release);
        m_firstRecordBoundaryPending = true;
        // ★ 세대를 먼저 올린다. 이 뒤에 도착하는 옛 청크는 세대가 어긋나
        //   수집기가 버린다 — 잠든 워커가 Clear **전에** 적은 것을 들고 깨어나
        //   새 녹화에 섞는 것이 감사에서 재현된 결함이다.
        const std::uint64_t next = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;

        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (stream_entry& entry : m_streams)
            {
                if (!entry.stream)
                {
                    continue;
                }
                entry.stream->set_generation(next);

                // ★ 봉인도 함께 청한다. 세대는 **청크 단위**로 찍히므로, 지금
                //   쓰고 있는 청크를 끊지 않으면 Clear 뒤에 적은 것까지 옛
                //   세대의 청크에 실려 통째로 버려진다.
                entry.stream->request_seal();
            }
        }

        // 이미 봉인돼 대기 중인 것도 옛 세대다. 거두지 않고 곧바로 되돌린다.
        if (event_chunk* stale = m_pool->take_sealed())
        {
            m_pool->release(stale);
        }

        m_ring.clear();
        m_accountedCounterQueueDropped = m_queueDroppedCounters.load(std::memory_order_relaxed);
        m_lastProcessSampleTick = 0;
        m_lastProcessCpu100ns = 0;
        m_lastProcessWall100ns = 0;
        m_hasProcessCpu = false;
        m_hasProcessRam = false;
        m_collectorTiming = {};
        m_gpuQueryOverflowPasses.store(0, std::memory_order_relaxed);
        m_gpuCollectFailures.store(0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> issueGuard(m_gpuIssueLock);
            m_gpuIssueLastFrame.store(0, std::memory_order_relaxed);
            m_gpuIssueLastError.clear();
        }
        m_pendingGapTick.store(0, std::memory_order_relaxed);
        m_accountedSourceDropped = m_queueDroppedEvents.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            for (const stream_entry& entry : m_streams)
            {
                if (entry.stream)
                {
                    m_accountedSourceDropped += entry.stream->dropped_events();
                }
            }
            m_accountedSourceDropped += m_retiredDropped.load(std::memory_order_relaxed);
        }
        m_droppedFrameBaseline.store(
            m_droppedFrameBoundaries.load(std::memory_order_relaxed), std::memory_order_relaxed);
        publish_ring_stats();

        std::lock_guard<std::mutex> guard(m_captureLock);
        m_capture.reset();
    }

    void profiler_service::collect_recording_frames()
    {
        auto retired = m_ring.take_retired_frames();
        for (frame_record& frame : retired)
        {
            if (!recording_writer_ || recording_finalized_)
            {
                continue;
            }
            recording_batch_bytes_ += frame.memory_bytes();
            recording_batch_.push_back(std::move(frame));
            if (recording_batch_.size() >= 16 || recording_batch_bytes_ >= 4ull * 1024ull * 1024ull)
            {
                flush_recording_batch();
            }
        }
    }

    void profiler_service::flush_recording_batch()
    {
        if (recording_batch_.empty() || !recording_writer_)
        {
            return;
        }
        std::vector<thread_info> vocabulary;
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            vocabulary = m_threadInfo;
        }
        auto batch = std::make_shared<const capture_session>(std::move(recording_batch_), std::move(vocabulary),
            snapshot_markers(), capture_environment{ ticks_per_second() }, false, 0, 0, snapshot_counters());
        recording_batch_.clear();
        recording_batch_bytes_ = 0;
        if (finalizing_recording_)
        {
            recording_writer_->append_final(std::move(batch));
        }
        else
        {
            recording_writer_->append(std::move(batch));
        }
    }

    void profiler_service::finalize_recording(bool complete, std::uint32_t unacked_streams)
    {
        if (!recording_writer_ || recording_finalized_)
        {
            return;
        }
        finalizing_recording_ = true;
        collect_recording_frames();
        const auto remaining = m_ring.freeze(threads(), capture_environment{ ticks_per_second() },
            complete, unacked_streams);
        for (const frame_record& frame : remaining->frames())
        {
            recording_batch_bytes_ += frame.memory_bytes();
            recording_batch_.push_back(frame);
            if (recording_batch_.size() >= 16 || recording_batch_bytes_ >= 4ull * 1024ull * 1024ull)
            {
                flush_recording_batch();
            }
        }
        flush_recording_batch();
        const recording_source_losses losses{
            m_ring.dropped_events(),
            m_droppedFrameBoundaries.load(std::memory_order_relaxed) -
                m_droppedFrameBaseline.load(std::memory_order_relaxed),
            m_ring.late_events_dropped(), m_ring.late_spans_dropped()
        };
        recording_writer_->request_finalize(complete, unacked_streams, m_ring.dropped_counters(), losses);
        recording_finalized_ = true;
        finalizing_recording_ = false;
    }

    ce::recording_status profiler_service::recording_status() const
    {
        std::lock_guard<std::mutex> guard(recording_lock_);
        if (recording_writer_)
        {
            return recording_writer_->status();
        }
        ce::recording_status result;
        result.state = recording_state::failed;
        result.error = recording_start_error_;
        return result;
    }

    std::filesystem::path profiler_service::recording_path() const
    {
        std::lock_guard<std::mutex> guard(recording_lock_);
        return recording_writer_ ? recording_writer_->path() : std::filesystem::path{};
    }

    capture_session_ptr profiler_service::capture() const
    {
        std::lock_guard<std::mutex> guard(m_captureLock);
        return m_capture;
    }

    void profiler_service::publish_ring_stats()
    {
        ring_stats value;
        value.retained_frames = m_ring.retained_frames();
        value.last_frame_events = m_ring.last_frame_events();
        value.peak_frame_events = m_ring.peak_frame_events();
        value.dropped_events = m_ring.dropped_events();
        value.memory_bytes = m_ring.memory_bytes();
        value.late_spans_placed = m_ring.late_spans_placed();
        value.late_spans_dropped = m_ring.late_spans_dropped();
        value.dropped_counters = m_ring.dropped_counters();
        value.late_spans_waiting = m_ring.late_spans_waiting();
        value.stale_chunks_dropped = m_ring.stale_chunks_dropped();
        value.malformed_pages = m_ring.malformed_pages();
        value.ingested_pages = m_ring.ingested_pages();
        value.late_events_placed = m_ring.late_events_placed();
        value.late_events_dropped = m_ring.late_events_dropped();
        value.collector = m_collectorTiming;

        std::lock_guard<std::mutex> guard(m_ringStatsLock);
        m_ringStats = value;
    }

    std::size_t profiler_service::retained_stream_count()
    {
        std::lock_guard<std::mutex> guard(retained_lock());
        return retained_streams().size();
    }

    live_summary profiler_service::summary() const
    {
        live_summary value;
        value.state = m_state.load(std::memory_order_acquire);
        value.engine_frame = m_engineFrame.load(std::memory_order_relaxed);
        // ★ 링을 직접 읽지 않는다. 수집기가 찍어 둔 사본을 베낀다.
        {
            std::lock_guard<std::mutex> guard(m_ringStatsLock);
            value.retained_frames = m_ringStats.retained_frames;
            value.last_frame_events = m_ringStats.last_frame_events;
            value.peak_frame_events = m_ringStats.peak_frame_events;
            value.dropped_events = m_ringStats.dropped_events;
            value.memory_bytes = m_ringStats.memory_bytes;
            value.late_spans_placed = m_ringStats.late_spans_placed;
            value.late_spans_dropped = m_ringStats.late_spans_dropped;
            value.dropped_counters = m_ringStats.dropped_counters;
            value.late_spans_waiting = m_ringStats.late_spans_waiting;
            value.stale_chunks_dropped = m_ringStats.stale_chunks_dropped;
            value.malformed_pages = m_ringStats.malformed_pages;
            value.ingested_pages = m_ringStats.ingested_pages;
            value.late_events_placed = m_ringStats.late_events_placed;
            value.late_events_dropped = m_ringStats.late_events_dropped;
            value.collector = m_ringStats.collector;
        }
        value.memory_budget = m_memoryBudget;
        value.registered_markers = registered_marker_count();
        value.free_chunks = m_pool->free_count();
        value.chunk_count = m_pool->chunk_count();
        value.page_pool_bytes = static_cast<std::size_t>(value.chunk_count) * sizeof(event_chunk);

        std::uint64_t unbalanced = m_retiredUnbalanced.load(std::memory_order_relaxed);
        std::uint64_t foreign = m_retiredForeign.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(m_streamLock);
            value.thread_count = static_cast<std::uint32_t>(m_threadInfo.size());
            for (const stream_entry& entry : m_streams)
            {
                if (entry.stream)
                {
                    unbalanced += entry.stream->unbalanced_scopes();
                    foreign += entry.stream->foreign_touches();
                }
            }
        }
        value.unbalanced_scopes = unbalanced;
        value.foreign_stream_touches = foreign;
        value.abandoned_streams = m_abandonedStreams.load(std::memory_order_relaxed);
        value.control_requests_deferred = m_controlDeferred.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(m_workLock);
            value.collector_queued_frames = static_cast<std::uint32_t>(m_queuedFrames);
        }
        value.collector_dropped_frames =
            m_droppedFrameBoundaries.load(std::memory_order_relaxed)
            - m_droppedFrameBaseline.load(std::memory_order_relaxed);
        value.collector_os_thread_id = m_collectorOsThreadId.load(std::memory_order_acquire);
        value.gpu_query_overflow_passes = m_gpuQueryOverflowPasses.load(std::memory_order_relaxed);
        value.gpu_collect_failures = m_gpuCollectFailures.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> issueGuard(m_gpuIssueLock);
            value.gpu_issue_last_frame = m_gpuIssueLastFrame.load(std::memory_order_relaxed);
            value.gpu_issue_last_error = m_gpuIssueLastError;
        }

        value.pause_unacked_streams = m_pauseUnacked.load(std::memory_order_relaxed);
        value.capture_complete = (0 == value.pause_unacked_streams);

        {
            std::lock_guard<std::mutex> guard(m_captureLock);
            if (m_capture)
            {
                value.total_events = m_capture->total_events();
                value.capture_complete = m_capture->complete();
                value.capture_unacked_streams = m_capture->unacked_streams();
            }
        }
        return value;
    }
}

namespace ce
{
    // 엔진 전체가 쓰는 라이브 서비스. 함수 지역 static 이라 첫 사용에서
    // 서고, 정적 소멸 순서 문제를 피하려고 의도적으로 해제하지 않는다 —
    // 종료 중에 도는 마커가 죽은 서비스를 만나는 것보다 낫다.
    profiler_service& profiler()
    {
        static profiler_service* instance = new profiler_service();
        return *instance;
    }
}
