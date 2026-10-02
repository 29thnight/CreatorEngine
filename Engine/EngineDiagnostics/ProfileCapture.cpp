#include "ProfileCapture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

namespace ce
{
	namespace detail::counter_registry_impl
	{
		struct registry
		{
			std::mutex lock;
			std::vector<capture_counter> entries{
				{ profile_counter_id::process_cpu_percent, "CPU", "%", counter_category::process },
				{ profile_counter_id::process_ram_mb, "RAM", "MB", counter_category::process },
				{ profile_counter_id::gpu_vram_mb, "VRAM", "MB", counter_category::gpu },
				{ profile_counter_id::lan_send_bytes_per_second, "LAN send", "B/s", counter_category::network },
				{ profile_counter_id::lan_receive_bytes_per_second, "LAN receive", "B/s", counter_category::network },
				{ profile_counter_id::upload_bytes, "Upload", "B", counter_category::render },
				{ profile_counter_id::upload_overflows, "Upload rejected batches", "count", counter_category::render },
				{ profile_counter_id::descriptor_allocations, "Descriptors", "count", counter_category::render },
				{ profile_counter_id::descriptor_overflows, "Descriptor overflow", "count", counter_category::render },
				{ profile_counter_id::draw_calls, "GBuffer draws", "count", counter_category::render },
				{ profile_counter_id::batches, "GBuffer batches", "count", counter_category::render },
				{ profile_counter_id::gc_gen0_collections, "GC Gen0", "count", counter_category::managed },
				{ profile_counter_id::gc_gen1_collections, "GC Gen1", "count", counter_category::managed },
				{ profile_counter_id::gc_gen2_collections, "GC Gen2", "count", counter_category::managed },
				{ profile_counter_id::gc_heap_mb, "GC heap", "MB", counter_category::managed },
				{ profile_counter_id::gc_fragmented_mb, "GC fragmented", "MB", counter_category::managed },
				{ profile_counter_id::gc_pause_percent, "GC pause", "%", counter_category::managed },
				{ profile_counter_id::resource_models, "Models", "count", counter_category::resources },
				{ profile_counter_id::resource_materials, "Materials", "count", counter_category::resources },
				{ profile_counter_id::resource_textures, "Textures", "count", counter_category::resources },
				{ profile_counter_id::resource_proxies, "Render proxies", "count", counter_category::resources },
				{ profile_counter_id::provider_process_us, "Process provider cost", "us", counter_category::process },
				{ profile_counter_id::provider_render_us, "Render provider cost", "us", counter_category::render },
				{ profile_counter_id::provider_gc_us, "GC provider cost", "us", counter_category::managed },
				{ profile_counter_id::provider_resources_us, "Resource provider cost", "us", counter_category::resources },
				{ profile_counter_id::physics_bodies, "Physics.Bodies", "bodies", counter_category::physics },
				{ profile_counter_id::physics_shapes, "Physics.Shapes", "shapes", counter_category::physics },
				{ profile_counter_id::physics_characters, "Physics.Characters", "characters", counter_category::physics },
				{ profile_counter_id::physics_active_bodies, "Physics.Active bodies", "bodies/tick", counter_category::physics },
				{ profile_counter_id::physics_active_shapes, "Physics.Active shapes", "shapes/tick", counter_category::physics },
				{ profile_counter_id::physics_changed_bodies, "Physics.Changed bodies", "bodies/interval", counter_category::physics },
				{ profile_counter_id::physics_changed_shapes, "Physics.Changed shapes", "shapes/interval", counter_category::physics },
				{ profile_counter_id::physics_changed_characters, "Physics.Changed characters", "characters/interval", counter_category::physics },
				{ profile_counter_id::physics_commands_applied, "Physics.Applied commands", "commands/tick", counter_category::physics },
				{ profile_counter_id::physics_commands_failed, "Physics.Failed commands", "commands/tick", counter_category::physics },
				{ profile_counter_id::physics_commands_cancelled, "Physics.Cancelled commands", "commands/tick", counter_category::physics },
				{ profile_counter_id::physics_commands_queued, "Physics.Queued commands", "commands", counter_category::physics },
				{ profile_counter_id::physics_events_stored, "Physics.Stored events", "events/tick", counter_category::physics },
				{ profile_counter_id::physics_required_events, "Physics.Required events", "events/tick", counter_category::physics },
				{ profile_counter_id::physics_dropped_events, "Physics.Dropped events", "events/tick", counter_category::physics },
				{ profile_counter_id::physics_contacts_stored, "Physics.Stored contact points", "points/tick", counter_category::physics },
				{ profile_counter_id::physics_required_contacts, "Physics.Required contact points", "points/tick", counter_category::physics },
				{ profile_counter_id::physics_dropped_contacts, "Physics.Dropped contact points", "points/tick", counter_category::physics },
				{ profile_counter_id::physics_unresolved_identities, "Physics.Unresolved identities", "pairs/tick", counter_category::physics },
				{ profile_counter_id::physics_queries, "Physics.Queries", "queries/interval", counter_category::physics },
				{ profile_counter_id::physics_query_hits, "Physics.Required query hits", "hits/interval", counter_category::physics },
				{ profile_counter_id::physics_query_overflows, "Physics.Query overflows", "queries/interval", counter_category::physics },
				{ profile_counter_id::physics_workers, "Physics.SDK workers", "workers", counter_category::physics },
				{ profile_counter_id::physics_tasks_submitted, "Physics.SDK tasks submitted", "tasks/interval", counter_category::physics },
				{ profile_counter_id::physics_tasks_completed, "Physics.SDK tasks completed", "tasks/interval", counter_category::physics },
				{ profile_counter_id::physics_tasks_inline, "Physics.Inline SDK tasks", "tasks/interval", counter_category::physics },
				{ profile_counter_id::physics_snapshot_buffers, "Physics.Snapshot buffers", "buffers", counter_category::physics },
				{ profile_counter_id::physics_snapshot_buffers_in_use, "Physics.Snapshot buffers in use", "buffers", counter_category::physics },
				{ profile_counter_id::physics_tick_buffer_bytes, "Physics.Tick buffer capacity", "B", counter_category::physics },
				{ profile_counter_id::physics_tick_buffer_peak_bytes, "Physics.Peak tick buffer capacity", "B", counter_category::physics },
				{ profile_counter_id::physics_query_scratch_peak_bytes, "Physics.Peak query stack scratch", "B", counter_category::physics },
				{ profile_counter_id::physics_step_failed, "Physics.Step failed", "0/1", counter_category::physics },
				{ profile_counter_id::physics_command_rejections, "Physics.Rejected input commands", "commands/interval", counter_category::physics },
				{ profile_counter_id::physics_command_overflows, "Physics.Input queue overflows", "commands/interval", counter_category::physics },
				{ profile_counter_id::physics_max_command_wait_ticks, "Physics.Maximum command residence", "ticks", counter_category::physics },
			};
		};
		registry& instance()
		{
			static registry value;
			return value;
		}
	}

