// PHASE 14 P1+P2 — 새 프로파일러 코어 프로브.
//
// 엔진을 띄우지 않는다. EngineDiagnostics 가 ProjectReference 0 의 독립
// 라이브러리이고 서비스가 인스턴스로 서므로, 코어만 링크해 초 단위로 돈다.
// 옛 코어는 전역 싱글톤 하나에 함수 지역 static thread_local 을 공유해서
// 이런 프로브가 불가능했고, 그래서 selftest 가 라이브 캡처의 프레임 경계를
// 직접 넘겨야 했다(그 교란이 stats 기준선을 못 믿게 만들었다).
//
// 판정은 종료 코드다. 실패 사유는 stderr 로 낸다.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "ProfileAggregate.h"
#include "ProfileReader.h"
#include "ProfileScope.h"
#include "ProfileService.h"

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void check(bool condition, const char* what)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::fprintf(stderr, "FAIL: %s\n", what);
		}
	}

	template <typename T>
	void check_eq(T actual, T expected, const char* what)
	{
		++g_checks;
		if (actual != expected)
		{
			++g_failures;
			std::fprintf(stderr, "FAIL: %s (expected %llu, got %llu)\n", what,
			             static_cast<unsigned long long>(expected),
			             static_cast<unsigned long long>(actual));
		}
	}

	// 한 프레임의 이벤트 수를 센다.
	std::size_t events_in(const ce::capture_session& capture, std::uint32_t frame)
	{
		const ce::frame_record* record = capture.find_frame(frame);
		return record ? record->events.size() : 0;
	}

	std::size_t count_marker(const ce::capture_session& capture, ce::marker_id id)
	{
		std::size_t total = 0;
		for (const ce::frame_record& frame : capture.frames())
		{
			for (const ce::profile_event& event : frame.events)
			{
				if (event.marker == id)
				{
					++total;
				}
			}
		}
		return total;
	}

	//-------------------------------------------------------------------------
	// ① 마커 — 같은 이름은 같은 id, 다른 이름은 다른 id.
	//-------------------------------------------------------------------------
	void test_marker_identity()
	{
		const ce::marker_id a1 = ce::marker<"AnimatorSystem">();
		const ce::marker_id a2 = ce::marker<"AnimatorSystem">();
		const ce::marker_id b = ce::marker<"DecalSystem">();

		check(a1 != ce::invalid_marker, "marker/valid — 등록된 id 가 invalid 가 아니다");
		check_eq(a1, a2, "marker/stable — 같은 이름은 같은 id");
		check(a1 != b, "marker/distinct — 다른 이름은 다른 id");
		check(std::string(ce::marker_info(a1).name) == "AnimatorSystem",
		      "marker/name — id 로 이름을 되찾는다");
	}

	//-------------------------------------------------------------------------
	// ② 기본 수집과 중첩.
	//-------------------------------------------------------------------------
	void test_basic_capture()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Probe");
		service.record(1);

		{
			ce::profile_scope outer{ service, ce::marker<"Outer">() };
			{
				ce::profile_scope inner{ service, ce::marker<"Inner">() };
			}
		}

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		check(capture != nullptr, "basic/capture — 얼린 캡처가 있다");
		if (!capture)
		{
			return;
		}

		check_eq(events_in(*capture, 1), std::size_t{ 2 }, "basic/count — 스코프 둘이 이벤트 둘");

		const ce::frame_record* frame = capture->find_frame(1);
		if (!frame || frame->events.size() < 2)
		{
			return;
		}

		// 안쪽이 먼저 닫히므로 먼저 기록된다.
		check_eq(frame->events[0].marker, ce::marker<"Inner">(), "basic/order — 안쪽이 먼저 닫힌다");
		check_eq(frame->events[0].depth, std::uint16_t{ 1 }, "basic/depth-inner — 안쪽 깊이 1");
		check_eq(frame->events[1].depth, std::uint16_t{ 0 }, "basic/depth-outer — 바깥 깊이 0");
		check(frame->events[1].tick_begin <= frame->events[0].tick_begin,
		      "basic/containment — 바깥이 먼저 시작한다");
		check(frame->events[1].tick_end >= frame->events[0].tick_end,
		      "basic/containment-end — 바깥이 나중에 끝난다");
	}

	//-------------------------------------------------------------------------
	// ③ ★ 프레임을 넘는 구간. 옛 코어의 기지 결함(cross-frame/preserve)이
	//    여기서 PASS 가 되는 것이 P2 의 판정이다.
	//-------------------------------------------------------------------------
	void test_cross_frame_scope()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Probe");
		service.record(1);

		const ce::marker_id spanning = ce::marker<"SpanningScope">();

		service.begin_scope(spanning);
		service.publish_frame(1);   // 열린 채로 프레임이 넘어간다
		service.publish_frame(2);
		service.end_scope();        // 3번째 프레임에서 닫힌다
		service.publish_frame(3);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		check(capture != nullptr, "cross-frame/capture — 얼린 캡처가 있다");
		if (!capture)
		{
			return;
		}

		check_eq(count_marker(*capture, spanning), std::size_t{ 1 },
		         "cross-frame/preserve — 프레임을 넘는 구간이 살아남는다");

		// 어느 프레임에서 시작했는지 복원된다. 이것이 없으면 분석기가
		// 구간을 닫힌 프레임의 것으로만 읽어 길이를 틀리게 만든다.
		for (const ce::frame_record& frame : capture->frames())
		{
			for (const ce::profile_event& event : frame.events)
			{
				if (event.marker == spanning)
				{
					check_eq(event.frame, std::uint32_t{ 1 },
					         "cross-frame/origin — 시작 프레임 번호가 보존된다");
					check(event.tick_end > event.tick_begin,
					      "cross-frame/duration — 구간 길이가 양수다");
				}
			}
		}
	}

	//-------------------------------------------------------------------------
	// ④ 여러 writer. 8개 스레드가 동시에 찍어도 손상이 없고 전부 귀속된다.
	//-------------------------------------------------------------------------
	void test_multithread_stress()
	{
		constexpr int kWorkers = 8;
		constexpr int kScopesPerWorker = 200;

		ce::profiler_service service;
		ce::profiler_config config;
		config.chunk_count = 512;
		service.initialize(config);
		service.register_thread("Main");
		service.record(1);

		std::atomic<int> ready{ 0 };
		std::vector<std::thread> workers;
		workers.reserve(kWorkers);

		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back([&service, &ready, i]()
			{
				const std::string name = "Worker" + std::to_string(i);
				service.register_thread(name.c_str());
				ready.fetch_add(1);

				for (int n = 0; n < kScopesPerWorker; ++n)
				{
					ce::profile_scope scope{ service, ce::marker<"WorkerScope">() };
				}

				// 스레드가 끝나기 전에 등록을 해제한다. 옛 코어는 이것 없이
				// 스레드가 죽으면 이후 모든 수집이 죽은 TLS 를 읽었다.
				service.unregister_thread();
			});
		}

		for (std::thread& worker : workers)
		{
			worker.join();
		}

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		check(capture != nullptr, "multithread/capture — 얼린 캡처가 있다");
		if (!capture)
		{
			return;
		}

		const ce::live_summary summary = service.summary();
		const std::size_t seen = count_marker(*capture, ce::marker<"WorkerScope">());
		const std::size_t expected = static_cast<std::size_t>(kWorkers) * kScopesPerWorker;

		// 드롭이 없으면 전부 있어야 한다. 드롭이 있었다면 그 수만큼만 빈다 —
		// 어느 쪽이든 **잃은 수를 모르는 상태**는 실패다.
		check_eq(seen + summary.dropped_events, static_cast<std::uint64_t>(expected),
		         "multithread/accounted — 수집분 + 드롭 = 발생분");
		check_eq(summary.unbalanced_scopes, std::uint64_t{ 0 },
		         "multithread/balanced — 불균형 스코프 0");

		// 스레드 귀속. 워커 8 + Main 1.
		check_eq(service.thread_count(), std::uint32_t{ kWorkers + 1 },
		         "multithread/threads — 등록된 스레드가 전부 남는다");
	}

	//-------------------------------------------------------------------------
	// ⑤ 풀 고갈. 막지 않고, 잃은 수를 정확히 센다.
	//-------------------------------------------------------------------------
	void test_pool_exhaustion()
	{
		ce::profiler_service service;
		ce::profiler_config config;
		config.chunk_count = 1;   // 청크 하나뿐
		service.initialize(config);
		service.register_thread("Probe");
		service.record(1);

		// 청크 하나가 담는 것보다 훨씬 많이 찍는다. 프레임을 닫지 않으므로
		// 봉인된 청크가 되돌아오지 않는다 — 반드시 고갈된다.
		constexpr int kScopes = ce::kEventsPerChunk * 4;
		for (int i = 0; i < kScopes; ++i)
		{
			ce::profile_scope scope{ service, ce::marker<"Flood">() };
		}

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		check(capture != nullptr, "overflow/capture — 고갈 뒤에도 캡처가 선다");
		if (!capture)
		{
			return;
		}

		const ce::live_summary summary = service.summary();
		const std::size_t seen = count_marker(*capture, ce::marker<"Flood">());

		check(summary.dropped_events > 0, "overflow/counted — 고갈이 드롭으로 기록된다");
		check_eq(seen + summary.dropped_events, static_cast<std::uint64_t>(kScopes),
		         "overflow/accounted — 수집분 + 드롭 = 발생분");
		check(seen <= ce::kEventsPerChunk,
		      "overflow/bounded — 청크 하나 넘게 담지 않는다");
	}

	//-------------------------------------------------------------------------
	// ⑥ ★ 서비스 격리. 두 인스턴스가 같은 스레드에서 서로를 교란하지 않는다.
	//    옛 코어가 못 하던 것이고, selftest 가 라이브 캡처를 망가뜨리던 이유다.
	//-------------------------------------------------------------------------
	void test_service_isolation()
	{
		ce::profiler_service live;
		ce::profiler_service probe;

		live.initialize();
		probe.initialize();
		live.register_thread("Live");
		probe.register_thread("Probe");

		live.record();
		probe.record();

		// probe 에만 찍는다.
		probe.begin_scope(ce::marker<"ProbeOnly">());
		probe.end_scope();

		live.publish_frame(1);
		probe.publish_frame(1);
		live.pause();
		probe.pause();

		ce::capture_session_ptr live_capture = live.capture();
		ce::capture_session_ptr probe_capture = probe.capture();
		check(live_capture && probe_capture, "isolation/capture — 두 캡처가 모두 선다");
		if (!live_capture || !probe_capture)
		{
			return;
		}

		check_eq(events_in(*live_capture, 1), std::size_t{ 0 },
		         "isolation/clean — 검사용 서비스가 라이브를 교란하지 않는다");
		check_eq(events_in(*probe_capture, 1), std::size_t{ 1 },
		         "isolation/recorded — 검사용 서비스는 제 것을 담는다");
	}

	//-------------------------------------------------------------------------
	// ⑦ recorder 상태 머신. stopped 에서는 기록하지 않고, 얼린 캡처는 변하지
	//    않는다.
	//-------------------------------------------------------------------------
	void test_recorder_states()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Probe");

		// stopped — 마커는 즉시 return
		{
			ce::profile_scope scope{ service, ce::marker<"WhileStopped">() };
		}
		service.publish_frame(1);

		service.record(1);
		{
			ce::profile_scope scope{ service, ce::marker<"WhileRecording">() };
		}
		service.publish_frame(2);
		service.pause();

		ce::capture_session_ptr frozen = service.capture();
		check(frozen != nullptr, "recorder/capture — 얼린 캡처가 있다");
		if (!frozen)
		{
			return;
		}

		check_eq(count_marker(*frozen, ce::marker<"WhileStopped">()), std::size_t{ 0 },
		         "recorder/stopped — 멈춘 동안은 기록하지 않는다");
		check_eq(count_marker(*frozen, ce::marker<"WhileRecording">()), std::size_t{ 1 },
		         "recorder/recording — 녹화 중에는 기록한다");

		// 얼린 뒤 엔진이 계속 돌아도 손에 든 자료는 변하지 않는다.
		const std::uint32_t frames_before = frozen->frame_count();
		const std::uint64_t events_before = frozen->total_events();

		service.record(1);
		for (int i = 0; i < 10; ++i)
		{
			ce::profile_scope scope{ service, ce::marker<"AfterFreeze">() };
			service.publish_frame(3 + static_cast<std::uint32_t>(i));
		}

		check_eq(frozen->frame_count(), frames_before, "recorder/immutable-frames — 얼린 프레임 수가 그대로다");
		check_eq(frozen->total_events(), events_before, "recorder/immutable-events — 얼린 이벤트 수가 그대로다");
		check_eq(count_marker(*frozen, ce::marker<"AfterFreeze">()), std::size_t{ 0 },
		         "recorder/immutable-content — 얼린 뒤의 기록이 새어들지 않는다");
	}

	//-------------------------------------------------------------------------
	// ⑧ rolling — 보존 한도를 넘으면 오래된 것부터 버린다.
	//-------------------------------------------------------------------------
	void test_rolling_retention()
	{
		ce::profiler_service service;
		ce::profiler_config config;
		config.retained_frames = 8;
		service.initialize(config);
		service.register_thread("Probe");
		service.record(1);

		for (std::uint32_t frame = 1; frame <= 32; ++frame)
		{
			{
				ce::profile_scope scope{ service, ce::marker<"Rolling">() };
			}
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		check(capture != nullptr, "rolling/capture — 얼린 캡처가 있다");
		if (!capture)
		{
			return;
		}

		check_eq(capture->frame_count(), std::uint32_t{ 8 }, "rolling/retained — 한도만큼만 남는다");
		check(capture->find_frame(1) == nullptr, "rolling/evicted — 오래된 프레임은 버려진다");
		check(capture->find_frame(32) != nullptr, "rolling/newest — 최신 프레임은 남는다");
	}
	//-------------------------------------------------------------------------
	// ⑨ PHASE 14 P3 — 집계. 얼린 캡처를 Hierarchy/Flat 으로 접는다.
	//
	//    ★ 이 검사들이 코어에 있는 이유: 완료조건("Timeline 합계와 Hierarchy
	//      inclusive time 이 일치한다")은 순수 계산이라 화면이 필요 없다.
	//      그리는 층에 집계를 두면 그 조건을 잴 수단이 눈뿐이 된다.
	//-------------------------------------------------------------------------
	const ce::aggregate_row* find_row(const ce::frame_aggregate& aggregate,
	                                  ce::marker_id marker, std::uint16_t depth)
	{
		for (const ce::aggregate_row& row : aggregate.hierarchy())
		{
			if (row.marker == marker && row.depth == depth)
			{
				return &row;
			}
		}
		return nullptr;
	}

	const ce::aggregate_row* find_flat(const ce::frame_aggregate& aggregate,
	                                   ce::marker_id marker)
	{
		for (const ce::aggregate_row& row : aggregate.flat())
		{
			if (row.marker == marker)
			{
				return &row;
			}
		}
		return nullptr;
	}

	void busy_ticks(int rounds)
	{
		// 구간에 잴 만한 길이를 준다. sleep 을 쓰면 검사가 느려지고, 빈
		// 스코프는 tick 해상도 아래로 내려가 0 이 될 수 있다.
		volatile std::uint64_t sink = 0;
		for (int i = 0; i < rounds * 1000; ++i)
		{
			sink += static_cast<std::uint64_t>(i);
		}
		(void)sink;
	}

	void test_aggregate_tree()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		// Outer > (Inner, Inner, Leaf) — 같은 marker 를 두 번 부른다.
		{
			ce::profile_scope outer{ service, ce::marker<"AggOuter">() };
			busy_ticks(4);
			{
				ce::profile_scope inner{ service, ce::marker<"AggInner">() };
				busy_ticks(2);
			}
			{
				ce::profile_scope inner{ service, ce::marker<"AggInner">() };
				busy_ticks(8);
			}
			{
				ce::profile_scope leaf{ service, ce::marker<"AggLeaf">() };
				busy_ticks(1);
			}
		}

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		check(capture != nullptr, "aggregate/capture — 얼린 캡처가 있다");
		if (!capture)
		{
			return;
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 1);

		check_eq(aggregate.event_count(), std::uint64_t{ 4 }, "aggregate/events — 이벤트 넷");
		check_eq(aggregate.hierarchy().size(), std::size_t{ 3 },
		         "aggregate/rows — 같은 부모의 같은 marker 는 한 줄로 접힌다");

		const ce::aggregate_row* outer = find_row(aggregate, ce::marker<"AggOuter">(), 0);
		const ce::aggregate_row* inner = find_row(aggregate, ce::marker<"AggInner">(), 1);
		const ce::aggregate_row* leaf = find_row(aggregate, ce::marker<"AggLeaf">(), 1);

		check(outer != nullptr, "aggregate/root — 루트 행이 있다");
		check(inner != nullptr, "aggregate/child — 자식 행이 있다");
		check(leaf != nullptr, "aggregate/leaf — 잎 행이 있다");
		if (!outer || !inner || !leaf)
		{
			return;
		}

		check_eq(outer->call_count, std::uint64_t{ 1 }, "aggregate/calls-root — 루트는 한 번");
		check_eq(inner->call_count, std::uint64_t{ 2 }, "aggregate/calls-child — 자식은 두 번");

		// ★ 트리가 서지 않으면 여기가 무너진다. depth 를 무시하고 접으면
		//   자식이 루트로 올라와 루트 행이 셋이 된다.
		std::size_t roots = 0;
		for (const ce::aggregate_row& row : aggregate.hierarchy())
		{
			if (row.depth == 0) ++roots;
		}
		check_eq(roots, std::size_t{ 1 }, "aggregate/single-root — 루트는 하나다");

		// self = total - 직속 자식 합.
		const ce::profile_tick childSum = inner->total_ticks + leaf->total_ticks;
		check(outer->total_ticks >= childSum,
		      "aggregate/containment — 부모 total 이 자식 합보다 크거나 같다");
		check_eq(outer->self_ticks, outer->total_ticks - childSum,
		         "aggregate/self — self 는 total 에서 직속 자식을 뺀 것");
		check_eq(inner->self_ticks, inner->total_ticks,
		         "aggregate/self-leaf — 자식이 없으면 self 는 total");

		// max 는 한 호출의 최대다. 두 번 부른 자식은 total 보다 작아야 한다.
		check(inner->max_ticks < inner->total_ticks,
		      "aggregate/max — 여러 번 부른 행의 max 는 total 보다 작다");
		check(inner->max_ticks > 0, "aggregate/max-positive — max 가 0 이 아니다");

		// ★ 완료조건. 같은 것을 두 길로 더하므로 정확히 같아야 한다.
		check_eq(aggregate.timeline_total_ticks(), aggregate.hierarchy_total_ticks(),
		         "aggregate/totals — Timeline 합계와 Hierarchy inclusive 합이 같다");
		check(aggregate.timeline_total_ticks() > 0,
		      "aggregate/totals-positive — 합계가 0 이 아니다");

		// 서브트리는 전위 순서에서 연속이다.
		check(outer->child_begin < outer->child_end,
		      "aggregate/child-range — 루트에 자식 범위가 있다");
		check_eq(static_cast<std::size_t>(outer->child_end - outer->child_begin),
		         std::size_t{ 2 }, "aggregate/child-count — 직속 자식 둘");

		const ce::thread_summary* thread = aggregate.threads().empty()
			? nullptr : &aggregate.threads()[0];
		check(thread != nullptr, "aggregate/thread — 스레드 요약이 있다");
		if (thread)
		{
			check_eq(thread->max_depth, std::uint16_t{ 1 }, "aggregate/depth — 최대 깊이 1");
			check_eq(thread->event_count, std::uint32_t{ 4 }, "aggregate/thread-events — 넷");
		}
	}

	//-------------------------------------------------------------------------
	// ⑩ 여러 프레임을 한 범위로 접는다. "다중 frame selection" 이 이것이다.
	//-------------------------------------------------------------------------
	void test_aggregate_range()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		for (std::uint32_t frame = 1; frame <= 4; ++frame)
		{
			{
				ce::profile_scope outer{ service, ce::marker<"RangeOuter">() };
				busy_ticks(2);
				{
					ce::profile_scope inner{ service, ce::marker<"RangeInner">() };
					busy_ticks(1);
				}
			}
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		if (!capture)
		{
			check(false, "aggregate-range/capture — 얼린 캡처가 있다");
			return;
		}

		const ce::frame_aggregate one = ce::aggregate_frames(*capture, 2, 2);
		const ce::frame_aggregate four = ce::aggregate_frames(*capture, 1, 4);

		check_eq(one.frame_count(), std::uint32_t{ 1 }, "aggregate-range/one — 한 프레임");
		check_eq(four.frame_count(), std::uint32_t{ 4 }, "aggregate-range/four — 네 프레임");

		const ce::aggregate_row* single = find_row(one, ce::marker<"RangeOuter">(), 0);
		const ce::aggregate_row* merged = find_row(four, ce::marker<"RangeOuter">(), 0);
		check(single != nullptr && merged != nullptr, "aggregate-range/rows — 두 범위 모두 행이 있다");
		if (!single || !merged)
		{
			return;
		}

		check_eq(single->call_count, std::uint64_t{ 1 }, "aggregate-range/calls-one — 한 번");
		check_eq(merged->call_count, std::uint64_t{ 4 }, "aggregate-range/calls-four — 네 번");
		check(merged->total_ticks > single->total_ticks,
		      "aggregate-range/total — 범위를 넓히면 total 이 커진다");
		check(merged->max_ticks >= single->max_ticks,
		      "aggregate-range/max — max 는 범위를 넓혀도 줄지 않는다");

		check_eq(four.timeline_total_ticks(), four.hierarchy_total_ticks(),
		         "aggregate-range/totals — 범위가 넓어도 두 합이 같다");

		// 캡처에 없는 범위는 빈 집계다. 던지지 않는다.
		const ce::frame_aggregate none = ce::aggregate_frames(*capture, 900, 901);
		check_eq(none.event_count(), std::uint64_t{ 0 }, "aggregate-range/empty — 없는 범위는 비어 있다");
		check(none.hierarchy().empty(), "aggregate-range/empty-rows — 빈 범위에 행이 없다");
	}

	//-------------------------------------------------------------------------
	// ⑪ 스레드가 섞여도 트리가 갈린다. 같은 marker 라도 스레드가 다르면
	//    다른 줄이어야 한다 — 그러지 않으면 워커의 시간이 게임 스레드에
	//    더해져 프레임 예산이 통째로 거짓이 된다.
	//-------------------------------------------------------------------------
	void test_aggregate_threads()
	{
		ce::profiler_service service;
		ce::profiler_config config;
		config.chunk_count = 256;
		service.initialize(config);
		service.register_thread("Main");
		service.record(1);

		std::thread worker([&service]()
		{
			service.register_thread("Worker");
			{
				ce::profile_scope scope{ service, ce::marker<"Shared">() };
				busy_ticks(4);
			}
			service.unregister_thread();
		});

		{
			ce::profile_scope scope{ service, ce::marker<"Shared">() };
			busy_ticks(4);
		}
		worker.join();

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		if (!capture)
		{
			check(false, "aggregate-thread/capture — 얼린 캡처가 있다");
			return;
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 1);
		check_eq(aggregate.threads().size(), std::size_t{ 2 },
		         "aggregate-thread/lanes — 스레드 둘이 레인 둘");
		check_eq(aggregate.hierarchy().size(), std::size_t{ 2 },
		         "aggregate-thread/rows — 같은 marker 라도 스레드가 다르면 다른 줄");

		// 두 줄의 thread_slot 이 실제로 다르다.
		if (aggregate.hierarchy().size() == 2)
		{
			check(aggregate.hierarchy()[0].thread_slot != aggregate.hierarchy()[1].thread_slot,
			      "aggregate-thread/slots — 두 줄의 스레드가 다르다");
		}

		// Flat 도 스레드를 섞지 않는다.
		std::size_t sharedRows = 0;
		for (const ce::aggregate_row& row : aggregate.flat())
		{
			if (row.marker == ce::marker<"Shared">()) ++sharedRows;
		}
		check_eq(sharedRows, std::size_t{ 2 }, "aggregate-thread/flat — Flat 도 스레드를 섞지 않는다");

		check_eq(aggregate.timeline_total_ticks(), aggregate.hierarchy_total_ticks(),
		         "aggregate-thread/totals — 스레드가 여럿이어도 두 합이 같다");
	}

	//-------------------------------------------------------------------------
	// ⑫ Flat 은 부모를 무시하고 접는다. 같은 marker 가 서로 다른 부모 아래
	//    있을 때 Hierarchy 는 두 줄, Flat 은 한 줄이어야 한다.
	//-------------------------------------------------------------------------
	void test_aggregate_flat()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		{
			ce::profile_scope a{ service, ce::marker<"ParentA">() };
			ce::profile_scope shared{ service, ce::marker<"SharedLeaf">() };
			busy_ticks(2);
		}
		{
			ce::profile_scope b{ service, ce::marker<"ParentB">() };
			ce::profile_scope shared{ service, ce::marker<"SharedLeaf">() };
			busy_ticks(3);
		}

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		if (!capture)
		{
			check(false, "aggregate-flat/capture — 얼린 캡처가 있다");
			return;
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 1);

		std::size_t hierarchyRows = 0;
		for (const ce::aggregate_row& row : aggregate.hierarchy())
		{
			if (row.marker == ce::marker<"SharedLeaf">()) ++hierarchyRows;
		}
		check_eq(hierarchyRows, std::size_t{ 2 },
		         "aggregate-flat/hierarchy — 부모가 다르면 Hierarchy 에서 두 줄");

		const ce::aggregate_row* flat = find_flat(aggregate, ce::marker<"SharedLeaf">());
		check(flat != nullptr, "aggregate-flat/row — Flat 에 줄이 있다");
		if (!flat)
		{
			return;
		}
		check_eq(flat->call_count, std::uint64_t{ 2 },
		         "aggregate-flat/calls — Flat 은 부모를 무시하고 합친다");

		// Flat 은 total 내림차순이다.
		for (std::size_t i = 1; i < aggregate.flat().size(); ++i)
		{
			check(aggregate.flat()[i - 1].total_ticks >= aggregate.flat()[i].total_ticks,
			      "aggregate-flat/order — Flat 은 total 내림차순");
		}

		check_eq(aggregate.timeline_total_ticks(), aggregate.hierarchy_total_ticks(),
		         "aggregate-flat/totals — 두 합이 같다");
	}

	//-------------------------------------------------------------------------
	// ⑬ 잘린 구간이 행에 전파된다. 표시하는 쪽은 이 줄의 합계를 다른 줄과
	//    나란히 두면 안 되므로, 집계가 그 사실을 잃으면 안 된다.
	//-------------------------------------------------------------------------
	void test_aggregate_truncated()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		// 프레임을 넘는 구간을 만든다 — 1 프레임에서 열고 2 에서 닫는다.
		{
			ce::profile_scope spanning{ service, ce::marker<"Spanning">() };
			busy_ticks(2);
			service.publish_frame(1);
			busy_ticks(2);
		}
		service.publish_frame(2);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		if (!capture)
		{
			check(false, "aggregate-truncated/capture — 얼린 캡처가 있다");
			return;
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 2);
		const ce::aggregate_row* row = find_row(aggregate, ce::marker<"Spanning">(), 0);
		check(row != nullptr, "aggregate-truncated/row — 프레임을 넘는 구간이 집계에 있다");
		if (!row)
		{
			return;
		}
		check(row->total_ticks > 0, "aggregate-truncated/length — 길이가 0 이 아니다");
	}
	//-------------------------------------------------------------------------
	// ⑭ PHASE 14 P3 — reader 상태. 완료조건 둘이 여기 있다:
	//    "10초 녹화 후 과거 프레임 선택 가능" 과 "pause 후 엔진이 계속 돌아도
	//    선택 자료가 변하지 않음".
	//-------------------------------------------------------------------------
	void test_reader_selection()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		// 프레임마다 호출 수를 다르게 한다 — 과거 프레임을 골랐을 때 그
		// 프레임의 자료가 나오는지 값으로 가릴 수 있어야 한다.
		for (std::uint32_t frame = 1; frame <= 10; ++frame)
		{
			for (std::uint32_t n = 0; n < frame; ++n)
			{
				ce::profile_scope scope{ service, ce::marker<"ReaderTick">() };
				busy_ticks(1);
			}
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());

		check(reader.has_capture(), "reader/capture — 얼린 캡처를 받았다");
		if (!reader.has_capture())
		{
			return;
		}

		check_eq(reader.available_first(), std::uint32_t{ 1 }, "reader/first — 가장 오래된 프레임");
		check_eq(reader.available_last(), std::uint32_t{ 10 }, "reader/last — 가장 최신 프레임");

		// live follow 가 기본이므로 선택은 최신이다.
		check(reader.live_follow(), "reader/follow-default — 기본은 따라가기");
		check_eq(reader.selected_first(), std::uint32_t{ 10 }, "reader/follow — 최신을 고른다");

		{
			const ce::aggregate_row* row =
				find_flat(reader.aggregate(), ce::marker<"ReaderTick">());
			check(row != nullptr, "reader/row — 선택한 프레임에 행이 있다");
			if (row)
			{
				check_eq(row->call_count, std::uint64_t{ 10 },
				         "reader/latest-calls — 최신 프레임은 열 번");
			}
		}

		// ★ 과거 프레임 선택. 3 번 프레임은 세 번 불렸다.
		reader.select_frame(3);
		check_eq(reader.selected_first(), std::uint32_t{ 3 }, "reader/select — 과거 프레임을 고른다");
		{
			const ce::aggregate_row* row =
				find_flat(reader.aggregate(), ce::marker<"ReaderTick">());
			check(row != nullptr, "reader/past-row — 과거 프레임에 행이 있다");
			if (row)
			{
				check_eq(row->call_count, std::uint64_t{ 3 },
				         "reader/past-calls — 고른 프레임의 자료가 나온다");
			}
		}

		// 범위 선택. 1..4 는 1+2+3+4 = 10 번.
		reader.select_range(1, 4);
		check_eq(reader.selected_count(), std::uint32_t{ 4 }, "reader/range — 네 프레임을 고른다");
		{
			const ce::aggregate_row* row =
				find_flat(reader.aggregate(), ce::marker<"ReaderTick">());
			if (row)
			{
				check_eq(row->call_count, std::uint64_t{ 10 },
				         "reader/range-calls — 범위의 합이 나온다");
			}
		}

		// 뒤집어 줘도 같은 범위다.
		reader.select_range(4, 1);
		check_eq(reader.selected_first(), std::uint32_t{ 1 }, "reader/swap-first — 뒤집힌 범위를 바로잡는다");
		check_eq(reader.selected_last(), std::uint32_t{ 4 }, "reader/swap-last — 뒤집힌 범위를 바로잡는다");

		// 범위 밖을 고르면 안으로 자른다.
		reader.select_range(900, 901);
		check_eq(reader.selected_first(), std::uint32_t{ 10 }, "reader/clamp — 범위 밖은 안으로 자른다");
	}

	//-------------------------------------------------------------------------
	// ⑮ ★ 완료조건: pause 뒤 엔진이 계속 돌아도 손에 든 자료가 변하지 않는다.
	//-------------------------------------------------------------------------
	void test_reader_frozen_while_running()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		for (std::uint32_t frame = 1; frame <= 3; ++frame)
		{
			ce::profile_scope scope{ service, ce::marker<"Before">() };
			busy_ticks(1);
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());
		reader.set_live_follow(false);
		reader.select_frame(2);

		const ce::frame_aggregate& before = reader.aggregate();
		const std::uint64_t beforeEvents = before.event_count();
		const ce::profile_tick beforeTotal = before.timeline_total_ticks();
		const std::size_t beforeRows = before.hierarchy().size();
		check(beforeEvents > 0, "reader-frozen/before — 얼리기 전에 자료가 있다");

		// 엔진이 계속 돈다. 녹화를 다시 켜고 다른 marker 로 여러 프레임.
		service.record(4);
		for (std::uint32_t frame = 4; frame <= 40; ++frame)
		{
			for (int n = 0; n < 5; ++n)
			{
				ce::profile_scope scope{ service, ce::marker<"After">() };
				busy_ticks(1);
			}
			service.publish_frame(frame);
		}

		// reader 는 아무것도 하지 않았다. 손에 든 것이 그대로여야 한다.
		const ce::frame_aggregate& after = reader.aggregate();
		check_eq(after.event_count(), beforeEvents,
		         "reader-frozen/events — 엔진이 돌아도 이벤트 수가 그대로다");
		check_eq(after.timeline_total_ticks(), beforeTotal,
		         "reader-frozen/total — 엔진이 돌아도 합계가 그대로다");
		check_eq(after.hierarchy().size(), beforeRows,
		         "reader-frozen/rows — 엔진이 돌아도 행 수가 그대로다");
		check_eq(reader.selected_first(), std::uint32_t{ 2 },
		         "reader-frozen/selection — 선택이 저절로 움직이지 않는다");

		// 새로 얼린 캡처에는 After 가 있다 — 엔진이 실제로 돌았다는 확인이다.
		// (이 줄이 없으면 위의 '변하지 않았다' 가 '아무 일도 없었다' 와
		//  구분되지 않는다.)
		service.pause();
		ce::capture_session_ptr fresh = service.capture();
		check(fresh != nullptr && count_marker(*fresh, ce::marker<"After">()) > 0,
		      "reader-frozen/engine-ran — 그동안 엔진이 실제로 기록했다");
	}

	//-------------------------------------------------------------------------
	// ⑯ Live Follow. 켜면 새 캡처를 받을 때 최신으로 가고, 끄면 보던 자리를
	//    지킨다 — 스파이크를 붙잡아 두는 것이 그 토글의 쓸모다.
	//-------------------------------------------------------------------------
	void test_reader_live_follow()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		auto run_frames = [&service](std::uint32_t first, std::uint32_t last)
		{
			for (std::uint32_t frame = first; frame <= last; ++frame)
			{
				ce::profile_scope scope{ service, ce::marker<"FollowTick">() };
				busy_ticks(1);
				service.publish_frame(frame);
			}
		};

		run_frames(1, 5);
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());
		check_eq(reader.selected_first(), std::uint32_t{ 5 }, "reader-follow/initial — 최신을 고른다");

		// 따라가기를 끄고 과거를 붙잡는다.
		reader.set_live_follow(false);
		reader.select_frame(2);

		service.record(6);
		run_frames(6, 12);
		service.pause();
		reader.adopt(service.capture());

		check_eq(reader.selected_first(), std::uint32_t{ 2 },
		         "reader-follow/held — 끄면 보던 자리를 지킨다");
		check_eq(reader.available_last(), std::uint32_t{ 12 },
		         "reader-follow/available — 새 캡처의 범위는 갱신된다");

		// 다시 켜면 그 자리에서 최신으로 간다.
		reader.set_live_follow(true);
		check_eq(reader.selected_first(), std::uint32_t{ 12 },
		         "reader-follow/resume — 켜면 최신으로 간다");

		service.record(13);
		run_frames(13, 20);
		service.pause();
		reader.adopt(service.capture());
		check_eq(reader.selected_first(), std::uint32_t{ 20 },
		         "reader-follow/latest — 켜 두면 계속 따라간다");

		reader.reset();
		check(!reader.has_capture(), "reader-follow/reset — 놓으면 비어 있다");
		check(reader.aggregate().hierarchy().empty(),
		      "reader-follow/reset-empty — 놓은 뒤 집계가 비어 있다");
	}

	//-------------------------------------------------------------------------
	// ⑰ 캐시. 같은 선택을 두 번 물어도 다시 접지 않는다. "느려지지 않았다" 를
	//    말로 적으면 아무도 재지 않으므로 접은 횟수를 밖에서 보이게 둔다.
	//-------------------------------------------------------------------------
	void test_reader_fold_cache()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		for (std::uint32_t frame = 1; frame <= 5; ++frame)
		{
			ce::profile_scope scope{ service, ce::marker<"CacheTick">() };
			busy_ticks(1);
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());
		check_eq(reader.fold_count(), std::uint64_t{ 0 }, "reader-cache/lazy — 묻기 전에는 접지 않는다");

		(void)reader.aggregate();
		(void)reader.aggregate();
		(void)reader.aggregate();
		check_eq(reader.fold_count(), std::uint64_t{ 1 }, "reader-cache/reuse — 같은 선택은 한 번만 접는다");

		reader.select_frame(2);
		(void)reader.aggregate();
		check_eq(reader.fold_count(), std::uint64_t{ 2 }, "reader-cache/invalidate — 선택이 바뀌면 다시 접는다");

		// 같은 자리를 다시 고르는 것은 바뀐 것이 아니다.
		reader.select_frame(2);
		(void)reader.aggregate();
		check_eq(reader.fold_count(), std::uint64_t{ 2 }, "reader-cache/same — 같은 자리를 다시 골라도 그대로");
	}
	//-------------------------------------------------------------------------
	// ⑱ PHASE 14 P3 — Timeline 이 그리는 원시 스팬.
	//
	//    집계가 세운 순서를 그대로 남긴 것이다. 그리는 층이 다시 정렬하면 두
	//    정렬이 갈리는 순간 표와 타임라인이 서로 다른 트리를 말하게 된다.
	//-------------------------------------------------------------------------
	void test_timeline_spans()
	{
		ce::profiler_service service;
		ce::profiler_config config;
		config.chunk_count = 256;
		service.initialize(config);
		service.register_thread("Main");
		service.record(1);

		std::thread worker([&service]()
		{
			service.register_thread("Worker");
			for (int i = 0; i < 3; ++i)
			{
				ce::profile_scope scope{ service, ce::marker<"SpanWorker">() };
				busy_ticks(2);
			}
			service.unregister_thread();
		});

		{
			ce::profile_scope outer{ service, ce::marker<"SpanOuter">() };
			busy_ticks(2);
			{
				ce::profile_scope inner{ service, ce::marker<"SpanInner">() };
				busy_ticks(1);
			}
		}
		worker.join();

		service.publish_frame(1);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		if (!capture)
		{
			check(false, "timeline/capture — 얼린 캡처가 있다");
			return;
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 1);
		const std::span<const ce::profile_event> spans = aggregate.spans();

		check_eq(static_cast<std::uint64_t>(spans.size()), aggregate.event_count(),
		         "timeline/count — 스팬 수가 이벤트 수와 같다");
		check(!spans.empty(), "timeline/nonempty — 스팬이 있다");
		if (spans.empty())
		{
			return;
		}

		// ★ 전위 순서. 스레드 → 시작 tick → depth.
		for (std::size_t i = 1; i < spans.size(); ++i)
		{
			const ce::profile_event& previous = spans[i - 1];
			const ce::profile_event& current = spans[i];
			const bool ordered =
				(previous.thread_slot < current.thread_slot) ||
				(previous.thread_slot == current.thread_slot &&
				 (previous.tick_begin < current.tick_begin ||
				  (previous.tick_begin == current.tick_begin &&
				   previous.depth <= current.depth)));
			check(ordered, "timeline/order — 스팬이 전위 순서로 서 있다");
		}

		// 스레드마다 자기 몫이 연속이다. 레인을 그리는 쪽이 그 범위만 훑는다.
		std::uint32_t covered = 0;
		for (const ce::thread_summary& thread : aggregate.threads())
		{
			check(thread.span_begin <= thread.span_end,
			      "timeline/range-order — 스팬 범위가 뒤집히지 않았다");
			check_eq(thread.span_end - thread.span_begin, thread.event_count,
			         "timeline/range-count — 범위 길이가 이벤트 수와 같다");
			for (std::uint32_t i = thread.span_begin; i < thread.span_end; ++i)
			{
				check_eq(spans[i].thread_slot, thread.thread_slot,
				         "timeline/range-owner — 범위 안은 전부 그 스레드의 것");
			}
			covered += thread.span_end - thread.span_begin;
		}
		check_eq(static_cast<std::size_t>(covered), spans.size(),
		         "timeline/range-cover — 범위들이 스팬을 빠짐없이 덮는다");

		// 스팬은 선택 구간의 벽시계 안에 있다.
		for (const ce::profile_event& span : spans)
		{
			check(span.tick_end >= span.tick_begin,
			      "timeline/span-order — 끝이 시작보다 앞서지 않는다");
		}
	}

	//-------------------------------------------------------------------------
	// ⑲ Timeline 의 가로 시야. 확대·이동이 구간 밖으로 나가지 않는다 —
	//    나갈 수 있으면 빈 화면을 보게 되고, 그때 사용자는 계측이 없다고 읽는다.
	//-------------------------------------------------------------------------
	void test_timeline_view()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		for (std::uint32_t frame = 1; frame <= 6; ++frame)
		{
			ce::profile_scope scope{ service, ce::marker<"ViewTick">() };
			busy_ticks(3);
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());
		reader.set_live_follow(false);
		reader.select_range(1, 6);

		const ce::frame_aggregate& folded = reader.aggregate();
		const ce::profile_tick low = folded.tick_begin();
		const ce::profile_tick high = folded.tick_end();
		check(high > low, "timeline-view/span — 선택 구간에 길이가 있다");
		if (high <= low)
		{
			return;
		}

		// 처음에는 구간 전체를 본다.
		check_eq(reader.view_begin(), low, "timeline-view/initial-begin — 처음엔 구간 전체");
		check_eq(reader.view_end(), high, "timeline-view/initial-end — 처음엔 구간 전체");

		// 확대하면 좁아지고, 가운데를 잡았으면 가운데가 제자리다.
		const ce::profile_tick pivot = low + (high - low) / 2;
		const ce::profile_tick fullSpan = reader.view_span();
		reader.zoom_view(0.5, pivot);
		check(reader.view_span() < fullSpan, "timeline-view/zoom-in — 확대하면 좁아진다");
		check(reader.view_begin() >= low && reader.view_end() <= high,
		      "timeline-view/zoom-bounds — 확대해도 구간 안에 있다");
		check(reader.view_begin() <= pivot && pivot <= reader.view_end(),
		      "timeline-view/zoom-pivot — 잡은 자리가 시야에 남는다");

		// 아무리 확대해도 0 폭이 되지 않는다. tick 이 정수라 반올림이 시야를
		// 뒤집으면 begin > end 가 되고 그리는 쪽이 음수 폭을 만난다.
		for (int i = 0; i < 40; ++i)
		{
			reader.zoom_view(0.5, pivot);
		}
		check(reader.view_span() > 0, "timeline-view/zoom-floor — 아무리 확대해도 폭이 0 이 아니다");
		check(reader.view_end() > reader.view_begin(), "timeline-view/zoom-sane — 시야가 뒤집히지 않는다");

		// 멀리 밀어도 구간 밖으로 나가지 않는다.
		reader.pan_view(static_cast<std::int64_t>(high - low) * 10);
		check(reader.view_end() <= high, "timeline-view/pan-right — 오른쪽으로 새지 않는다");
		check(reader.view_begin() >= low, "timeline-view/pan-right-begin — 시작도 구간 안");

		reader.pan_view(-static_cast<std::int64_t>(high - low) * 10);
		check(reader.view_begin() >= low, "timeline-view/pan-left — 왼쪽으로 새지 않는다");

		// 축소는 구간 전체보다 넓어지지 않는다.
		for (int i = 0; i < 40; ++i)
		{
			reader.zoom_view(2.0, pivot);
		}
		check_eq(reader.view_span(), high - low, "timeline-view/zoom-out-cap — 구간 전체보다 넓어지지 않는다");

		// ★ 선택이 바뀌면 시야가 그 구간으로 돌아간다. 전에 보던 자리가 남아
		//   있으면 다른 프레임을 골랐을 때 빈 화면이 나온다.
		reader.zoom_view(0.25, pivot);
		reader.select_frame(2);
		const ce::frame_aggregate& one = reader.aggregate();
		check_eq(reader.view_begin(), one.tick_begin(),
		         "timeline-view/reset-on-select — 선택이 바뀌면 시야가 돌아간다");
		check_eq(reader.view_end(), one.tick_end(),
		         "timeline-view/reset-on-select-end — 선택이 바뀌면 시야가 돌아간다");

		reader.zoom_view(0.25, one.tick_begin());
		reader.reset_view();
		check_eq(reader.view_begin(), one.tick_begin(), "timeline-view/reset — 손으로도 되돌린다");
	}

	//-------------------------------------------------------------------------
	// ㉑ 녹화 경계를 넘는 스코프.
	//
	//    pause·record 가 구간 한가운데서 일어나도 짝이 어긋나지 않는다.
	//    여는 쪽과 닫는 쪽에 같은 상태 관문을 걸었더니, 닫는 쪽이 얼어 버려
	//    스택에 한 칸이 남고 그 뒤의 모든 구간이 한 칸씩 깊어졌다.
	//
	//    ★ 조용한 결함이다. 아무것도 실패하지 않고 깊이만 밀린다 — 그래서
	//      불균형 계수기만 보는 단정으로는 부족하고, 경계 뒤의 중첩까지 재야 한다.
	//-------------------------------------------------------------------------
	void test_scope_across_state_change()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		// ㈚ 열고 나서 얼린다 — 닫는 쪽이 얼린 뒤에 온다.
		{
			ce::profile_scope outer{ service, ce::marker<"AcrossPause">() };
			busy_ticks(1);
			service.pause();
		}
		check_eq(service.summary().unbalanced_scopes, static_cast<std::uint64_t>(0),
		         "state-change/close-while-frozen — 얼린 뒤에 닫아도 짝이 맞는다");

		// ㈛ 얼린 채로 열고, 녹화를 다시 열어 놓고 닫는다 — 여는 쪽이 없었다.
		{
			ce::profile_scope outer{ service, ce::marker<"AcrossRecord">() };
			busy_ticks(1);
			service.record(2);
		}
		check_eq(service.summary().unbalanced_scopes, static_cast<std::uint64_t>(0),
		         "state-change/open-while-frozen — 얼린 채 연 것을 닫아도 짝이 맞는다");

		// ★ 경계를 두 번 넘고 난 뒤의 중첩이 제 깊이로 선다. 짝이 하나
		//   어긋나 있으면 여기서 깊이가 밀린다 — 불균형 계수기는 0 인 채로.
		{
			ce::profile_scope outer{ service, ce::marker<"AfterBoundaryOuter">() };
			busy_ticks(1);
			{
				ce::profile_scope inner{ service, ce::marker<"AfterBoundaryInner">() };
				busy_ticks(1);
			}
		}

		service.publish_frame(2);
		service.pause();

		ce::capture_session_ptr capture = service.capture();
		if (!capture)
		{
			check(false, "state-change/capture — 얼린 캡처가 있다");
			return;
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 2, 2);

		// ★ 깊이를 찾는 조건에 건다. 짝이 하나 어긋나 있으면 이름은 그대로인 채
		//   깊이만 밀리므로, 이름만 찾으면 밀린 것을 찾아내지 못한다.
		const ce::aggregate_row* outer =
			find_row(aggregate, ce::marker<"AfterBoundaryOuter">(), 0);
		const ce::aggregate_row* inner =
			find_row(aggregate, ce::marker<"AfterBoundaryInner">(), 1);

		check(outer != nullptr,
		      "state-change/outer-depth — 경계 뒤의 바깥 구간이 깊이 0 으로 선다");
		check(inner != nullptr,
		      "state-change/inner-depth — 경계 뒤의 안쪽 구간이 깊이 1 로 선다");

		// 얼린 채 연 구간은 재지 않았으므로 표에 없어야 한다. 남아 있으면
		// 녹화를 멈춰 둔 동안의 시간이 구간 길이로 들어가 숨는다.
		check(find_flat(aggregate, ce::marker<"AcrossRecord">()) == nullptr,
		      "state-change/skipped-not-recorded — 얼린 채 연 구간은 표에 없다");
	}

	//-------------------------------------------------------------------------
	// ㉒ 창이 매 프레임 부르는 따라가기 규칙(capture_reader::sync).
	//
	//    이것이 창이 아니라 코어에 있는 이유는 재기 위해서다. 예전에는
	//    창이 `state == frozen` 을 보고 집었는데, 얼린 순간은 보는 쪽이 한 번도
	//    못 볼 수 있는 찰나라 어느 날은 빈 화면이 나왔다. 그 규칙이 화면에
	//    있었으므로 게이트가 물 자리가 없었다.
	//-------------------------------------------------------------------------
	void test_reader_sync()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");

		auto take = [&service](std::uint32_t frame) -> ce::capture_session_ptr
		{
			service.record(frame);
			{
				ce::profile_scope scope{ service, ce::marker<"SyncTick">() };
				busy_ticks(2);
			}
			service.publish_frame(frame);
			service.pause();
			return service.capture();
		};

		ce::capture_reader reader;

		// 손에도 없고 서비스에도 없으면 아무 일도 없다.
		check(!reader.sync(nullptr), "reader-sync/empty — 내놓은 것이 없으면 갈아타지 않는다");
		check(!reader.has_capture(), "reader-sync/empty-stays — 그래도 빈 손 그대로다");

		const ce::capture_session_ptr first = take(1);
		check(static_cast<bool>(first), "reader-sync/first-exists — 첫 캡처가 섬");
		check(reader.sync(first), "reader-sync/first — 빈 손이면 첫 하나를 집는다");
		check(reader.capture() == first.get(), "reader-sync/first-same — 집은 것이 그것이다");

		// ★ 같은 것을 다시 주면 손대지 않는다. 창이 매 프레임 부르므로, 여기서
		//   매번 adopt 하면 선택과 시야가 매 프레임 초기화되고 접은 결과가 버려진다.
		//
		// ★ 이 절은 **따라가기를 켜 둔 채로** 재야 한다. 꺼 두면 붙잡아 둔 것을 지키는
		//   절이 먼저 막아서, 같은 것인지 가리는 절을 걷어도 단정이 통과한다 —
		//   두 층이 같은 절을 막으면 변이가 조용히 살아남는다.
		check(reader.live_follow(), "reader-sync/follow-default — 기본값은 따라가기다");
		reader.select_frame(1);
		const std::uint64_t foldsBefore = reader.fold_count();
		(void)reader.aggregate();
		check(!reader.sync(first), "reader-sync/same — 같은 것을 다시 주면 갈아타지 않는다");
		(void)reader.aggregate();
		check_eq(reader.fold_count(), foldsBefore + 1,
		         "reader-sync/same-no-refold — 같은 것을 받았다고 다시 접지 않는다");

		reader.set_live_follow(false);

		// 따라가지 않기로 했으면 새로 얼린 것이 와도 보던 것을 지킨다.
		const ce::capture_session_ptr second = take(2);
		check(second.get() != first.get(), "reader-sync/second-differs — 두 번째는 다른 것이다");
		check(!reader.sync(second), "reader-sync/pinned — 꺼 두었으면 갈아타지 않는다");
		check(reader.capture() == first.get(), "reader-sync/pinned-keeps — 보던 것을 그대로 든다");

		// 켜면 새로 얼린 것으로 갈아탄다. 그것이 그 체크박스가 적은 약속이다.
		reader.set_live_follow(true);
		check(reader.sync(second), "reader-sync/follow — 켜 두었으면 새로 얼린 것으로 간다");
		check(reader.capture() == second.get(), "reader-sync/follow-latest — 손에 든 것이 최신이다");

		// ★ 서비스가 빈손이라고 보던 것을 버리지는 않는다. 놓는 것은 Clear 가
		//   reset() 으로 명시하는 일이고, 그것을 여기서 흔들면 붙잡아 둔 것이 사라진다.
		check(!reader.sync(nullptr), "reader-sync/null — 빈 것을 주면 갈아타지 않는다");
		check(reader.capture() == second.get(), "reader-sync/null-keeps — 보던 것을 잃지 않는다");
	}
}


