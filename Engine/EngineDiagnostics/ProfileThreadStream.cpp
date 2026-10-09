#include "ProfileThreadStream.h"

#include <algorithm>
#include <new>
#include <utility>

namespace ce
{
	bool track_precedes(const thread_info& a, const thread_info& b)
	{
		if (a.kind != b.kind) return a.kind < b.kind;
		if (a.track_order != b.track_order) return a.track_order < b.track_order;
		return a.slot < b.slot;
	}

	//-------------------------------------------------------------------------
	// chunk_pool
	//-------------------------------------------------------------------------

	chunk_pool::~chunk_pool()
	{
		shutdown();
	}

	void chunk_pool::initialize(std::uint32_t chunk_count, std::uint32_t maximum_chunks)
	{
		std::lock_guard<std::mutex> guard(m_lock);

		m_storage.clear();
		// 명시적 상한이 있으면 포인터 자리를 미리 잡는다. 기본 모드는
		// 필요할 때 확장하므로 초기 페이지 수만큼만 예약한다.
		m_maximumChunks = maximum_chunks == 0 ? 0 : (std::max)(chunk_count, maximum_chunks);
		m_storage.reserve(m_maximumChunks == 0 ? chunk_count : m_maximumChunks);
		m_free = nullptr;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		m_chunkCount = chunk_count;
		m_freeCount = chunk_count;

		// 초기 페이지만 한 번에 잡는다. 부족해지면 수집 스레드가
		// 보충하므로 기록자의 hot path 에서는 heap 을 건드리지 않는다.
		for (std::uint32_t i = 0; i < chunk_count; ++i)
		{
			auto chunk = std::make_unique<event_chunk>();
			chunk->next = m_free;
			m_free = chunk.get();
			m_storage.push_back(std::move(chunk));
		}
	}