	profile_counter_id register_counter(std::string_view name, std::string_view unit,
	                                    counter_category category)
	{
		if (name.empty() || name.size() > 255 || unit.size() > 32)
			return static_cast<profile_counter_id>(0);
		auto& registry = detail::counter_registry_impl::instance();
		std::lock_guard guard(registry.lock);
		for (const auto& entry : registry.entries)
			if (entry.name == name)
				return entry.unit == unit && entry.category == category
					? entry.id : static_cast<profile_counter_id>(0);
		if (registry.entries.size() >= (std::numeric_limits<std::uint16_t>::max)())
			return static_cast<profile_counter_id>(0);
		const auto id = static_cast<profile_counter_id>(registry.entries.size() + 1);
		registry.entries.push_back({ id, std::string(name), std::string(unit), category });
		return id;
	}

	std::vector<capture_counter> snapshot_counters()
	{
		auto& registry = detail::counter_registry_impl::instance();
		std::lock_guard guard(registry.lock);
		return registry.entries;
	}

	counter_mask counter_category_bit(profile_counter_id id)
	{
		const auto number = static_cast<std::uint16_t>(id);
		if (number >= 1 && number <= 2) return counter_bit(counter_category::process);
		if (number == 3) return counter_bit(counter_category::gpu);
		if (number >= 4 && number <= 5) return counter_bit(counter_category::network);
		if (number >= 6 && number <= 11) return counter_bit(counter_category::render);
		if (number >= 12 && number <= 17) return counter_bit(counter_category::managed);
		if (number >= 18 && number <= 21) return counter_bit(counter_category::resources);
		if (number == 22) return counter_bit(counter_category::process);
		if (number == 23) return counter_bit(counter_category::render);
		if (number == 24) return counter_bit(counter_category::managed);
		if (number == 25) return counter_bit(counter_category::resources);
		if (number >= 26 && number <= 60) return counter_bit(counter_category::physics);
		auto& registry = detail::counter_registry_impl::instance();
		std::lock_guard guard(registry.lock);
		if (number > 0 && number <= registry.entries.size())
			return counter_bit(registry.entries[number - 1].category);
		return 0;
	}