// ── GPU 레인: 늦게 온 구간이 제 프레임 칸으로 돌아가는가 ──────────────
//
// ★ 이것이 이 조각의 계약 전부다. capture_ring 은 이벤트를 **수집한 프레임**의
//   기록에 담는데, GPU 구간은 펜스가 끝난 뒤에야 읽히므로 늦게 온다(실측
//   제출→수집 최대 54.6 ms — 60 Hz 로 세 프레임이 넘는다). 표식을 보고
//   돌려보내지 않으면 GPU 일이 세 칸 뒤에 그려진다.
void test_gpu_lane()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	// 프레임 1~5 를 돌린다. GPU 구간은 **프레임 2 의 것**인데 프레임 5 를
	// 닫기 직전에야 도착한다.
	const ce::profile_tick base = ce::profiler_service::now();
	for (std::uint32_t frame = 1; frame <= 5; ++frame)
	{
		if (frame == 5)
		{
			service.submit_gpu_span(ce::marker<"GpuPass">(), base + 10, base + 20, 2);
			service.publish_gpu_spans();
		}
		service.publish_frame(frame);
	}

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "gpu-lane/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	// 레인이 이름을 달고 표에 서 있다.
	bool laneFound = false;
	std::uint16_t laneSlot = 0;
	for (const ce::thread_info& info : capture->threads())
	{
		if (info.name != ce::profiler_service::kGpuLaneName) continue;
		laneFound = true;
		laneSlot = static_cast<std::uint16_t>(info.slot);
	}
	check(laneFound, "gpu-lane/name — [GPU Graphics] 레인이 표에 있다");

	// ★ 구간은 **프레임 2** 칸에 있어야 한다. 도착한 프레임 5 가 아니다.
	std::size_t inFrameTwo = 0;
	std::size_t inOtherFrames = 0;
	for (const ce::frame_record& frame : capture->frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (!ce::has_flag(event.flags, ce::event_flags::gpu_span)) continue;
			if (frame.engine_frame == 2) ++inFrameTwo;
			else ++inOtherFrames;

			check(event.thread_slot == laneSlot,
			      "gpu-lane/slot — 구간이 GPU 레인 자리에 달렸다");
			check(event.tick_begin == base + 10 && event.tick_end == base + 20,
			      "gpu-lane/ticks — 넘긴 틱이 그대로 남는다");
			check(event.frame == 2, "gpu-lane/frame-label — 라벨이 제 프레임이다");
		}
	}
	check_eq<std::size_t>(inFrameTwo, 1, "gpu-lane/placed — 제 프레임 칸에 들어갔다");
	check_eq<std::size_t>(inOtherFrames, 0,
	                      "gpu-lane/no-skew — 수집한 프레임 칸에 남지 않았다");

	service.shutdown();
}

