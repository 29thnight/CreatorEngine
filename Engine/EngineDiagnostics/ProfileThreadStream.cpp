#include "ProfileThreadStream.h"

#include <utility>

namespace ce
{
	//-------------------------------------------------------------------------
	// chunk_pool
	//-------------------------------------------------------------------------

	chunk_pool::~chunk_pool()
	{
		shutdown();
	}

	void chunk_pool::initialize(std::uint32_t chunk_count)
	{
		std::lock_guard<std::mutex> guard(m_lock);

		m_storage.clear();
		m_storage.reserve(chunk_count);
		m_free = nullptr;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		m_chunkCount = chunk_count;
		m_freeCount = chunk_count;

		// 한 번에 다 잡아 둔다. hot path 에서 heap 을 건드리지 않는 것이
		// 계약이므로(§6.2) 이후의 할당은 존재하지 않는다 — 옛 코어의
		// BeginEvent 가 버퍼를 resize 하던 자리가 여기서 사라진다.
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
		m_free = nullptr;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		m_freeCount = 0;
		m_chunkCount = 0;
		m_storage.clear();
	}

	event_chunk* chunk_pool::acquire()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		if (!m_free)
		{
			return nullptr;
		}

		event_chunk* chunk = m_free;
		m_free = chunk->next;
		chunk->next = nullptr;
		--m_freeCount;
		return chunk;
	}

	void chunk_pool::seal(event_chunk* chunk)
	{
		if (!chunk)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(m_lock);

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
			chunk_list->next = m_free;
			m_free = chunk_list;
			++m_freeCount;
			chunk_list = next;
		}
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
		: m_pool(pool)
		, m_info(std::move(info))
	{
	}

	thread_stream::~thread_stream()
	{
		// 남은 청크를 잃지 않는다. 열린 스코프는 finish() 가 닫았어야 하지만,
		// 안 불렸더라도 여기서 청크는 넘어간다.
		seal_current();
	}

	bool thread_stream::ensure_chunk()
	{
		if (m_writer && !m_writer->full())
		{
			return true;
		}

		if (m_writer)
		{
			seal_current();
		}

		m_writer = m_pool.acquire();
		if (!m_writer)
		{
			return false;
		}

		m_writer->reset(m_info.slot, m_sequence);
		return true;
	}

	void thread_stream::write(const profile_event& value)
	{
		if (!ensure_chunk())
		{
			++m_droppedEvents;
			return;
		}

		m_writer->events[m_writer->count] = value;
		++m_writer->count;
		++m_sequence;
	}

	void thread_stream::seal_current()
	{
		if (!m_writer)
		{
			return;
		}

		event_chunk* chunk = m_writer;
		m_writer = nullptr;

		if (chunk->count == 0)
		{
			// 빈 청크는 수집기에 보내지 않는다 — 봉인 목록에 빈 것이 섞이면
			// "이 프레임에 아무 일도 없었다" 와 "아직 안 왔다" 가 구분되지 않는다.
			m_pool.release(chunk);
			return;
		}

		m_pool.seal(chunk);
	}

	void thread_stream::begin_scope(marker_id id, profile_tick now, std::uint32_t frame)
	{
		if (m_depth >= kMaxScopeDepth)
		{
			// 버리고 센다. 옛 코어는 이 자리에서 스택 밖에 썼다.
			//
			// ★ 못 연 것을 따로 세어 두는 것이 핵심이다. 이 스코프도 언젠가
			//   닫히는데, 그때 스택에서 하나 꺼내면 **남의 구간을 닫는다** —
			//   RAII 표기라도 짝이 어긋난다. 스코프는 LIFO 이므로 못 연 것이
			//   언제나 가장 안쪽이고, 따라서 end_scope 가 이 수를 먼저 소비하면
			//   정확히 제 짝을 만난다.
			++m_skippedDepth;
			++m_droppedScopes;
			return;
		}

		open_scope& scope = m_stack[m_depth];
		scope.tick_begin = now;
		scope.marker = id;
		scope.frame = frame;
		scope.flags = event_flags::none;
		++m_depth;
	}

	void thread_stream::end_scope(profile_tick now)
	{
		// 깊이 상한을 넘겨 못 연 스코프의 짝이 먼저다(begin_scope 의 주석).
		// 이것을 스택보다 먼저 보지 않으면 남의 구간을 닫는다.
		if (m_skippedDepth > 0)
		{
			--m_skippedDepth;
			return;
		}

		if (m_depth == 0)
		{
			// 열지 않고 닫았다. RAII 표기에서는 나올 수 없는 경로다 —
			// 남는다면 손으로 짝을 맞춘 코드가 어딘가 있다는 뜻이므로 센다.
			++m_unbalancedScopes;
			return;
		}

		--m_depth;
		const open_scope& scope = m_stack[m_depth];

		profile_event value;
		value.tick_begin = scope.tick_begin;
		value.tick_end = now;
		value.marker = scope.marker;
		value.frame = scope.frame;
		value.depth = static_cast<std::uint16_t>(m_depth);
		value.flags = scope.flags;
		write(value);
	}

	void thread_stream::publish_frame()
	{
		// ★ 열린 스코프를 닫지 않는다. 프레임을 넘는 구간은 닫히는 프레임에
		//   기록되고, 시작 프레임 번호를 들고 있으므로 분석기가 어느 프레임에
		//   걸쳐 있었는지 복원할 수 있다. 옛 코어는 여기서 스택 맨 위를 무조건
		//   닫았고(그래서 "CPU Frame" 대신 남의 구간을 닫았다) 그것이
		//   cross-frame/preserve 가 기지 결함으로 남아 있던 이유다.
		seal_current();
	}

	void thread_stream::finish(profile_tick now)
	{
		// 아직 열려 있는 것을 잃지 않고 닫는다 — 끝을 못 본 구간이라고 표시해서.
		while (m_depth > 0)
		{
			--m_depth;
			const open_scope& scope = m_stack[m_depth];

			profile_event value;
			value.tick_begin = scope.tick_begin;
			value.tick_end = now;
			value.marker = scope.marker;
			value.frame = scope.frame;
			value.depth = static_cast<std::uint16_t>(m_depth);
			value.flags = scope.flags | event_flags::truncated_end;
			write(value);

			++m_unbalancedScopes;
		}

		seal_current();
	}
}