	const capture_counter* find_counter(std::span<const capture_counter> counters,
	                                    profile_counter_id id)
	{
		const auto number = static_cast<std::uint16_t>(id);
		if (number > 0 && number <= counters.size() && counters[number - 1].id == id)
			return &counters[number - 1];
		for (const auto& descriptor : counters)
			if (descriptor.id == id) return &descriptor;
		return nullptr;
	}

	const profile_event& frame_events::operator[](std::size_t index) const
	{
		assert(index < m_count);
		std::size_t low = 0;
		std::size_t high = m_segments.size();
		while (low + 1 < high)
		{
			const std::size_t middle = low + (high - low) / 2;
			if (m_segments[middle].begin <= index) low = middle;
			else high = middle;
		}
		const segment& part = m_segments[low];
		const std::size_t offset = part.offset + index - part.begin;
		return part.page ? part.page->events[offset] : (*part.owned)[offset];
	}

	void frame_events::materialize()
	{
		if (m_segments.size() == 1 && m_segments[0].owned
		    && m_segments[0].owned.use_count() == 1) return;
		auto values = std::make_shared<std::vector<profile_event>>();
		values->reserve(m_count);
		for (const profile_event& value : *this) values->push_back(value);
		m_segments.clear();
		m_segments.push_back(segment{ {}, std::move(values), 0, 0,
			static_cast<std::uint32_t>(m_count) });
		m_memoryBytes = m_segments[0].owned->capacity() * sizeof(profile_event);
	}

	profile_event& frame_events::mutable_at(std::size_t index)
	{
		assert(index < m_count);
		materialize();
		return (*m_segments[0].owned)[index];
	}

	void frame_events::push_back(const profile_event& value)
	{
		if (m_segments.empty() || !m_segments.back().owned
		    || m_segments.back().owned.use_count() != 1)
		{
			auto values = std::make_shared<std::vector<profile_event>>();
			values->reserve(8);
			m_memoryBytes += values->capacity() * sizeof(profile_event);
			m_segments.push_back(segment{ {}, std::move(values), m_count, 0, 0 });
		}
		segment& last = m_segments.back();
		const std::size_t before = last.owned->capacity();
		last.owned->push_back(value);
		m_memoryBytes += (last.owned->capacity() - before) * sizeof(profile_event);
		++last.count;
		++m_count;
	}

	void frame_events::resize(std::size_t count)
	{
		if (count == 0)
		{
			m_segments.clear();
			m_count = m_memoryBytes = 0;
			return;
		}
		materialize();
		m_segments[0].owned->resize(count);
		m_segments[0].count = static_cast<std::uint32_t>(count);
		m_count = count;
		m_memoryBytes = m_segments[0].owned->capacity() * sizeof(profile_event);
	}