// 아직 닫히지 않은 프레임의 구간은 기다렸다가 그 프레임이 닫힐 때 들어간다.
void test_gpu_lane_deferred()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	const ce::profile_tick base = ce::profiler_service::now();

	// 프레임 1 을 닫은 뒤, **아직 오지 않은** 프레임 3 의 구간을 낸다.
	service.publish_frame(1);
	service.submit_gpu_span(ce::marker<"GpuAhead">(), base + 1, base + 2, 3);
	service.publish_gpu_spans();
	service.publish_frame(2);   // 아직 3 이 아니다 — 기다려야 한다
	service.publish_frame(3);   // 여기서 들어간다
	service.publish_frame(4);

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "gpu-deferred/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	std::size_t inFrameThree = 0;
	std::size_t elsewhere = 0;
	for (const ce::frame_record& frame : capture->frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (!ce::has_flag(event.flags, ce::event_flags::gpu_span)) continue;
			if (frame.engine_frame == 3) ++inFrameThree;
			else ++elsewhere;
		}
	}
	check_eq<std::size_t>(inFrameThree, 1,
	                      "gpu-deferred/placed — 그 프레임이 닫힐 때 들어간다");
	check_eq<std::size_t>(elsewhere, 0,
	                      "gpu-deferred/no-early — 닫히기 전 칸에 끼지 않는다");

	service.shutdown();
}

