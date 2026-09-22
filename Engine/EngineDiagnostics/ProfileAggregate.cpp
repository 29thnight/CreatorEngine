#include "ProfileAggregate.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

// 유니티 빌드 때문에 익명 네임스페이스를 쓰지 않는다(계획서 §14).
namespace ce::detail::profile_aggregate_impl
{
	// 전위 순회 순서. 같은 스레드 안에서 시작이 이른 것이 먼저이고, 시작이
	// 같으면 바깥(depth 가 작은 것)이 먼저다.
	//
	// ★ 목록을 그냥 읽으면 안 되는 이유: 이벤트는 end_scope 에서 기록되므로
	//   **끝난 순서**로 들어간다. 자식이 부모보다 먼저 있다.
	inline bool precedes(const profile_event& a, const profile_event& b)
	{
		if (a.thread_slot != b.thread_slot) return a.thread_slot < b.thread_slot;
		if (a.tick_begin != b.tick_begin)   return a.tick_begin < b.tick_begin;
		if (a.depth != b.depth)             return a.depth < b.depth;
		// 같은 자리에서 같은 깊이로 시작한 둘은 긴 쪽을 먼저 둔다.
		return a.tick_end > b.tick_end;
	}

	inline profile_tick span_ticks(const profile_event& event)
	{
		return (event.tick_end > event.tick_begin)
			? (event.tick_end - event.tick_begin) : 0;
	}

	inline bool is_instant(const profile_event& event)
	{
		return has_flag(event.flags, event_flags::instant);
	}

	inline bool is_truncated(const profile_event& event)
	{
		return has_flag(event.flags, event_flags::truncated_begin)
			|| has_flag(event.flags, event_flags::truncated_end);
	}

	// 호출 하나의 표본(§7.4 의 Min·P95·Frames).
	//
	// ★ 이 셋은 누적만으로는 안 나온다. p95 는 분포를 알아야 하므로 호출마다의
	//   길이를 남겨야 하고, "나타난 프레임 수" 는 중복을 지워야 하므로 프레임
	//   번호도 함께 남겨야 한다.
	//
	// ★ 프레임을 "마지막으로 본 값" 과 비교해 세면 안 된다. 이벤트는 시작 tick
	//   오름차순으로 훑는데, 프레임 경계를 넘긴 바깥 구간은 자기보다 늦게
	//   시작한 자식보다 **큰** 프레임 번호를 달고 먼저 나온다 — 그 자리에서
	//   프레임 번호가 거꾸로 가고, 그러면 같은 프레임을 두 번 센다.
	struct call_sample
	{
		std::uint32_t row = 0;
		std::uint32_t frame = 0;
		profile_tick  ticks = 0;
	};

	// 행마다 min · p95 · 나타난 프레임 수를 표본에서 낸다.
	//
	// ★ 부르는 자리가 중요하다. `rows` 의 색인이 표본의 `row` 와 같아야 하므로,
	//   행을 다시 세우는(전위 순서로 옮기는) **앞**에서 부르거나, 옮긴 뒤라면
	//   표본의 색인도 같이 옮겨야 한다. §0.5.20 의 스팬 경계와 같은 함정이다.
	inline void fold_call_samples(std::vector<call_sample>& samples,
	                              std::vector<aggregate_row>& rows)
	{
		if (samples.empty() || rows.empty())
		{
			return;
		}

		std::sort(samples.begin(), samples.end(),
		          [](const call_sample& a, const call_sample& b)
		          {
			          if (a.row != b.row) return a.row < b.row;
			          return a.ticks < b.ticks;
		          });

		for (std::size_t begin = 0; begin < samples.size(); )
		{
			std::size_t end = begin;
			while (end < samples.size() && samples[end].row == samples[begin].row)
			{
				++end;
			}

			aggregate_row& row = rows[samples[begin].row];
			const std::size_t count = end - begin;
			row.min_ticks = samples[begin].ticks;

			// nearest-rank: ceil(0.95 × count) 번째. count 가 1 이면 1 이므로
			// 색인은 언제나 [begin, end) 안이다.
			const std::size_t rank = (count * 95 + 99) / 100;
			row.p95_ticks = samples[begin + rank - 1].ticks;

			begin = end;
		}

		// 나타난 프레임 수는 (행, 프레임) 중복을 지워야 나온다.
		std::sort(samples.begin(), samples.end(),
		          [](const call_sample& a, const call_sample& b)
		          {
			          if (a.row != b.row) return a.row < b.row;
			          return a.frame < b.frame;
		          });

		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			if (i > 0 && samples[i].row == samples[i - 1].row &&
			    samples[i].frame == samples[i - 1].frame)
			{
				continue;
			}
			++rows[samples[i].row].frame_appearances;
		}
	}

	// hierarchy 에서 (부모, 스레드, marker) 가 같은 줄을 찾는 키.
	struct node_key
	{
		std::uint32_t parent = 0;   // 부모 행 인덱스 + 1, 루트는 0
		std::uint16_t thread_slot = 0;
		marker_id     marker = invalid_marker;

		bool operator==(const node_key& other) const
		{
			return parent == other.parent
				&& thread_slot == other.thread_slot
				&& marker == other.marker;
		}
	};

	struct node_key_hash
	{
		std::size_t operator()(const node_key& key) const
		{
			std::size_t value = static_cast<std::size_t>(key.parent) * 1315423911u;
			value ^= static_cast<std::size_t>(key.thread_slot) * 2654435761u;
			value ^= static_cast<std::size_t>(key.marker) * 40503u;
			return value;
		}
	};
}