	void frame_events::append_page(std::shared_ptr<const event_chunk> page,
	                               std::uint32_t offset, std::uint32_t count)
	{
		if (!page || count == 0) return;
		assert(offset + count <= page->count);
		if (!m_segments.empty())
		{
			segment& last = m_segments.back();
			if (last.page.get() == page.get() && last.offset + last.count == offset)
			{
				last.count += count;
				m_count += count;
				return;
			}
		}
		bool firstReference = true;
		for (const segment& existing : m_segments)
		{
			if (existing.page.get() == page.get()) { firstReference = false; break; }
		}
		m_segments.push_back(segment{ std::move(page), {}, m_count, offset, count });
		m_count += count;
		if (firstReference) m_memoryBytes += sizeof(event_chunk);
	}

	//-------------------------------------------------------------------------
	// capture_session
	//-------------------------------------------------------------------------

	capture_session::capture_session(std::vector<frame_record>   frames,
	                                 std::vector<thread_info>    threads,
	                                 std::vector<capture_marker> markers,
	                                 capture_environment         environment,
	                                 bool                        complete,
	                                 std::uint32_t               unacked_streams,
	                                 std::uint64_t               dropped_counters,
	                                 std::vector<capture_counter> counters)
		: m_frames(std::move(frames))
		, m_threads(std::move(threads))
		, m_markers(std::move(markers))
		, m_counters(std::move(counters))
		, m_environment(environment)
		, m_droppedCounters(dropped_counters)
		, m_complete(complete)
		, m_unackedStreams(unacked_streams)
	{
		if (m_counters.empty())
		{
			auto builtins = snapshot_counters();
			m_counters.assign(builtins.begin(), builtins.begin() + (std::min)(builtins.size(), std::size_t{ 25 }));
		}
		for (const frame_record& frame : m_frames)
		{
			m_memoryBytes += frame.memory_bytes();
			m_totalEvents += frame.events.size();
		}
	}

	double capture_session::milliseconds(profile_tick ticks) const
	{
		// 주파수를 모르는 캡처는 0 을 낸다. 이 기계의 QPC 로 대신 나누지
		// **않는다** — 그러면 남의 기계에서 뜬 캡처가 그럴듯한 거짓 숫자를 낸다.
		if (0 == m_environment.ticks_per_second)
		{
			return 0.0;
		}
		return static_cast<double>(ticks) * 1000.0 /
		       static_cast<double>(m_environment.ticks_per_second);
	}

	std::uint32_t capture_session::marker_count() const
	{
		// **이 캡처가 들고 있는** 표의 크기다. 전역 registry 의 수가 아니다 —
		// 그것을 내면 얼린 뒤 등록된 마커가 이 캡처에 섞여 보인다.
		return static_cast<std::uint32_t>(m_markers.size());
	}

	const capture_marker& capture_session::marker(marker_id id) const
	{
		// 표 밖의 id 는 **빈 이름**이다. 전역 registry 로 물러나지 않는다 —
		// 그러면 파일에서 온 캡처가 이 프로세스의 남의 이름을 그린다.
		static const capture_marker unknown{};
		if (id >= m_markers.size())
		{
			return unknown;
		}
		return m_markers[id];
	}

	const frame_record* capture_session::find_frame(std::uint32_t engine_frame) const
	{
		// 프레임은 증가 순으로 들어오므로 이분 탐색이 성립한다.
		const auto found = std::lower_bound(
			m_frames.begin(), m_frames.end(), engine_frame,
			[](const frame_record& record, std::uint32_t value) { return record.engine_frame < value; });

		if (found == m_frames.end() || found->engine_frame != engine_frame)
		{
			return nullptr;
		}
		return &(*found);
	}

	//-------------------------------------------------------------------------
	// capture_ring
	//-------------------------------------------------------------------------

	void capture_ring::configure(std::uint32_t retained_frames, std::size_t memory_budget)
	{
		m_retainedFrames = retained_frames > 0 ? retained_frames : kDefaultRetainedFrames;
		m_memoryBudget = memory_budget > 0 ? memory_budget : kDefaultMemoryBudget;
	}

