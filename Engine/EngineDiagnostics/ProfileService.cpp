#include "ProfileService.h"

#include <Windows.h>

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
	thread_local thread_stream* t_streams[kMaxLiveServices] = {};
}

namespace ce
{
	using namespace ce::detail::profile_service_impl;

	profiler_service::profiler_service()
		: m_serviceSlot(acquire_slot())
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

		m_pool.initialize(config.chunk_count);
		m_ring.configure(config.retained_frames, config.memory_budget);
		m_frameBeginTick = now();
		m_initialized.store(true, std::memory_order_release);
	}

	void profiler_service::shutdown()
	{
		if (!m_initialized.exchange(false, std::memory_order_acq_rel))
		{
			return;
		}

		m_state.store(recorder_state::stopped, std::memory_order_release);

		{
			std::lock_guard<std::mutex> guard(m_streamLock);
			for (std::size_t i = 0; i < m_streams.size(); ++i)
			{
				destroy_stream_locked(i);
			}
			m_streams.clear();
			m_threadInfo.clear();
		}

		// 봉인된 것을 마저 거둔 뒤 풀을 접는다. 여기서 빼먹으면 종료 직전
		// 프레임이 통째로 사라진다.
		collect_sealed();
		m_pool.shutdown();

		{
			std::lock_guard<std::mutex> guard(m_captureLock);
			m_capture.reset();
		}
		m_ring.clear();
	}

	void profiler_service::destroy_stream_locked(std::size_t index)
	{
		stream_entry& entry = m_streams[index];
		if (!entry.stream)
		{
			return;
		}

		entry.stream->finish(now());
		m_retiredDropped.fetch_add(entry.stream->dropped_events(), std::memory_order_relaxed);
		m_retiredUnbalanced.fetch_add(entry.stream->unbalanced_scopes(), std::memory_order_relaxed);

		// ★ 이 스레드가 이 서비스에서 쓰던 자리를 끊는다. 옛 코어가 죽은
		//   thread_local 을 계속 가리켜 이후 모든 수집이 UAF 였던 자리다.
		if (t_streams[m_serviceSlot] == entry.stream.get())
		{
			t_streams[m_serviceSlot] = nullptr;
		}

		entry.stream.reset();
		entry.live = false;
	}

	void profiler_service::register_thread(const char* name)
	{
		if (!m_initialized.load(std::memory_order_acquire))
		{
			return;
		}

		if (t_streams[m_serviceSlot])
		{
			return;   // 이미 등록됨
		}

		const std::uint32_t os_id = static_cast<std::uint32_t>(::GetCurrentThreadId());

		std::lock_guard<std::mutex> guard(m_streamLock);

		thread_info info;
		info.os_thread_id = os_id;
		info.slot = static_cast<std::uint32_t>(m_streams.size());
		if (name && *name)
		{
			info.name = name;
		}
		else
		{
			info.name = "Thread " + std::to_string(info.slot);
		}

		auto stream = std::make_unique<thread_stream>(m_pool, info);
		t_streams[m_serviceSlot] = stream.get();

		stream_entry entry;
		entry.stream = std::move(stream);
		entry.os_thread_id = os_id;
		entry.live = true;

		m_streams.push_back(std::move(entry));
		m_threadInfo.push_back(std::move(info));
	}

	void profiler_service::unregister_thread()
	{
		thread_stream* stream = t_streams[m_serviceSlot];
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
		t_streams[m_serviceSlot] = nullptr;
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

	thread_stream* profiler_service::current_stream()
	{
		thread_stream* stream = t_streams[m_serviceSlot];
		if (stream)
		{
			return stream;
		}

		// 등록하지 않은 스레드가 마커를 찍으면 그 자리에서 등록한다.
		// 이름이 없으면 슬롯 번호로 붙는다 — 잃는 것보다 낫다.
		register_thread(nullptr);
		return t_streams[m_serviceSlot];
	}

	void profiler_service::begin_scope(marker_id id)
	{
		if (m_state.load(std::memory_order_relaxed) != recorder_state::recording)
		{
			return;
		}

		thread_stream* stream = current_stream();
		if (!stream)
		{
			return;
		}

		stream->begin_scope(id, now(), m_engineFrame.load(std::memory_order_relaxed));
	}

	void profiler_service::end_scope()
	{
		if (m_state.load(std::memory_order_relaxed) != recorder_state::recording)
		{
			return;
		}

		thread_stream* stream = t_streams[m_serviceSlot];
		if (!stream)
		{
			return;
		}

		stream->end_scope(now());
	}

	void profiler_service::collect_sealed()
	{
		event_chunk* sealed = m_pool.take_sealed();
		if (!sealed)
		{
			return;
		}

		m_ring.ingest(sealed);
		m_pool.release(sealed);
	}

	void profiler_service::publish_frame(std::uint32_t engine_frame)
	{
		if (!m_initialized.load(std::memory_order_acquire))
		{
			return;
		}

		if (m_state.load(std::memory_order_acquire) != recorder_state::recording)
		{
			return;
		}

		// 등록된 모든 스트림이 자기 청크를 봉인한다. 열린 스코프는 닫지
		// 않는다 — 프레임을 넘는 구간을 잃지 않기 위해서다.
		std::uint64_t dropped = 0;
		{
			std::lock_guard<std::mutex> guard(m_streamLock);
			for (stream_entry& entry : m_streams)
			{
				if (entry.stream)
				{
					entry.stream->publish_frame();
					dropped += entry.stream->dropped_events();
				}
			}
		}

		collect_sealed();

		const profile_tick tick = now();
		// 누적 드롭에서 지난번까지의 몫을 빼 이번 프레임의 증분만 넘긴다.
		const std::uint64_t total = dropped + m_retiredDropped.load(std::memory_order_relaxed);
		if (total > m_ring.dropped_events())
		{
			m_ring.note_dropped(total - m_ring.dropped_events());
		}

		m_ring.close_frame(engine_frame, m_frameBeginTick, tick);
		m_frameBeginTick = tick;

		// 다음 프레임 번호를 예측해 올린다. 밖이 실제로 그 번호를 주면
		// 일치하고, 건너뛰더라도 다음 publish 가 라벨을 정확히 붙인다.
		m_engineFrame.store(engine_frame + 1, std::memory_order_relaxed);
	}

	void profiler_service::record(std::uint32_t first_frame)
	{
		if (!m_initialized.load(std::memory_order_acquire))
		{
			return;
		}
		m_engineFrame.store(first_frame, std::memory_order_relaxed);
		m_frameBeginTick = now();
		m_state.store(recorder_state::recording, std::memory_order_release);
	}

	void profiler_service::pause()
	{
		if (m_state.load(std::memory_order_acquire) != recorder_state::recording)
		{
			return;
		}

		// producer 를 먼저 멈춘다. 그 뒤에 봉인된 것을 거둬야 얼린 캡처가
		// 멈춘 시점까지를 온전히 담는다.
		m_state.store(recorder_state::frozen, std::memory_order_release);

		{
			std::lock_guard<std::mutex> guard(m_streamLock);
			for (stream_entry& entry : m_streams)
			{
				if (entry.stream)
				{
					entry.stream->publish_frame();
				}
			}
		}
		collect_sealed();

		std::vector<thread_info> threads;
		{
			std::lock_guard<std::mutex> guard(m_streamLock);
			threads = m_threadInfo;
		}

		capture_session_ptr frozen = m_ring.freeze(threads);
		{
			std::lock_guard<std::mutex> guard(m_captureLock);
			m_capture = std::move(frozen);
		}
	}

	void profiler_service::clear()
	{
		m_ring.clear();
		std::lock_guard<std::mutex> guard(m_captureLock);
		m_capture.reset();
	}

	capture_session_ptr profiler_service::capture() const
	{
		std::lock_guard<std::mutex> guard(m_captureLock);
		return m_capture;
	}

	live_summary profiler_service::summary() const
	{
		live_summary value;
		value.state = m_state.load(std::memory_order_acquire);
		value.engine_frame = m_engineFrame.load(std::memory_order_relaxed);
		value.retained_frames = m_ring.retained_frames();
		value.last_frame_events = m_ring.last_frame_events();
		value.peak_frame_events = m_ring.peak_frame_events();
		value.dropped_events = m_ring.dropped_events();
		value.memory_bytes = m_ring.memory_bytes();
		value.memory_budget = kDefaultMemoryBudget;
		value.registered_markers = registered_marker_count();
		value.free_chunks = m_pool.free_count();
		value.chunk_count = m_pool.chunk_count();

		std::uint64_t unbalanced = m_retiredUnbalanced.load(std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> guard(m_streamLock);
			value.thread_count = static_cast<std::uint32_t>(m_threadInfo.size());
			for (const stream_entry& entry : m_streams)
			{
				if (entry.stream)
				{
					unbalanced += entry.stream->unbalanced_scopes();
				}
			}
		}
		value.unbalanced_scopes = unbalanced;

		{
			std::lock_guard<std::mutex> guard(m_captureLock);
			if (m_capture)
			{
				value.total_events = m_capture->total_events();
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