namespace ce
{
	using namespace ce::detail::profile_aggregate_impl;

	frame_aggregate aggregate_frames(const capture_session& capture,
	                                 std::uint32_t first_frame,
	                                 std::uint32_t last_frame)
	{
		frame_aggregate result;
		if (last_frame < first_frame)
		{
			return result;
		}

		result.m_frameBegin = first_frame;
		result.m_frameEnd = last_frame + 1;

		// ── 1. 범위 안의 이벤트를 모은다 ────────────────────────────────────
		//
		// 모아서 세운 것을 그대로 결과에 남긴다(m_spans). Timeline 이 그리는
		// 원시 구간이 이것이고, 그리는 층이 다시 정렬하면 두 정렬이 갈리는
		// 순간 표와 타임라인이 서로 다른 트리를 말하게 된다.
		std::vector<profile_event>& events = result.m_spans;
		bool sawFrame = false;
		for (const frame_record& frame : capture.frames())
		{
			if (frame.engine_frame < first_frame || frame.engine_frame > last_frame)
			{
				continue;
			}

			if (!sawFrame)
			{
				result.m_tickBegin = frame.tick_begin;
				result.m_tickEnd = frame.tick_end;
				sawFrame = true;
			}
			else
			{
				result.m_tickBegin = (std::min)(result.m_tickBegin, frame.tick_begin);
				result.m_tickEnd = (std::max)(result.m_tickEnd, frame.tick_end);
			}

			result.m_droppedEvents += frame.dropped_events;
			events.insert(events.end(), frame.events.begin(), frame.events.end());

			// 프레임이 어디서 갈리는지. 프레임 기록은 엔진 프레임 오름차순으로
			// 서 있으므로 그대로 옮기면 정렬이 끝난다.
			frame_boundary boundary;
			boundary.engine_frame = frame.engine_frame;
			boundary.tick_begin = frame.tick_begin;
			boundary.tick_end = frame.tick_end;
			result.m_boundaries.push_back(boundary);
		}

		result.m_eventCount = events.size();
		if (events.empty())
		{
			return result;
		}

		std::sort(events.begin(), events.end(), precedes);

		// ── 2. 스레드 요약 ──────────────────────────────────────────────────
		//
		// root_ticks 는 depth 0 만 더한다. 그 구간들은 서로 겹치지 않으므로
		// 합이 그대로 그 스레드가 이 범위에서 계측된 시간이다.
		for (const profile_event& event : events)
		{
			if (is_truncated(event))
			{
				++result.m_truncatedEvents;
			}

			thread_summary* summary = nullptr;
			for (thread_summary& candidate : result.m_threads)
			{
				if (candidate.thread_slot == event.thread_slot)
				{
					summary = &candidate;
					break;
				}
			}
			if (!summary)
			{
				thread_summary fresh;
				fresh.thread_slot = event.thread_slot;
				result.m_threads.push_back(fresh);
				summary = &result.m_threads.back();
			}

			++summary->event_count;
			summary->max_depth = (std::max)(summary->max_depth, event.depth);

			// ★ 길이가 없는 사건은 합에도 트리에도 넣지 않는다. Hierarchy·Flat
			//   이 세는 것은 **걸린 시간**이고 점에는 그것이 없다.
			//
			//   길이가 0 이라 더해도 0 이지만, 넣고 안 넣고를 **두 곳에서
			//   다르게** 정하면 timeline_total 과 hierarchy_total 의 동치가
			//   깨진다. 한 자리에서 가르고 아래 트리도 같은 기준을 쓴다.
			if (is_instant(event))
			{
				result.m_instants.push_back(event);
				continue;
			}

			if (event.depth == 0)
			{
				summary->root_ticks += span_ticks(event);
			}
		}

		// 사건은 **시각 순**으로 세운다. 위에서는 스팬 순서(슬롯 먼저)로 모았고,
		// 경계 띠는 스레드를 가리지 않고 시간축 하나에 늘어놓기 때문이다.
		std::sort(result.m_instants.begin(), result.m_instants.end(),
		          [](const profile_event& a, const profile_event& b)
		          {
			          if (a.tick_begin != b.tick_begin) return a.tick_begin < b.tick_begin;
			          return a.thread_slot < b.thread_slot;
		          });

		std::sort(result.m_threads.begin(), result.m_threads.end(),
		          [](const thread_summary& a, const thread_summary& b)
		          { return a.thread_slot < b.thread_slot; });

		// 스팬은 스레드 순으로 서 있으므로 각 스레드의 몫이 연속이다.
		// 레인을 그리는 쪽이 자기 구간만 훑도록 경계를 적어 둔다.
		//
		// ★ 이 순회는 요약이 **이벤트와 같은 순서**(슬롯 오름차순)일 때만
		//   성립한다. 아래에서 트랙 순서로 다시 세우므로, 경계는 반드시
		//   그 전에 적어야 한다. 순서를 바꾼 뒤에 돌리면 첫 요약의 슬롯이
		//   events[0] 과 안 맞아 **모든 레인이 [0,0) 으로 비고**, 아무것도
		//   실패하지 않은 채 타임라인만 빈 화면이 된다.
		{
			std::uint32_t cursor = 0;
			for (thread_summary& summary : result.m_threads)
			{
				summary.span_begin = cursor;
				while (cursor < events.size() &&
				       events[cursor].thread_slot == summary.thread_slot)
				{
					++cursor;
				}
				summary.span_end = cursor;
			}
		}

		// ── 2b. §7.3 의 트랙 순서로 레인을 세운다 ──────────────────────────
		//
		// 슬롯은 **등록 순서**라 회차마다 갈린다(워커 여덟의 등록이 겹친다).
		// 트랙은 등록하는 쪽이 선언한 것이므로 회차와 무관하다.
		//
		// ★ 경계(span_begin/end)는 인덱스라 자리를 바꿔도 그대로 뜻을 지킨다 —
		//   그래서 위에서 먼저 적어 두고 여기서 옮긴다.
		{
			const std::span<const thread_info> registry = capture.threads();
			auto track_of = [&](std::uint16_t slot) -> thread_info
			{
				for (const thread_info& info : registry)
				{
					if (info.slot == slot) return info;
				}
				// 표에 없는 슬롯. 트랙을 모르므로 기본값(other)으로 맨 아래다.
				thread_info unknown;
				unknown.slot = slot;
				return unknown;
			};

			std::stable_sort(result.m_threads.begin(), result.m_threads.end(),
			                 [&](const thread_summary& a, const thread_summary& b)
			                 {
				                 return track_precedes(track_of(a.thread_slot),
				                                       track_of(b.thread_slot));
			                 });
		}

		for (const thread_summary& summary : result.m_threads)
		{
			result.m_timelineTotal += summary.root_ticks;
		}

		// ── 3. Hierarchy ────────────────────────────────────────────────────
		//
		// 전위 순서를 한 번 훑으며 depth 스택으로 부모를 찾는다. 같은 부모 ·
		// 같은 스레드 · 같은 marker 는 한 줄로 접는다.
		std::unordered_map<node_key, std::uint32_t, node_key_hash> nodes;
		std::vector<std::uint32_t> parentOf;
		std::vector<std::uint32_t> stack;        // 행 인덱스 + 1
		std::vector<profile_tick>  stackEnd;
		std::vector<profile_tick>  childTicks;   // 행마다 직속 자식 total 합
		std::vector<call_sample>   samples;      // 호출마다 길이와 프레임
		samples.reserve(events.size());

		for (const profile_event& event : events)
		{
			// 길이가 없는 사건은 트리에 들어가지 않는다(위의 같은 기준).
			if (is_instant(event))
			{
				continue;
			}

			// 스레드가 바뀌면 스택은 뜻을 잃는다. 깊이 비교만으로는 다른
			// 스레드의 조상이 부모로 잡힌다.
			if (!stack.empty() &&
			    result.m_hierarchy[stack.back() - 1].thread_slot != event.thread_slot)
			{
				stack.clear();
				stackEnd.clear();
			}
			while (!stack.empty() &&
			       (stackEnd.back() <= event.tick_begin ||
			        result.m_hierarchy[stack.back() - 1].depth >= event.depth))
			{
				stack.pop_back();
				stackEnd.pop_back();
			}

			const node_key key{ stack.empty() ? 0u : stack.back(),
			                    event.thread_slot, event.marker };

			auto found = nodes.find(key);
			std::uint32_t row = 0;
			if (found != nodes.end())
			{
				row = found->second;
			}
			else
			{
				row = static_cast<std::uint32_t>(result.m_hierarchy.size());
				aggregate_row fresh;
				fresh.marker = event.marker;
				fresh.thread_slot = event.thread_slot;
				fresh.depth = event.depth;
				result.m_hierarchy.push_back(fresh);
				childTicks.push_back(0);
				parentOf.push_back(key.parent);
				nodes.emplace(key, row);
			}

			const profile_tick ticks = span_ticks(event);
			samples.push_back(call_sample{ row, event.frame, ticks });

			aggregate_row& node = result.m_hierarchy[row];
			++node.call_count;
			node.total_ticks += ticks;
			node.max_ticks = (std::max)(node.max_ticks, ticks);
			if (is_truncated(event))
			{
				node.truncated = true;
			}

			if (!stack.empty())
			{
				childTicks[stack.back() - 1] += ticks;
			}
			else
			{
				result.m_hierarchyTotal += ticks;
			}

			stack.push_back(row + 1);
			stackEnd.push_back(event.tick_end);
		}

		// self = total - 직속 자식 합. 자식이 부모 밖으로 새면 0 으로 접는다 —
		// 음수를 부호 없는 수에 담으면 표가 거대한 값으로 뒤집힌다.
		for (std::size_t i = 0; i < result.m_hierarchy.size(); ++i)
		{
			aggregate_row& node = result.m_hierarchy[i];
			node.self_ticks = (node.total_ticks > childTicks[i])
				? (node.total_ticks - childTicks[i]) : 0;
		}

		// Min·P95·Frames 는 **다시 세우기 전에** 접는다. 표본의 `row` 는 아직
		// 쌓은 순서의 색인이다.
		fold_call_samples(samples, result.m_hierarchy);

		// 행을 전위 순서로 다시 세운다. 위 순회는 **처음 본 순서**로 쌓으므로
		// 한 부모의 자식들이 흩어져 있다 — 표가 트리로 보이려면 서브트리가
		// 이어져 있어야 한다.
		{
			std::vector<std::vector<std::uint32_t>> childrenOf(result.m_hierarchy.size() + 1);
			for (std::uint32_t i = 0; i < result.m_hierarchy.size(); ++i)
			{
				childrenOf[parentOf[i]].push_back(i);
			}

			// 같은 부모의 자식은 스레드 먼저, 그 다음 total 내림차순 —
			// 비싼 것이 위로 온다.
			for (auto& children : childrenOf)
			{
				std::sort(children.begin(), children.end(),
				          [&](std::uint32_t a, std::uint32_t b)
				          {
					          const aggregate_row& left = result.m_hierarchy[a];
					          const aggregate_row& right = result.m_hierarchy[b];
					          if (left.thread_slot != right.thread_slot)
						          return left.thread_slot < right.thread_slot;
					          if (left.total_ticks != right.total_ticks)
						          return left.total_ticks > right.total_ticks;
					          return left.marker < right.marker;
				          });
			}

			std::vector<aggregate_row> ordered;
			ordered.reserve(result.m_hierarchy.size());
			std::vector<std::uint32_t> remap(result.m_hierarchy.size(), 0);

			std::vector<std::uint32_t> pending;
			for (auto it = childrenOf[0].rbegin(); it != childrenOf[0].rend(); ++it)
			{
				pending.push_back(*it);
			}
			while (!pending.empty())
			{
				const std::uint32_t source = pending.back();
				pending.pop_back();
				remap[source] = static_cast<std::uint32_t>(ordered.size());
				ordered.push_back(result.m_hierarchy[source]);
				const auto& children = childrenOf[source + 1];
				for (auto it = children.rbegin(); it != children.rend(); ++it)
				{
					pending.push_back(*it);
				}
			}

			// 전위 순서에서 한 노드의 서브트리는 연속이다. 직속 자식의 범위는
			// 첫 자식부터 마지막 자식의 서브트리 끝까지다.
			for (std::uint32_t source = 0; source < result.m_hierarchy.size(); ++source)
			{
				const auto& children = childrenOf[source + 1];
				aggregate_row& node = ordered[remap[source]];
				if (children.empty())
				{
					node.child_begin = node.child_end = remap[source] + 1;
					continue;
				}
				node.child_begin = remap[children.front()];
				std::uint32_t last = node.child_begin;
				for (std::uint32_t child : children)
				{
					last = (std::max)(last, remap[child]);
				}
				node.child_end = last + 1;
			}

			// 표본도 함께 옮긴다. Flat 이 같은 표본을 다시 쓰는데, 옮기지 않으면
			// 남의 행에 자기 분포를 적는다.
			for (call_sample& sample : samples)
			{
				sample.row = remap[sample.row];
			}

			result.m_hierarchy = std::move(ordered);
		}

		// ── 4. Flat ─────────────────────────────────────────────────────────
		//
		// 부모를 무시하고 (스레드, marker) 로 접는다. self 는 Hierarchy 의
		// self 를 더한 것이라 총합이 보존된다.
		{
			std::unordered_map<std::uint64_t, std::uint32_t> flatIndex;
			std::vector<std::uint32_t> flatOfRow(result.m_hierarchy.size(), 0);
			std::uint32_t source = 0;
			for (const aggregate_row& node : result.m_hierarchy)
			{
				const std::uint64_t key =
					(static_cast<std::uint64_t>(node.thread_slot) << 32) |
					static_cast<std::uint64_t>(node.marker);
				auto found = flatIndex.find(key);
				if (found == flatIndex.end())
				{
					flatOfRow[source] = static_cast<std::uint32_t>(result.m_flat.size());
					flatIndex.emplace(key, static_cast<std::uint32_t>(result.m_flat.size()));
					aggregate_row fresh = node;
					fresh.depth = 0;
					fresh.child_begin = fresh.child_end = 0;

					// ★ 분포는 **베끼지 않는다.** 아래에서 표본으로 다시 접으므로
					//   여기 남은 값은 남의 것이 되고, 프레임 수는 `+=` 라 그 위에
					//   더해져 호출 수보다 커진다.
					fresh.min_ticks = 0;
					fresh.p95_ticks = 0;
					fresh.frame_appearances = 0;
					result.m_flat.push_back(fresh);
					++source;
					continue;
				}

				flatOfRow[source] = found->second;
				aggregate_row& target = result.m_flat[found->second];
				target.call_count += node.call_count;
				target.total_ticks += node.total_ticks;
				target.self_ticks += node.self_ticks;
				target.max_ticks = (std::max)(target.max_ticks, node.max_ticks);
				target.truncated = target.truncated || node.truncated;
				++source;
			}

			// ★ 프레임 수를 행마다 더하면 안 된다. 같은 marker 가 한 프레임 안
			//   두 부모 밑에서 불리면 행이 둘인데 프레임은 하나다 — 더하면 2 가
			//   된다. p95 도 같다: 부분들의 p95 로는 전체의 p95 가 안 나온다.
			//   그래서 Flat 은 합치지 않고 **같은 표본을 다시 접는다.**
			for (call_sample& sample : samples)
			{
				sample.row = flatOfRow[sample.row];
			}
			fold_call_samples(samples, result.m_flat);

			std::sort(result.m_flat.begin(), result.m_flat.end(),
			          [](const aggregate_row& a, const aggregate_row& b)
			          {
				          if (a.total_ticks != b.total_ticks) return a.total_ticks > b.total_ticks;
				          return a.marker < b.marker;
			          });
		}

		return result;
	}
}