	void capture_ring::clear()
	{
		m_frames.clear();
		m_pending = frame_record{};
		m_deferredCounters.clear();
		m_droppedCounters = 0;
		m_memoryBytes = 0;
		m_droppedEvents = 0;
		m_lastFrameEvents = 0;
		m_peakFrameEvents = 0;
		m_deferredSpans.clear();
		m_lateSpansPlaced = 0;
		m_lateSpansDropped = 0;
		m_staleChunksDropped = 0;
		m_malformedPages = 0;
		m_ingestedPages = 0;
		m_lateEventsPlaced = 0;
		m_lateEventsDropped = 0;
	}

	void capture_ring::ingest(event_chunk* sealed_list, std::shared_ptr<chunk_pool> pool,
	                          std::uint64_t generation, profile_tick frame_begin_tick)
	{
		while (sealed_list)
		{
			event_chunk* const next = sealed_list->next;
			sealed_list->next = nullptr;
			// 캡처가 이 페이지의 마지막 참조를 놓아야 free 목록에 돌아간다.
			// 풀도 함께 붙잡으므로 서비스 종료 뒤의 얼린 캡처가 안전하다.
			std::shared_ptr<const event_chunk> page(sealed_list,
				[pool](const event_chunk* value)
				{
					pool->release(const_cast<event_chunk*>(value));
				});
			if (page->magic != kProfilePageMagic || page->version != kProfilePageVersion
			    || page->event_bytes != sizeof(profile_event)
			    || page->count > kEventsPerChunk)
			{
				++m_malformedPages;
				note_dropped((std::min)(page->count, kEventsPerChunk));
				sealed_list = next;
				continue;
			}
			// ★ 지운 세대의 것은 받지 않는다. 잠든 워커가 Clear **전에** 적은
			//   것을 들고 깨어나면, 그것은 이미 없어진 녹화의 자료다.
			if (page->generation != generation)
			{
				m_staleChunksDropped += page->count;
				sealed_list = next;
				continue;
			}
			++m_ingestedPages;

			if (page->late_ingest)
			{
				// ★ 늦게 온 것은 **수집한 프레임**이 아니라 제 프레임 칸으로
				//   돌려보낸다. 이 갈래가 없으면 GPU 일이 세 칸 뒤에 그려진다.
				for (std::uint32_t i = 0; i < page->count; ++i)
				{
					place_late_span(page->events[i], page, i);
				}
				sealed_list = next;
				continue;
			}

			// 청크 안의 순서는 writer 가 지켰다. 대부분의 페이지는 전부
			// 현재 프레임이므로 인덱스 하나로 붙이고 이벤트를 복사하지 않는다.
			//
			// ★ 다만 **지금 프레임보다 앞서 끝난** 이벤트는 그대로 두면 안 된다.
			//   writer 가 자기 일정으로 봉인하므로 CPU 구간도 늦게 올 수 있고,
			//   늦은 것을 여기 담으면 프레임 귀속이 그만큼 틀어진다(§0.5.15).
			bool allCurrent = true;
			for (std::uint32_t i = 0; i < page->count; ++i)
			{
				if (0 != frame_begin_tick && page->events[i].tick_end < frame_begin_tick)
				{
					allCurrent = false;
					break;
				}
			}
			if (allCurrent)
			{
				m_pending.events.append_page(page, 0, page->count);
			}
			else
			{
				for (std::uint32_t i = 0; i < page->count; ++i)
				{
					const profile_event& value = page->events[i];
					if (0 != frame_begin_tick && value.tick_end < frame_begin_tick
					    && place_by_tick(value, page, i)) continue;
					m_pending.events.append_page(page, i, 1);
				}
			}
			sealed_list = next;
		}
	}