// 그 프레임이 이미 링 밖으로 밀려났으면 **버리고 센다.** 조용히 사라지면
// "GPU 레인이 비었다" 와 "늦어서 잃었다" 가 구분되지 않는다.
void test_gpu_lane_dropped()
{
	ce::profiler_config config;
	config.retained_frames = 3;

	ce::profiler_service service;
	service.initialize(config);
	service.record(1);

	const ce::profile_tick base = ce::profiler_service::now();
	for (std::uint32_t frame = 1; frame <= 8; ++frame)
	{
		service.publish_frame(frame);
	}

	// 프레임 1 은 이미 밀려났다.
	service.submit_gpu_span(ce::marker<"GpuGone">(), base + 1, base + 2, 1);
	service.publish_gpu_spans();
	service.publish_frame(9);

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "gpu-dropped/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	std::size_t spans = 0;
	for (const ce::frame_record& frame : capture->frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (ce::has_flag(event.flags, ce::event_flags::gpu_span)) ++spans;
		}
	}
	check_eq<std::size_t>(spans, 0, "gpu-dropped/absent — 갈 곳이 없으면 넣지 않는다");
	check(service.summary().late_spans_dropped > 0,
	      "gpu-dropped/counted — 버렸다는 사실을 센다");

	service.shutdown();
}