	void chunk_pool::shutdown()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		m_signal = nullptr;
		m_free = nullptr;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		m_freeCount = 0;
		m_chunkCount = 0;
		m_maximumChunks = 0;
		m_storage.clear();
	}

	void chunk_pool::set_signal(std::counting_semaphore<INT_MAX>* signal)
	{
		std::lock_guard<std::mutex> guard(m_lock);
		m_signal = signal;
	}

	event_chunk* chunk_pool::acquire()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		if (m_free)
		{
			event_chunk* chunk = m_free;
			m_free = chunk->next;
			chunk->next = nullptr;
			--m_freeCount;
			return chunk;
		}
		if (m_chunkCount == UINT32_MAX
		    || (m_maximumChunks != 0 && m_chunkCount >= m_maximumChunks))
		{
			return nullptr;
		}
		try
		{
			auto chunk = std::make_unique<event_chunk>();
			event_chunk* result = chunk.get();
			m_storage.push_back(std::move(chunk));
			++m_chunkCount;
			return result;
		}
		catch (const std::bad_alloc&)
		{
			return nullptr;
		}
	}

	void chunk_pool::seal(event_chunk* chunk)
	{
		if (!chunk)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(m_lock);
		const bool wasEmpty = (m_sealed == nullptr);

		// 꼬리에 붙인다. 청크 안의 순서는 writer 가, 청크 사이의 순서는
		// 이 목록이 지킨다.
		chunk->next = nullptr;
		if (m_sealedTail)
		{
			m_sealedTail->next = chunk;
			m_sealedTail = chunk;
		}
		else
		{
			m_sealed = chunk;
			m_sealedTail = chunk;
		}
		if (wasEmpty && m_signal) m_signal->release();
	}

	event_chunk* chunk_pool::take_sealed()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		event_chunk* head = m_sealed;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		return head;
	}

	void chunk_pool::release(event_chunk* chunk_list)
	{
		if (!chunk_list)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(m_lock);
		while (chunk_list)
		{
			event_chunk* next = chunk_list->next;
			chunk_list->count = 0;
			chunk_list->render_measurement_count = 0;
			chunk_list->next = m_free;
			m_free = chunk_list;
			++m_freeCount;
			chunk_list = next;
		}
	}

	void chunk_pool::replenish(std::uint32_t target_free, std::uint32_t maximum_chunks)
	{
		std::uint32_t count = 0;
		{
			std::lock_guard<std::mutex> guard(m_lock);
			const std::uint32_t limit = maximum_chunks == 0
				? UINT32_MAX : (std::max)(maximum_chunks, m_maximumChunks);
			if (m_freeCount >= target_free || m_chunkCount >= limit) return;
			count = (std::min)(target_free - m_freeCount, limit - m_chunkCount);
		}
		// 미리 보충하는 페이지는 잠금 밖의 수집 스레드에서 할당한다.
		std::vector<std::unique_ptr<event_chunk>> fresh;
		try
		{
			fresh.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				fresh.push_back(std::make_unique<event_chunk>());
			}
		}
		catch (const std::bad_alloc&)
		{
			// 준비된 페이지는 추가한다. 다음 기록자가 부족하면 직접 할당한다.
		}
		if (fresh.empty()) return;
		std::lock_guard<std::mutex> guard(m_lock);
		const std::uint32_t limit = maximum_chunks == 0
			? UINT32_MAX : (std::max)(maximum_chunks, m_maximumChunks);
		for (auto& page : fresh)
		{
			if (m_chunkCount >= limit) break;
			try
			{
				m_storage.push_back(std::move(page));
			}
			catch (const std::bad_alloc&)
			{
				break;
			}
			event_chunk* added = m_storage.back().get();
			added->next = m_free;
			m_free = added;
			++m_freeCount;
			++m_chunkCount;
		}
	}

	std::uint32_t chunk_pool::chunk_count() const
	{
		std::lock_guard<std::mutex> guard(m_lock);
		return m_chunkCount;
	}

	std::uint32_t chunk_pool::free_count() const
	{
		std::lock_guard<std::mutex> guard(m_lock);
		return m_freeCount;
	}

	//-------------------------------------------------------------------------
	// thread_stream
	//-------------------------------------------------------------------------

    thread_stream::thread_stream(chunk_pool& pool, thread_info info)
        : m_pool(pool), m_info(std::move(info))
    {
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                      "scope publication requires lock-free 64-bit atomics");
    }

    thread_stream::~thread_stream()
    {
        // 남의 writer는 봉인하거나 회수할 수 없다. 서비스가 수명을 보장한다.
        if (!owned_by_caller())
        {
            return;
        }
        seal_current();
    }

    bool thread_stream::owned_by_caller() const
    {
        static thread_local const std::thread::id self = std::this_thread::get_id();
        if (m_ownerThread == self)
        {
            return true;
        }
        m_foreignTouches.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool thread_stream::ensure_chunk(std::uint64_t generation)
    {
        // 요청과 세대 갱신 사이에 기록자가 끼어들어도 서로 다른 세대는
        // 한 청크에 섞이지 않는다. 청크의 세대는 이벤트를 받아들인 때의 것이다.
        if (m_writer && (m_writer->full() || m_writer->generation != generation))
        {
            seal_current(true);
        }
        if (m_writer)
        {
            return true;
        }
        m_pendingState.fetch_or(kPendingWork, std::memory_order_release);
        m_writer = m_pool.acquire();
        if (!m_writer)
        {
            return false;
        }
        m_writer->reset(m_info.slot, m_sequence);
        m_writer->generation = generation;
        return true;
    }

    void thread_stream::request_freeze(profile_tick freeze_tick)
    {
        // 제어 호출은 직렬화되어 있다. 홀수 버전은 아직 요청을 쓰는 중이다.
        m_freezeVersion.fetch_add(1, std::memory_order_acq_rel);
        const std::uint64_t generation = m_generation.load(std::memory_order_acquire);
        m_freezeTick.store(freeze_tick, std::memory_order_release);
        m_freezeGeneration.store(generation, std::memory_order_release);
        m_frozenTick.store(freeze_tick, std::memory_order_release);
        m_frozenGeneration.store(generation, std::memory_order_seq_cst);
        m_freezeRequest.fetch_add(1, std::memory_order_release);
        m_freezeVersion.fetch_add(1, std::memory_order_release);
        request_seal();
    }

    void thread_stream::honor_seal_request()
    {
        if (m_inHonor)
        {
            return;
        }

        const std::uint64_t sealRequested = m_sealRequest.load(std::memory_order_acquire);
        const std::uint64_t version = m_freezeVersion.load(std::memory_order_acquire);
        const std::uint64_t freezeRequested = m_freezeRequest.load(std::memory_order_acquire);
        const profile_tick freezeTick = m_freezeTick.load(std::memory_order_acquire);
        const std::uint64_t freezeGeneration = m_freezeGeneration.load(std::memory_order_acquire);
        const bool freezePending = (version & 1) == 0
            && version == m_freezeVersion.load(std::memory_order_acquire)
            && freezeRequested != m_freezeAck.load(std::memory_order_relaxed);
        const bool staleWriter = m_writer
            && m_writer->generation != m_generation.load(std::memory_order_acquire);
        if (!freezePending && !staleWriter
            && sealRequested == m_sealAck.load(std::memory_order_relaxed))
        {
            return;
        }

        m_inHonor = true;
        if (freezePending)
        {
            truncate_open_scopes(freezeTick, freezeGeneration);
        }
        seal_current();
        m_sealAck.store(sealRequested, std::memory_order_release);
        if (freezePending)
        {
            // 나중에 들어온 요청까지 응답하지 않는다. seal 응답과 별개로
            // 확인하므로 프레임 봉인이 freeze를 영원히 가릴 수도 없다.
            m_freezeAck.store(freezeRequested, std::memory_order_release);
        }
        m_inHonor = false;
    }

    bool thread_stream::write(const profile_event& value, std::uint64_t generation, bool late_ingest)
    {
        if (generation != m_generation.load(std::memory_order_acquire))
        {
            // 이전 회차의 늦은 종료는 새 캡처의 용량 손실이 아니다.
            m_staleEvents.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (m_writer && m_writer->late_ingest != late_ingest)
        {
            seal_current(true);
        }
        if (!ensure_chunk(generation))
        {
            m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_writer->late_ingest = late_ingest;
        m_writer->events[m_writer->count] = value;
        m_writer->events[m_writer->count].thread_slot = static_cast<std::uint16_t>(m_info.slot);
        ++m_writer->count;
        ++m_sequence;
        return true;
    }

    void thread_stream::seal_current(bool writing)
    {
        if (m_writer)
        {
            event_chunk* chunk = m_writer;
            m_writer = nullptr;
            if (chunk->count == 0 && chunk->render_measurement_count == 0)
            {
                m_pool.release(chunk);
            }
            else
            {
                m_pool.seal(chunk);
            }
        }
        // false가 보이면 그 전에 청크의 소유권도 이미 넘어갔다. 봉인보다
        // 먼저 내리면 Stop이 빈 스트림으로 판단하고 마지막 청크를 놓친다.
        // 다음 이벤트를 쓰기 위해 청크를 바꾸는 중이면 빈 구간이 아니다.
        // 새 페이지 확보가 지연되어도 Stop은 아직 넘어오지 않은 꼬리를 센다.
        if (writing || recording_depth(m_generation.load(std::memory_order_acquire)) != 0)
        {
            m_pendingState.fetch_or(kPendingWork, std::memory_order_release);
        }
        else
        {
            m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingWork), std::memory_order_release);
        }
    }

    std::uint16_t thread_stream::recording_depth(std::uint64_t generation) const
    {
        std::uint16_t depth = 0;
        for (std::uint32_t i = 0; i < m_depth; ++i)
        {
            const open_scope& scope = m_stack[i];
            if (scope.token != 0 && scope.generation == generation
                && m_publishedScopes[i].token.load(std::memory_order_acquire) == scope.token)
            {
                ++depth;
            }
        }
        return depth;
    }

    void thread_stream::push_skipped_scope()
    {
        if (m_depth == kMaxScopeDepth)
        {
            ++m_overflowDepth;
            return;
        }
        m_stack[m_depth] = {};
        m_publishedScopes[m_depth].token.store(0, std::memory_order_release);
        ++m_depth;
        m_publishedDepth.store(m_depth, std::memory_order_release);
    }

    void thread_stream::skip_scope()
    {
        if (!owned_by_caller())
        {
            return;
        }
        honor_seal_request();
        push_skipped_scope();
    }

    void thread_stream::publish_open_scope(std::uint32_t index)
    {
        const open_scope& scope = m_stack[index];
        published_scope& published = m_publishedScopes[index];
        published.token.store(0, std::memory_order_release);
        // 필드도 release/acquire로 짝짓는다. 재사용된 필드를 하나라도 읽으면
        // 마지막 token 검사보다 무효화가 앞서므로, 옛 token으로 통과하지 못한다.
        published.generation.store(scope.generation, std::memory_order_release);
        published.tick_begin.store(scope.tick_begin, std::memory_order_release);
        published.marker.store(scope.marker, std::memory_order_release);
        published.frame.store(scope.frame, std::memory_order_release);
        published.depth.store(scope.depth, std::memory_order_release);
        published.flags.store(scope.flags, std::memory_order_release);
        published.session.store(scope.cpu.session, std::memory_order_release);
        published.tick.store(scope.cpu.tick, std::memory_order_release);
        published.task.store(scope.cpu.task, std::memory_order_release);
        published.token.store(scope.token, std::memory_order_release);
    }

    void thread_stream::begin_scope(marker_id id, profile_tick now, std::uint32_t frame,
                                    const cpu_span_context& cpu)
    {
        begin_scope(id, now, frame, cpu, m_generation.load(std::memory_order_acquire));
    }

    void thread_stream::begin_scope(marker_id id, profile_tick now, std::uint32_t frame,
                                    const cpu_span_context& cpu, std::uint64_t generation)
    {
        if (!owned_by_caller())
        {
            return;
        }
        m_pendingState.fetch_or(kPendingAdmission, std::memory_order_seq_cst);
        honor_seal_request();
        if (generation != m_generation.load(std::memory_order_acquire)
            || generation == m_frozenGeneration.load(std::memory_order_seq_cst))
        {
            push_skipped_scope();
            m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingAdmission), std::memory_order_release);
            return;
        }
        if (m_depth == kMaxScopeDepth || m_nextScopeToken == UINT64_MAX)
        {
            push_skipped_scope();
            m_droppedScopes.fetch_add(1, std::memory_order_relaxed);
            m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingAdmission), std::memory_order_release);
            return;
        }

        open_scope& scope = m_stack[m_depth];
        scope.tick_begin = now;
        scope.marker = id;
        scope.frame = frame;
        scope.cpu = cpu;
        scope.flags = event_flags::none;
        scope.generation = generation;
        scope.depth = recording_depth(generation);
        scope.token = m_nextScopeToken;
        m_nextScopeToken += 2;
        m_pendingState.fetch_or(kPendingWork, std::memory_order_release);
        publish_open_scope(m_depth);
        ++m_depth;
        m_publishedDepth.store(m_depth, std::memory_order_release);
        // 처음 확인한 뒤 들어온 Stop도 놓치지 않는다. 여기서 멈춘 producer는
        // pending admission과 공개된 시작점으로 미완료 상태를 드러낸다.
        honor_seal_request();
        m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingAdmission), std::memory_order_release);
    }

    bool thread_stream::claim_scope(std::uint32_t index)
    {
        std::uint64_t expected = m_stack[index].token;
        if (expected == 0)
        {
            return false;
        }
        return m_publishedScopes[index].token.compare_exchange_strong(
            expected, expected & ~std::uint64_t{ 1 }, std::memory_order_acq_rel);
    }

    void thread_stream::end_scope(profile_tick now)
    {
        if (!owned_by_caller())
        {
            return;
        }
        honor_seal_request();
        if (m_overflowDepth != 0)
        {
            --m_overflowDepth;
            return;
        }
        if (m_depth == 0)
        {
            m_unbalancedScopes.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        --m_depth;
        m_publishedDepth.store(m_depth, std::memory_order_release);
        const open_scope& scope = m_stack[m_depth];
        const bool claimed = claim_scope(m_depth);
        m_publishedScopes[m_depth].token.store(0, std::memory_order_release);
        if (!claimed)
        {
            return;
        }
        if (scope.generation != m_generation.load(std::memory_order_acquire))
        {
            m_staleScopes.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        profile_event value;
        value.tick_begin = scope.tick_begin;
        value.tick_end = (std::max)(now, scope.tick_begin);
        value.marker = scope.marker;
        value.frame = scope.frame;
        value.cpu = scope.cpu;
        value.depth = scope.depth;
        value.flags = scope.flags;
        // 요청의 마지막 버전이 아직 공개되지 않은 짧은 구간에도, 이미 닫힌
        // 세대의 실제 end가 Stop 뒤로 늘어난 완성 이벤트가 되지 않게 한다.
        if (scope.generation == m_frozenGeneration.load(std::memory_order_acquire))
        {
            const profile_tick cutoff = m_frozenTick.load(std::memory_order_acquire);
            if (scope.tick_begin > cutoff)
            {
                return;
            }
            value.tick_end = (std::min)(value.tick_end, cutoff);
            value.flags = value.flags | event_flags::truncated_end;
        }
        write(value, scope.generation);
        honor_seal_request();
    }

    bool thread_stream::read_open_scope(std::uint32_t index, std::uint64_t generation,
                                       profile_tick end_tick, profile_event& value,
                                       std::uint64_t& token) const
    {
        const published_scope& published = m_publishedScopes[index];
        token = published.token.load(std::memory_order_acquire);
        if ((token & 1) == 0 || published.generation.load(std::memory_order_acquire) != generation)
        {
            return false;
        }
        value = {};
        value.tick_begin = published.tick_begin.load(std::memory_order_acquire);
        value.tick_end = end_tick;
        value.marker = published.marker.load(std::memory_order_acquire);
        value.frame = published.frame.load(std::memory_order_acquire);
        value.depth = published.depth.load(std::memory_order_acquire);
        value.flags = published.flags.load(std::memory_order_acquire) | event_flags::truncated_end;
        value.cpu.session = published.session.load(std::memory_order_acquire);
        value.cpu.tick = published.tick.load(std::memory_order_acquire);
        value.cpu.task = published.task.load(std::memory_order_acquire);
        value.thread_slot = static_cast<std::uint16_t>(m_info.slot);
        return value.tick_begin <= end_tick
            && token == published.token.load(std::memory_order_acquire);
    }

    std::uint32_t thread_stream::snapshot_open_scopes(std::uint64_t generation, profile_tick end_tick,
                                                     profile_event* output, std::uint32_t capacity) const
    {
        if (!output)
        {
            return 0;
        }
        std::uint32_t count = 0;
        for (std::uint32_t i = 0; i < kMaxScopeDepth && count < capacity; ++i)
        {
            profile_event value;
            std::uint64_t token = 0;
            if (read_open_scope(i, generation, end_tick, value, token))
            {
                output[count++] = value;
            }
        }
        return count;
    }

    std::uint32_t thread_stream::claim_open_scopes(std::uint64_t generation, profile_tick end_tick,
                                                  profile_event* output, std::uint32_t capacity)
    {
        if (!output)
        {
            return 0;
        }
        std::uint32_t count = 0;
        for (std::uint32_t i = 0; i < kMaxScopeDepth && count < capacity; ++i)
        {
            profile_event value;
            std::uint64_t token = 0;
            if (read_open_scope(i, generation, end_tick, value, token)
                && m_publishedScopes[i].token.compare_exchange_strong(
                    token, token & ~std::uint64_t{ 1 }, std::memory_order_acq_rel))
            {
                output[count++] = value;
            }
        }
        return count;
    }

    void thread_stream::write_span(marker_id id, profile_tick begin, profile_tick end,
                                   std::uint32_t frame, std::uint16_t depth, const gpu_span_context& gpu)
    {
        if (!owned_by_caller())
        {
            return;
        }
        honor_seal_request();
        const std::uint64_t generation = gpu.generation != 0
            ? gpu.generation : m_generation.load(std::memory_order_acquire);
        if (generation != m_generation.load(std::memory_order_acquire))
        {
            m_staleEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (end < begin)
        {
            m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        profile_event value;
        value.tick_begin = begin;
        value.tick_end = end;
        value.marker = id;
        value.frame = frame;
        value.depth = depth;
        value.flags = event_flags::gpu_span;
        value.submission = gpu.submission;
        value.view = gpu.view;
        value.queue = gpu.queue;
        // GPU는 Stop의 제출 cutoff까지 늦게 도착한다. CPU의 admission cutoff를
        // 적용하지 않고, 제출할 때 잡은 세대를 그대로 청크에 싣는다.
        if (write(value, generation, true) && gpu.provenance.frame_kind != 0)
        {
            profile_render_measurement sample;
            sample.engine_frame = frame;
            sample.axis = profile_render_axis::gpu_pass;
            sample.queue = gpu.queue;
            sample.event_view = gpu.view;
            sample.event_submission = gpu.submission;
            sample.marker = id;
            sample.submission_id = gpu.submission_id;
            sample.tick_begin = begin;
            sample.tick_end = end;
            sample.provenance = gpu.provenance;
            write_render_measurement(sample, generation);
        }
    }

    void thread_stream::write_presenter_return(const profile_render_measurement& sample,
                                              std::uint64_t generation)
    {
        if (!owned_by_caller())
        {
            return;
        }
        // Match CPU instant admission: Stop sees in-flight ownership, and an
        // already frozen generation cannot admit a new observation afterward.
        m_pendingState.fetch_or(kPendingAdmission, std::memory_order_seq_cst);
        honor_seal_request();
        if (generation == m_generation.load(std::memory_order_acquire) &&
            generation != m_frozenGeneration.load(std::memory_order_seq_cst))
        {
            // Share the owner's ordinary CPU page. Existing frame/Stop seal
            // requests flush it at the next owner safe point; no per-Present
            // page allocation or blocking collector handoff is introduced.
            write_render_measurement(sample, generation, false);
        }
        honor_seal_request();
        m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingAdmission), std::memory_order_release);
    }

    void thread_stream::write_render_measurement(const profile_render_measurement& sample,
                                                std::uint64_t generation, bool late_ingest)
    {
        if (!owned_by_caller())
        {
            return;
        }
        honor_seal_request();
        if (generation != m_generation.load(std::memory_order_acquire))
        {
            m_staleEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!sample.valid())
        {
            m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (m_writer && m_writer->late_ingest != late_ingest)
        {
            seal_current(true);
        }
        if (!ensure_chunk(generation))
        {
            m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        m_writer->late_ingest = late_ingest;
        m_writer->render_measurements[m_writer->render_measurement_count++] = sample;
        ++m_sequence;
    }

    void thread_stream::write_instant(marker_id id, profile_tick tick, std::uint32_t frame,
                                      const cpu_span_context& cpu)
    {
        write_instant(id, tick, frame, cpu, m_generation.load(std::memory_order_acquire));
    }

    void thread_stream::write_instant(marker_id id, profile_tick tick, std::uint32_t frame,
                                      const cpu_span_context& cpu, std::uint64_t generation)
    {
        if (!owned_by_caller())
        {
            return;
        }
        m_pendingState.fetch_or(kPendingAdmission, std::memory_order_seq_cst);
        honor_seal_request();
        if (generation != m_generation.load(std::memory_order_acquire)
            || generation == m_frozenGeneration.load(std::memory_order_seq_cst))
        {
            m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingAdmission), std::memory_order_release);
            return;
        }
        profile_event value;
        value.tick_begin = tick;
        value.tick_end = tick;
        value.marker = id;
        value.frame = frame;
        value.depth = recording_depth(generation);
        value.flags = event_flags::instant;
        value.cpu = cpu;
        write(value, generation);
        honor_seal_request();
        m_pendingState.fetch_and(static_cast<std::uint8_t>(~kPendingAdmission), std::memory_order_release);
    }

    void thread_stream::publish_frame()
    {
        if (!owned_by_caller())
        {
            return;
        }
        // freeze를 처리하지 않은 채 seal만 응답하면 다음 안전 지점에서도
        // 요청이 없는 것으로 보였던 경로다. 응답은 honor 한 곳에서만 쓴다.
        honor_seal_request();
        seal_current();
    }

    void thread_stream::freeze_self(profile_tick freeze_tick)
    {
        if (!owned_by_caller())
        {
            return;
        }
        const std::uint64_t generation = m_generation.load(std::memory_order_acquire);
        m_frozenTick.store(freeze_tick, std::memory_order_release);
        m_frozenGeneration.store(generation, std::memory_order_seq_cst);
        honor_seal_request();
        truncate_open_scopes(freeze_tick, generation);
        seal_current();
    }

    void thread_stream::truncate_open_scopes(profile_tick freeze_tick)
    {
        if (!owned_by_caller())
        {
            return;
        }
        honor_seal_request();
        truncate_open_scopes(freeze_tick, m_generation.load(std::memory_order_acquire));
    }

    void thread_stream::truncate_open_scopes(profile_tick freeze_tick, std::uint64_t generation)
    {
        if (generation != m_generation.load(std::memory_order_acquire))
        {
            return;
        }
        for (std::uint32_t i = m_depth; i > 0; --i)
        {
            const open_scope& scope = m_stack[i - 1];
            if (scope.generation != generation || scope.tick_begin > freeze_tick || !claim_scope(i - 1))
            {
                continue;
            }
            profile_event value;
            value.tick_begin = scope.tick_begin;
            value.tick_end = freeze_tick;
            value.marker = scope.marker;
            value.frame = scope.frame;
            value.cpu = scope.cpu;
            value.depth = scope.depth;
            value.flags = scope.flags | event_flags::truncated_end;
            write(value, scope.generation);
        }
    }

    void thread_stream::finish(profile_tick now)
    {
        if (!owned_by_caller())
        {
            return;
        }
        honor_seal_request();
        m_overflowDepth = 0;
        while (m_depth != 0)
        {
            const open_scope& scope = m_stack[m_depth - 1];
            const bool open = scope.token != 0
                && m_publishedScopes[m_depth - 1].token.load(std::memory_order_acquire) == scope.token;
            if (open && scope.generation == m_generation.load(std::memory_order_acquire))
            {
                // end_scope의 선점/세대/Stop cutoff를 그대로 사용한다.
                m_unbalancedScopes.fetch_add(1, std::memory_order_relaxed);
                m_stack[m_depth - 1].flags = scope.flags | event_flags::truncated_end;
            }
            end_scope(now);
        }
        honor_seal_request();
        seal_current();
    }
}