	bool capture_ring::place_by_tick(const profile_event& value,
	                                 const std::shared_ptr<const event_chunk>& page,
	                                 std::uint32_t offset)
	{
		// 뒤에서부터 찾는다. 늦게 오는 것은 대개 가장 최근 몇 프레임의 것이다.
		for (std::size_t i = m_frames.size(); i > 0; --i)
		{
			frame_record& frame = m_frames[i - 1];

			// 이 칸보다 **뒤에** 끝난 것이면 더 오래된 칸을 봐도 소용없다.
			// 지금 프레임에 두는 것이 맞다.
			if (value.tick_end >= frame.tick_end) return false;
			if (value.tick_end < frame.tick_begin) continue;

			const std::size_t before = frame.memory_bytes();
			frame.events.append_page(page, offset, 1);
			m_memoryBytes += frame.memory_bytes() - before;
			++m_lateEventsPlaced;
			return true;
		}

		// 링의 가장 오래된 칸보다도 앞선다. 지금 프레임에 담으면 귀속이
		// 틀어지므로 담지 않고 **센다** — 조용히 섞으면 틀린 수치가 정상처럼 보인다.
		++m_lateEventsDropped;
		return true;
	}

	void capture_ring::place_late_span(const profile_event& value,
	                                   const std::shared_ptr<const event_chunk>& page,
	                                   std::uint32_t offset)
	{
		// 뒤에서부터 찾는다. 늦게 오는 것은 대개 가장 최근 몇 프레임의 것이고,
		// 실측에서 제출→수집이 최대 54.6 ms(세 프레임 남짓)였다.
		for (std::size_t i = m_frames.size(); i > 0; --i)
		{
			frame_record& frame = m_frames[i - 1];
			if (frame.engine_frame != value.frame) continue;

			const std::size_t before = frame.memory_bytes();
			if (page) frame.events.append_page(page, offset, 1);
			else frame.events.push_back(value);
			m_memoryBytes += frame.memory_bytes() - before;
			++m_lateSpansPlaced;
			return;
		}

		// 링에 있는 가장 오래된 프레임보다 앞선 것은 이미 밀려난 것이다.
		// 기다려도 오지 않으므로 버리고 센다.
		if (!m_frames.empty() && value.frame < m_frames.front().engine_frame)
		{
			++m_lateSpansDropped;
			return;
		}

		// 그 프레임이 아직 안 닫혔다. 다음 수집에서 다시 시도한다.
		if (m_deferredSpans.size() >= kMaxDeferredSpans)
		{
			++m_lateSpansDropped;
			return;
		}
		m_deferredSpans.push_back(value);
	}

	void capture_ring::drain_deferred_spans()
	{
		if (m_deferredSpans.empty()) return;

		// 자기 자신을 다시 채우지 않도록 통째로 떼어 내고 돈다.
		std::vector<profile_event> pendingSpans;
		pendingSpans.swap(m_deferredSpans);
		for (const profile_event& value : pendingSpans)
		{
			place_late_span(value);
		}
	}

	void capture_ring::close_frame(std::uint32_t engine_frame, profile_tick tick_begin, profile_tick tick_end)
	{
		m_pending.engine_frame = engine_frame;
		m_pending.tick_begin = tick_begin;
		m_pending.tick_end = tick_end;

		// 프레임이 가져가는 것은 **이 프레임의** 드롭이다. 누적을 넣으면
		// 프레임마다 같은 큰 수가 박혀 어느 프레임이 실제로 잃었는지
		// 구분되지 않는다. 누적은 m_droppedEvents 가 따로 들고 있다.
		m_pending.dropped_events = m_pendingDropped;
		m_pendingDropped = 0;

		m_lastFrameEvents = static_cast<std::uint32_t>(m_pending.events.size());
		m_peakFrameEvents = std::max(m_peakFrameEvents, m_lastFrameEvents);

		m_memoryBytes += m_pending.memory_bytes();
		m_frames.push_back(std::move(m_pending));
		m_pending = frame_record{};
		for (auto it = m_deferredCounters.begin(); it != m_deferredCounters.end();)
		{
			if (it->frame == engine_frame)
			{
				record_counter(it->frame, it->sample);
				it = m_deferredCounters.erase(it);
			}
			else if (it->frame < engine_frame)
			{
				++m_droppedCounters;
				it = m_deferredCounters.erase(it);
			}
			else ++it;
		}

		// 방금 프레임 하나가 닫혔다. 그 프레임을 기다리던 구간이 있으면
		// 지금 들어간다 — 닫히기 **전에** 온 것들의 자리가 여기다.
		drain_deferred_spans();

		trim();
	}