// ── 적는 동안 거두기 ──────────────────────────────────────────────────
//
// ★ 기존 multithread 검사는 워커를 전부 join() 한 **뒤에** 수집한다. 그래서
//   "writer 가 계속 적는 동안 collector 가 거둔다" 는 경계를 한 번도 자극하지
//   않았다. 설계는 "writer 는 자기 스트림만, 수집기는 봉인된 것만" 이라고
//   적혀 있지만, publish_frame 은 등록된 **모든** 스트림의 현재 청크를
//   봉인하고 writer 는 그 잠금을 잡지 않는다.
//
//   판정은 계수다. 봉인과 쓰기가 겹쳐 이벤트를 잃으면 드롭으로 세지 않고
//   사라지므로 "수집분 + 드롭 = 발생분" 이 깨진다.
void test_concurrent_publish()
{
	constexpr int kWorkers = 4;
	constexpr int kScopesPerWorker = 20000;

	ce::profiler_service service;
	ce::profiler_config config;
	config.chunk_count = 512;
	config.retained_frames = 4096;
	service.initialize(config);
	service.register_thread("Main");
	service.record(1);

	std::atomic<int> finished{ 0 };
	std::vector<std::thread> workers;
	workers.reserve(kWorkers);

	for (int i = 0; i < kWorkers; ++i)
	{
		workers.emplace_back([&service, &finished, i]()
		{
			const std::string name = "Worker" + std::to_string(i);
			service.register_thread(name.c_str());

			for (int n = 0; n < kScopesPerWorker; ++n)
			{
				ce::profile_scope scope{ service, ce::marker<"ConcurrentScope">() };
			}

			service.unregister_thread();
			finished.fetch_add(1, std::memory_order_release);
		});
	}

	// ★ 워커가 적는 **동안** 프레임을 계속 닫는다. 이것이 자극이다.
	std::uint32_t frame = 1;
	while (finished.load(std::memory_order_acquire) < kWorkers)
	{
		service.publish_frame(frame++);
	}

	for (std::thread& worker : workers)
	{
		worker.join();
	}

	service.publish_frame(frame);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "concurrent-publish/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	const ce::live_summary summary = service.summary();
	const std::size_t seen = count_marker(*capture, ce::marker<"ConcurrentScope">());
	const std::uint64_t expected =
		static_cast<std::uint64_t>(kWorkers) * kScopesPerWorker;

	// 잃었으면 드롭으로 세어야 한다. 세지 않고 사라지는 것이 이 경계의 결함이다.
	check_eq(seen + summary.dropped_events, expected,
	         "concurrent-publish/accounted — 수집분 + 드롭 = 발생분");
	check_eq(summary.unbalanced_scopes, std::uint64_t{ 0 },
	         "concurrent-publish/balanced — 불균형 스코프 0");

	service.shutdown();
}

int main()
{
	test_marker_identity();
	test_basic_capture();
	test_cross_frame_scope();
	test_multithread_stress();
	test_pool_exhaustion();
	test_service_isolation();
	test_recorder_states();
	test_rolling_retention();
	test_aggregate_tree();
	test_aggregate_range();
	test_aggregate_threads();
	test_aggregate_flat();
	test_aggregate_truncated();
	test_reader_selection();
	test_reader_frozen_while_running();
	test_reader_live_follow();
	test_reader_fold_cache();
	test_timeline_spans();
	test_timeline_view();
	test_scope_across_state_change();
	test_reader_sync();
	test_gpu_lane();
	test_gpu_lane_deferred();
	test_gpu_lane_dropped();
	test_concurrent_publish();

	std::printf("profile core probe: %d checks, %d failures\n", g_checks, g_failures);
	if (g_failures == 0)
	{
		std::printf("PROFILE_CORE_OK=true\n");
	}
	return g_failures == 0 ? 0 : 1;
}