	void capture_ring::record_counter(std::uint32_t engine_frame, profile_counter_sample sample)
	{
		if (static_cast<std::uint16_t>(sample.id) == 0 || !std::isfinite(sample.value))
		{
			++m_droppedCounters;
			return;
		}
		for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it)
		{
			if (it->engine_frame != engine_frame) continue;
			for (auto& existing : it->counters)
			{
				if (existing.id == sample.id && existing.cpu.session == sample.cpu.session &&
				    existing.cpu.tick == sample.cpu.tick && existing.cpu.task == sample.cpu.task)
				{ existing.value = sample.value; return; }
			}
			const std::size_t before = it->memory_bytes();
			it->counters.push_back(sample);
			m_memoryBytes += it->memory_bytes() - before;
			return;
		}
		if (!m_frames.empty() && engine_frame < m_frames.front().engine_frame)
		{
			++m_droppedCounters;
			return;
		}
		if (!m_frames.empty() && engine_frame <= m_frames.back().engine_frame)
		{
			++m_droppedCounters; // frame boundary was skipped
			return;
		}
		if (m_deferredCounters.size() >= 4096) { ++m_droppedCounters; return; }
		m_deferredCounters.push_back({ engine_frame, sample });
	}

	void capture_ring::discard_deferred_counters_before(std::uint32_t engine_frame)
	{
		// Record may be requested while the game thread advances. Samples from
		// submissions older than the first boundary never belonged to this capture.
		std::erase_if(m_deferredCounters, [engine_frame](const deferred_counter& sample)
		{
			return sample.frame < engine_frame;
		});
	}

	void capture_ring::trim()
	{
		// 프레임 수와 메모리 예산 중 먼저 닿는 쪽을 따른다. 가장 오래된
		// **완결된** 프레임부터 버리므로 지금 쓰는 프레임은 건드리지 않는다.
		//
		// 앞에서 하나씩 erase 하면 매번 뒤를 전부 옮겨 O(n^2) 이 된다.
		// 몇 개를 버릴지 먼저 정하고 한 번에 옮긴다.
		std::size_t drop = 0;
		if (m_frames.size() > m_retainedFrames)
		{
			drop = m_frames.size() - m_retainedFrames;
		}

		std::size_t bytes = m_memoryBytes;
		for (std::size_t i = 0; i < drop; ++i)
		{
			bytes -= m_frames[i].memory_bytes();
		}

		// 예산을 넘으면 더 버린다. 마지막 한 프레임은 남긴다 — 예산이 한
		// 프레임보다 작게 잡혀도 캡처가 통째로 비어 버리지 않게.
		while (bytes > m_memoryBudget && drop + 1 < m_frames.size())
		{
			bytes -= m_frames[drop].memory_bytes();
			++drop;
		}

		if (drop == 0)
		{
			return;
		}

		m_memoryBytes = bytes;
		m_frames.erase(m_frames.begin(), m_frames.begin() + static_cast<std::ptrdiff_t>(drop));
	}

	capture_session_ptr capture_ring::freeze(std::span<const thread_info> threads,
	                                         capture_environment environment,
	                                         bool complete, std::uint32_t unacked_streams) const
	{
		// 프레임 인덱스와 페이지 참조만 복사한다. 이벤트 본문은 봉인된
		// 페이지에 남으므로 라이브 스냅샷을 반복해도 본문을 다시 복사하지 않는다.
		std::vector<frame_record> frames(m_frames.begin(), m_frames.end());
		std::vector<thread_info>  thread_list(threads.begin(), threads.end());

		// ★ 어휘도 **이 순간**의 것을 함께 뜬다(P6). 얼린 뒤에 등록되는
		//   마커는 이 캡처의 뜻이 아니고, 파일로 나갔다 돌아온 캡처는 이
		//   표 없이는 제 이름을 말할 수 없다.
		return std::make_shared<const capture_session>(
			std::move(frames), std::move(thread_list), snapshot_markers(),
			environment, complete, unacked_streams, m_droppedCounters, snapshot_counters());
	}
}
