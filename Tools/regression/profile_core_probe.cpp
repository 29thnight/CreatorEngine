// PHASE 14 P1+P2 — 새 프로파일러 코어 프로브.
//
// 엔진을 띄우지 않는다. EngineDiagnostics 가 ProjectReference 0 의 독립
// 라이브러리이고 서비스가 인스턴스로 서므로, 코어만 링크해 초 단위로 돈다.
// 옛 코어는 전역 싱글톤 하나에 함수 지역 static thread_local 을 공유해서
// 이런 프로브가 불가능했고, 그래서 selftest 가 라이브 캡처의 프레임 경계를
// 직접 넘겨야 했다(그 교란이 stats 기준선을 못 믿게 만들었다).
//
// 판정은 종료 코드다. 실패 사유는 stderr 로 낸다.
#if defined(_WIN32)
#include <windows.h>
#include <crtdbg.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
				// ★ 스코프를 **닫고** 프레임을 넘긴다. 닫기 전에 넘기면 그 구간은
				//   다음 프레임의 것이 되고, 그러면 이 자극은 "프레임마다 한 틱"
				//   이 아니라 "한 칸씩 밀린 틱" 을 만든다.
				{
					ce::profile_scope scope{ service, ce::marker<"FollowTick">() };
					busy_ticks(1);
				}
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
	// ⑯-2 프레임 그래프의 창. 막대 폭이 고정이므로 화면이 담는 프레임 수는
	//     그리는 쪽이 정하고, 창은 최신을 따라 **흐른다** — 새 프레임이
	//     오른쪽에서 들어오고 옛 프레임은 왼쪽으로 밀려 나간다. 뒤로 굴리면
	//     그 자리에 멈춰 있어야 옛 기록을 읽을 수 있다.
	//-------------------------------------------------------------------------
	void test_reader_graph_window()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("Main");
		service.record(1);

		auto run_frames = [&service](std::uint32_t first, std::uint32_t last)
		{
			for (std::uint32_t frame = first; frame <= last; ++frame)
			{
				{
					ce::profile_scope scope{ service, ce::marker<"GraphTick">() };
					busy_ticks(1);
				}
				service.publish_frame(frame);
			}
		};

		run_frames(1, 40);
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());

		// 창을 세우기 전에는 보존 전체다.
		check_eq(reader.graph_first(), std::uint32_t{ 1 }, "graph/default-first");
		check_eq(reader.graph_count(), std::uint32_t{ 40 }, "graph/default-all");
		check_eq(reader.graph_last(), std::uint32_t{ 40 }, "graph/default-last");

		// 화면이 10 프레임만 담는다면 **오른쪽 끝이 최신**이어야 한다. 왼쪽
		// 끝에 붙이면 녹화 중에 최신 프레임이 화면 밖에 있게 된다.
		reader.set_graph_span(10);
		check_eq(reader.graph_count(), std::uint32_t{ 10 }, "graph/span-count");
		check_eq(reader.graph_last(), std::uint32_t{ 40 }, "graph/span-anchors-right");
		check_eq(reader.graph_first(), std::uint32_t{ 31 }, "graph/span-first");

		// 새 캡처가 오면 창이 흐른다 — 폭은 그대로, 오른쪽 끝만 최신으로.
		service.record(41);
		run_frames(41, 50);
		service.pause();
		reader.adopt(service.capture());
		check_eq(reader.graph_count(), std::uint32_t{ 10 }, "graph/flow-keeps-span");
		check_eq(reader.graph_last(), std::uint32_t{ 50 }, "graph/flows");
		check_eq(reader.graph_first(), std::uint32_t{ 41 }, "graph/flow-first");

		// 뒤로 굴리면 따라가기가 풀리고 그 자리에 선다.
		check_eq(reader.selected_first(), std::uint32_t{ 50 }, "graph/pan-selection-before");
		reader.pan_graph(-20);
		check(!reader.live_follow(), "graph/pan-stops-following");
		check_eq(reader.graph_first(), std::uint32_t{ 21 }, "graph/pan-back");
		check_eq(reader.graph_last(), std::uint32_t{ 30 }, "graph/pan-window");

		// ★ 고른 프레임도 같은 만큼 움직인다. 축은 하나다 — 그래프 창만
		//   밀리고 선택이 제자리면 아래 타임라인이 안 따라오고, 그때 스크롤은
		//   위쪽 그림만 흔드는 것이 된다.
		check_eq(reader.selected_first(), std::uint32_t{ 30 }, "graph/selection-follows-pan");
		check_eq(reader.selected_last(), std::uint32_t{ 30 }, "graph/selection-keeps-width");

		// ★ 굴려 놓은 자리는 새 캡처가 와도 지켜진다. 이것이 "옛 기록이 왼쪽에
		//   남는다" 의 실체다 — 여기서 최신으로 튀면 읽던 구간을 잃는다.
		service.record(51);
		run_frames(51, 60);
		service.pause();
		reader.adopt(service.capture());
		check_eq(reader.graph_first(), std::uint32_t{ 21 }, "graph/pan-held");
		check_eq(reader.available_last(), std::uint32_t{ 60 }, "graph/pan-available");

		// 왼쪽 끝을 넘지 않는다. 선택도 창이 실제로 움직인 만큼만 따라간다.
		reader.pan_graph(-100000);
		check_eq(reader.graph_first(), reader.available_first(), "graph/clamp-left");
		check_eq(reader.selected_first(), std::uint32_t{ 10 }, "graph/selection-clamped-with-window");

		// 오른쪽 끝에 닿으면 따라가기가 다시 켜진다.
		reader.pan_graph(100000);
		check_eq(reader.graph_last(), std::uint32_t{ 60 }, "graph/clamp-right");
		check(reader.live_follow(), "graph/pan-to-newest-resumes");

		// 보존보다 넓게 달라고 해도 보존 전체까지다.
		reader.set_graph_span(100000);
		check_eq(reader.graph_count(), reader.available_last() - reader.available_first() + 1,
		         "graph/span-clamped-to-available");
		check_eq(reader.graph_first(), reader.available_first(), "graph/span-clamped-first");

		// 놓으면 창도 초기화된다.
		reader.set_graph_span(10);
		reader.reset();
		reader.adopt(service.capture());
		check_eq(reader.graph_count(), std::uint32_t{ 60 }, "graph/reset-clears-window");
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

		// ★ 20 프레임이다. 여섯으로는 **창을 좁힐 수가 없다** —
		//   kMinimumGraphFrames 가 8 이라 set_graph_span(3) 이 보존 전체로
		//   되돌아가고, 그러면 "창이 좁아지면 집계도 좁아진다" 를 무는 변이가
		//   조용히 통과한다.
		for (std::uint32_t frame = 1; frame <= 20; ++frame)
		{
			ce::profile_scope scope{ service, ce::marker<"ViewTick">() };
			busy_ticks(3);
			service.publish_frame(frame);
		}
		service.pause();

		ce::capture_reader reader;
		reader.adopt(service.capture());
		reader.set_live_follow(false);
		// ★ 선택을 창보다 좁게 둔다. 둘이 같으면 "시야가 선택이 아니라 창을
		//   따른다" 를 자극할 수가 없다.
		reader.select_range(1, 6);

		// ★ 기준은 **보이는 창**이다. 선택(1..6)으로 재면 안 된다 — 위 자극은
		//   스코프를 publish_frame 뒤에 닫으므로 19 틱짜리 프레임 7 이 하나 더
		//   생기고, 창은 그것까지 품는다.
		const ce::frame_aggregate& folded = reader.window_aggregate();
		const ce::profile_tick low = folded.tick_begin();
		const ce::profile_tick high = folded.tick_end();
		check(high > low, "timeline-view/span — 보이는 창에 길이가 있다");
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

		// ★ 시야의 기준은 선택이 아니라 **보이는 창**이다.
		//
		//   고른 한 프레임을 기준으로 삼으면, 위 그래프가 244 프레임을 보여
		//   주는 동안 아래 타임라인은 1.6 ms 짜리 한 칸만 그린다 — 같은 화면의
		//   두 그림이 서로 다른 범위를 말한다.
		reader.reset_view();
		{
			const ce::frame_aggregate& windowed = reader.window_aggregate();
			check_eq(windowed.frame_begin(), reader.graph_first(),
			         "window/first — 창 집계의 시작은 그래프 창의 시작이다");
			check_eq(windowed.frame_end(), reader.graph_last() + 1,
			         "window/last — 창 집계의 끝은 그래프 창의 끝이다");
			check_eq(reader.view_begin(), windowed.tick_begin(),
			         "timeline-view/spans-window — 시야는 창 전체로 선다");
			check_eq(reader.view_end(), windowed.tick_end(),
			         "timeline-view/spans-window-end — 시야는 창 전체로 선다");
		}

		// 선택을 한 프레임으로 좁혀도 시야는 그대로다. 여기서 되돌리면
		// 확대해 둔 것이 클릭 한 번에 풀린다.
		reader.zoom_view(0.25, pivot);
		const ce::profile_tick keptBegin = reader.view_begin();
		const ce::profile_tick keptSpan = reader.view_span();
		reader.select_frame(2);
		check_eq(reader.view_begin(), keptBegin,
		         "timeline-view/kept-on-select — 골라도 시야가 안 움직인다");
		check_eq(reader.view_span(), keptSpan,
		         "timeline-view/kept-on-select-span — 골라도 배율이 안 풀린다");

		// 창을 좁히면, 확대해 두지 않았을 때는 **따라간다.**
		reader.reset_view();
		reader.set_graph_span(3);
		{
			const ce::frame_aggregate& narrowed = reader.window_aggregate();
			check_eq(narrowed.frame_begin(), reader.graph_first(),
			         "window/follows-span — 창을 좁히면 집계도 좁아진다");
			check_eq(reader.view_begin(), narrowed.tick_begin(),
			         "timeline-view/follows-window — 확대 안 했으면 창을 따라간다");
			check_eq(reader.view_end(), narrowed.tick_end(),
			         "timeline-view/follows-window-end — 확대 안 했으면 창을 따라간다");
		}

		// 확대해 뒀으면 창이 미끄러져도 배율을 지킨다.
		reader.zoom_view(0.5, reader.view_begin() + reader.view_span() / 2);
		const ce::profile_tick heldSpan = reader.view_span();
		reader.pan_graph(-1);
		check_eq(reader.view_span(), heldSpan,
		         "timeline-view/zoom-held-on-pan — 굴려도 배율이 안 풀린다");

		reader.reset_view();
		check_eq(reader.view_begin(), reader.window_aggregate().tick_begin(),
		         "timeline-view/reset — 손으로도 되돌린다");
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
			service.submit_gpu_span(ce::marker<"GpuPass">(), base + 10, base + 20, 2, {});
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

//-----------------------------------------------------------------------------
// §7.3 의 트랙 순서. 레인은 **등록 순서가 아니라 선언한 트랙**으로 선다.
//
// ★ 자극을 일부러 거꾸로 세운다. 슬롯 오름차순이면 정답과 정확히 반대가
//   되도록 등록해야, 두 순서가 우연히 같아 변이가 통과하는 일이 없다.
//   실제 에디터에서 워커 여덟의 등록 순서가 회차마다 갈리는 것이 이 축을
//   세운 이유다 — 슬롯은 회차를 타고, 트랙은 타지 않는다.
//
// ★ 레인의 스팬 경계도 함께 묻는다. 그 경계는 요약이 **이벤트와 같은
//   순서**일 때 걸어 둔 것이라, 순서를 바꾸는 자리와 짝이 맞지 않으면
//   아무것도 실패하지 않고 모든 레인이 비어 버린다.
//-----------------------------------------------------------------------------
void test_track_order()
{
	ce::profiler_service service;
	service.initialize({});

	// 슬롯 0 — 맨 아래로 가야 할 것을 맨 먼저 등록한다.
	service.register_thread("Script", ce::track_kind::script_thread);
	service.record(1);

	auto lane = [&service](const char* name, ce::track_kind kind, std::uint32_t order)
	{
		std::thread worker([&service, name, kind, order]()
		{
			service.register_thread(name, kind, order);
			{
				ce::profile_scope scope{ service, ce::marker<"LaneWork">() };
				busy_ticks(4);
			}
			service.unregister_thread();
		});
		worker.join();
	};

	lane("WorkerTwo", ce::track_kind::command_thread, 2);   // 슬롯 1
	lane("WorkerOne", ce::track_kind::command_thread, 1);   // 슬롯 2
	lane("Game",      ce::track_kind::game_thread,    0);   // 슬롯 3

	{
		ce::profile_scope scope{ service, ce::marker<"ScriptWork">() };
		busy_ticks(4);
	}

	// 슬롯 4 — GPU 레인은 submit 이 만든다.
	const ce::profile_tick base = ce::profiler_service::now();
	service.submit_gpu_span(ce::marker<"GpuPass">(), base + 10, base + 20, 1, {});
	service.publish_gpu_spans();

	service.publish_frame(1);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "track-order/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 1);
	const std::span<const ce::thread_summary> lanes = aggregate.threads();
	check_eq(lanes.size(), std::size_t{ 5 }, "track-order/lanes — 레인 다섯이 섰다");
	if (lanes.size() != 5) { service.shutdown(); return; }

	auto name_of = [&capture](std::uint16_t slot) -> std::string
	{
		for (const ce::thread_info& info : capture->threads())
		{
			if (info.slot == slot) return info.name;
		}
		return "?";
	};

	const char* expected[5] = { "Game", "WorkerOne", "WorkerTwo", "Script",
	                            ce::profiler_service::kGpuLaneName };
	for (std::size_t i = 0; i < 5; ++i)
	{
		const std::string actual = name_of(lanes[i].thread_slot);
		const std::string label = std::string("track-order/lane") + std::to_string(i) +
		                          " — " + expected[i] + " 자리 (관측 " + actual + ")";
		check(actual == expected[i], label.c_str());
	}

	// ★ 경계가 제 레인을 가리키는가. 순서를 바꾸는 자리와 경계를 적는 자리가
	//   어긋나면 여기서 모든 레인이 [0,0) 으로 빈다.
	const std::span<const ce::profile_event> spans = aggregate.spans();
	std::size_t covered = 0;
	std::size_t strayed = 0;
	for (const ce::thread_summary& summary : lanes)
	{
		const std::string label = std::string("track-order/range-") +
		                          name_of(summary.thread_slot) +
		                          " — 레인의 스팬 구간이 비지 않았다";
		check(summary.span_begin < summary.span_end, label.c_str());
		for (std::uint32_t i = summary.span_begin; i < summary.span_end && i < spans.size(); ++i)
		{
			++covered;
			if (spans[i].thread_slot != summary.thread_slot) ++strayed;
		}
	}
	check_eq(strayed, std::size_t{ 0 },
	         "track-order/range-owner — 레인 구간에 남의 스팬이 없다");
	check_eq(covered, spans.size(),
	         "track-order/range-cover — 레인 구간들이 스팬 전부를 덮는다");

	service.shutdown();
}

//-----------------------------------------------------------------------------
// §6.4 개정 — 녹화 중에도 캡처가 공개된다.
//
// ★ 이 검사가 생긴 까닭. §6.4 는 "pause 시 capture 를 교체" 라고만 적었고,
//   그것을 글자대로 구현하자 **수집을 멈춰야만 프레임을 볼 수 있는** 도구가
//   됐다. 목표는 Unity 프로파일러다 — Record 가 도는 동안에도 프레임을
//   보여 준다(매뉴얼의 Current Frame 모드).
//
// ★ 그러면서도 **청하지 않으면 만들지 않는다.** 창이 닫혀 있는 동안에도
//   프레임마다 링을 통째로 복사하면, 그 비용은 프로파일러가 스스로 만들어
//   낸 것이라 어느 마커에도 안 잡힌다.
//-----------------------------------------------------------------------------
void test_live_capture_while_recording()
{
	ce::profiler_service service;
	ce::profiler_config config;
	config.live_capture_interval_ms = 0.0;   // 검사에서는 청하는 대로 낸다
	service.initialize(config);
	service.register_thread("Main", ce::track_kind::game_thread);
	service.record(1);

	// ① 아무도 안 보면 스냅샷이 없다.
	for (std::uint32_t frame = 1; frame <= 3; ++frame)
	{
		{
			ce::profile_scope scope{ service, ce::marker<"Work">() };
			busy_ticks(4);
		}
		service.publish_frame(frame);
	}
	check(!service.capture(), "live/unrequested — 청하지 않으면 스냅샷이 없다");

	// ② 청하면 다음 프레임 경계에서 선다.
	service.request_live_capture();
	{
		ce::profile_scope scope{ service, ce::marker<"Work">() };
		busy_ticks(4);
	}
	service.publish_frame(4);

	const ce::capture_session_ptr live = service.capture();
	check(static_cast<bool>(live), "live/published — 녹화 중에 캡처가 선다");

	// ③ 공개했다고 녹화가 멈추지 않는다. 이것이 Pause 와 갈리는 자리다.
	check(service.state() == ce::recorder_state::recording,
	      "live/still-recording — 공개해도 녹화가 멈추지 않는다");

	if (live)
	{
		check_eq(live->frame_count(), std::uint32_t{ 4 },
		         "live/frames — 닫힌 프레임이 모두 담긴다");
		check(live->complete(), "live/complete — 닫힌 프레임만 담으므로 온전하다");

		// 이 스냅샷으로도 접힌다. 얼린 것만 접을 수 있으면 "보려면 멈춰라" 가
		// 코어에 남아 있는 것이다.
		const ce::frame_aggregate aggregate = ce::aggregate_frames(*live, 1, 4);
		check(aggregate.hierarchy().size() > 0,
		      "live/foldable — 녹화 중 스냅샷도 접힌다");
	}

	// ④ 다시 청하면 그 뒤의 프레임까지 담긴 **새** 스냅샷이 온다.
	service.request_live_capture();
	{
		ce::profile_scope scope{ service, ce::marker<"Work">() };
		busy_ticks(4);
	}
	service.publish_frame(5);

	const ce::capture_session_ptr next = service.capture();
	check(next.get() != live.get(), "live/refreshed — 다시 청하면 새 스냅샷이다");
	if (next)
	{
		check_eq(next->frame_count(), std::uint32_t{ 5 },
		         "live/grows — 새 스냅샷에 그 뒤 프레임이 들어 있다");
	}

	// ⑤ 앞서 받은 것은 **변하지 않는다.** 읽는 쪽이 손에 쥔 자료가 뒤에서
	//    바뀌면 접은 결과와 화면이 어긋난다.
	if (live)
	{
		check_eq(live->frame_count(), std::uint32_t{ 4 },
		         "live/immutable — 먼저 받은 스냅샷은 그대로다");
	}

	service.shutdown();
}

//-----------------------------------------------------------------------------
// §7.4 의 Min·P95·Frames.
//
// ★ 길이를 정확히 아는 자극이 필요하다. busy_ticks 는 회차마다 길이가 달라
//   p95 의 **값**을 물을 수가 없고, 물을 수 없으면 "커 보이니 맞겠지" 로
//   끝난다. GPU 구간은 tick 을 직접 주므로 분포를 지어낼 수 있다.
//-----------------------------------------------------------------------------
void test_call_distribution()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	const ce::profile_tick base = ce::profiler_service::now();
	const ce::gpu_span_context origin;

	// 프레임 1..4 에 다섯씩 스무 개, 길이 1..20.
	std::uint32_t length = 0;
	for (std::uint32_t frame = 1; frame <= 5; ++frame)
	{
		if (frame >= 2)
		{
			for (int i = 0; i < 5; ++i)
			{
				++length;
				const ce::profile_tick begin = base + length * 100;
				service.submit_gpu_span(ce::marker<"Pass">(), begin, begin + length,
				                        frame - 1, origin);
			}
			service.publish_gpu_spans();
		}
		service.publish_frame(frame);
	}

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "distribution/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 4);

	const ce::aggregate_row* row = nullptr;
	for (const ce::aggregate_row& candidate : aggregate.hierarchy())
	{
		if (candidate.marker == ce::marker<"Pass">()) row = &candidate;
	}
	check(nullptr != row, "distribution/row — Pass 가 표에 있다");
	if (nullptr != row)
	{
		check_eq<unsigned long long>(row->call_count, 20ull,
		                             "distribution/calls — 스무 번 불렸다");
		check_eq<ce::profile_tick>(row->min_ticks, 1u,
		                           "distribution/min — 가장 짧은 호출이 1");
		check_eq<ce::profile_tick>(row->max_ticks, 20u,
		                           "distribution/max — 가장 긴 호출이 20");

		// nearest-rank: ceil(0.95 × 20) = 19 → 오름차순 19 번째 = 19.
		// 보간하면 19.05 가 나오는데, 그만큼 걸린 호출은 하나도 없다.
		check_eq<ce::profile_tick>(row->p95_ticks, 19u,
		                           "distribution/p95 — 95 백분위가 19");
		check_eq<std::uint32_t>(row->frame_appearances, 4u,
		                        "distribution/frames — 네 프레임에 나타났다");
	}

	// ★ 프레임 수는 호출 수와 **다른 수**여야 한다. 둘이 같아지는 구현
	//   (중복을 안 지움)이 가장 흔한 실수라, 다르다는 것 자체를 문다.
	if (nullptr != row)
	{
		check(row->frame_appearances != static_cast<std::uint32_t>(row->call_count),
		      "distribution/frames-not-calls — 프레임 수가 호출 수와 다르다");
	}

	service.shutdown();
}

//-----------------------------------------------------------------------------
// Flat 의 분포는 부분을 합쳐서 만들 수 없다.
//
// ★ 같은 marker 가 한 프레임 안에서 두 부모 밑으로 불리면 hierarchy 행은
//   둘인데 **프레임은 하나**다. 행마다의 프레임 수를 더하면 2 가 되고, 그
//   수는 프레임 수보다 커진다 — 아무것도 실패하지 않은 채로.
//-----------------------------------------------------------------------------
void test_flat_distribution_union()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main", ce::track_kind::game_thread);
	service.record(1);

	for (std::uint32_t frame = 1; frame <= 3; ++frame)
	{
		{
			ce::profile_scope left{ service, ce::marker<"Left">() };
			ce::profile_scope shared{ service, ce::marker<"Shared">() };
			busy_ticks(2);
		}
		{
			ce::profile_scope right{ service, ce::marker<"Right">() };
			ce::profile_scope shared{ service, ce::marker<"Shared">() };
			busy_ticks(2);
		}
		service.publish_frame(frame);
	}

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "flat-union/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 3);

	std::size_t hierarchyRows = 0;
	for (const ce::aggregate_row& node : aggregate.hierarchy())
	{
		if (node.marker != ce::marker<"Shared">()) continue;
		++hierarchyRows;
		check_eq<std::uint32_t>(node.frame_appearances, 3u,
		                        "flat-union/branch-frames — 갈래마다 세 프레임");
	}
	check_eq(hierarchyRows, std::size_t{ 2 },
	         "flat-union/branches — 부모가 둘이라 행도 둘이다");

	const ce::aggregate_row* flat = nullptr;
	for (const ce::aggregate_row& node : aggregate.flat())
	{
		if (node.marker == ce::marker<"Shared">()) flat = &node;
	}
	check(nullptr != flat, "flat-union/row — Flat 에 Shared 가 한 줄이다");
	if (nullptr != flat)
	{
		check_eq<unsigned long long>(flat->call_count, 6ull,
		                             "flat-union/calls — 호출은 여섯이다");
		check_eq<std::uint32_t>(flat->frame_appearances, 3u,
		                        "flat-union/frames — 프레임은 셋이다(더하면 6)");
		check(flat->min_ticks <= flat->p95_ticks,
		      "flat-union/order — min 이 p95 를 넘지 않는다");
		check(flat->p95_ticks <= flat->max_ticks,
		      "flat-union/order-max — p95 가 max 를 넘지 않는다");
	}

	service.shutdown();
}

//-----------------------------------------------------------------------------
// §7.3 트랙 1 — 프레임 경계.
//
// ★ 집계가 들고 있던 tick_begin/tick_end 는 **범위 전체**의 양 끝이라 프레임이
//   어디서 갈리는지는 말하지 못한다. 경계를 따로 들지 않으면 그리는 층이 캡처를
//   다시 훑게 되고, 접기가 두 곳에 생긴다.
//-----------------------------------------------------------------------------
void test_frame_boundaries()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main", ce::track_kind::game_thread);
	service.record(1);

	for (std::uint32_t frame = 1; frame <= 4; ++frame)
	{
		{
			ce::profile_scope scope{ service, ce::marker<"FrameWork">() };
			busy_ticks(4);
		}
		service.publish_frame(frame);
	}

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "boundary/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 4);
	const std::span<const ce::frame_boundary> bounds = aggregate.boundaries();
	check_eq(bounds.size(), std::size_t{ 4 }, "boundary/count — 프레임 넷의 경계가 넷이다");
	if (bounds.size() != 4) { service.shutdown(); return; }

	// 엔진 프레임 오름차순이고, 경계가 겹치지 않고, 범위의 양 끝과 맞는다.
	bool ascending = true;
	bool ordered = true;
	for (std::size_t i = 0; i < bounds.size(); ++i)
	{
		if (bounds[i].tick_end < bounds[i].tick_begin) ordered = false;
		if (i > 0 && bounds[i].engine_frame <= bounds[i - 1].engine_frame) ascending = false;
		if (i > 0 && bounds[i].tick_begin < bounds[i - 1].tick_begin) ordered = false;
	}
	check(ascending, "boundary/ascending — 엔진 프레임 오름차순이다");
	check(ordered, "boundary/monotonic — 경계가 시간 순으로 선다");
	check_eq<std::uint32_t>(bounds.front().engine_frame, 1u, "boundary/first — 첫 칸이 프레임 1");
	check_eq<std::uint32_t>(bounds.back().engine_frame, 4u, "boundary/last — 마지막 칸이 프레임 4");

	// ★ 범위의 양 끝은 경계들의 양 끝과 **같아야** 한다. 다르면 둘 중 하나가
	//   다른 자료를 보고 있다는 뜻이다.
	check_eq(aggregate.tick_begin(), bounds.front().tick_begin,
	         "boundary/range-begin — 범위 시작이 첫 경계와 같다");
	check_eq(aggregate.tick_end(), bounds.back().tick_end,
	         "boundary/range-end — 범위 끝이 마지막 경계와 같다");

	// 고른 범위 밖의 프레임은 경계에도 없다.
	const ce::frame_aggregate two = ce::aggregate_frames(*capture, 2, 3);
	check_eq(two.boundaries().size(), std::size_t{ 2 },
	         "boundary/selected — 고른 범위만큼만 경계가 선다");

	service.shutdown();
}

//-----------------------------------------------------------------------------
// §7.3 트랙 1 — 길이가 없는 사건.
//
// ★ 이것이 트리에 들어가면 깊이 0 짜리 점이 스택에서 부모를 밀어내고, 그
//   뒤의 자식들이 **부모를 잃은 채 루트로 올라온다.** 길이가 0 이라 합계는
//   하나도 안 어긋나므로 수치로는 잡히지 않는다 — 부모가 누구인지를 물어야
//   잡힌다.
//-----------------------------------------------------------------------------
void test_instant_events()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main", ce::track_kind::game_thread);
	service.record(1);

	{
		ce::profile_scope outer{ service, ce::marker<"Outer">() };
		busy_ticks(4);

		// 바깥 구간 **한가운데**에서 찍는다. 스코프 밖에서 찍으면 스택이
		// 비어 있어 부모를 밀어낼 일 자체가 없고, 그러면 아래 단정이
		// 일어나지도 않은 사고를 통과시킨다.
		service.mark_instant(ce::marker<"SceneActivated">());

		{
			ce::profile_scope inner{ service, ce::marker<"Inner">() };
			busy_ticks(4);
		}
	}

	service.publish_frame(1);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "instant/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	const ce::frame_aggregate aggregate = ce::aggregate_frames(*capture, 1, 1);

	// ① 사건이 자기 목록에 있고, 길이가 0 이고, 표식을 달고 있다.
	const std::span<const ce::profile_event> instants = aggregate.instants();
	check_eq(instants.size(), std::size_t{ 1 }, "instant/present — 사건 하나가 담겼다");
	if (instants.size() == 1)
	{
		check(instants[0].marker == ce::marker<"SceneActivated">(),
		      "instant/marker — 찍은 이름 그대로다");
		check_eq(instants[0].tick_end, instants[0].tick_begin,
		         "instant/zero-length — 길이가 0 이다");
		check(ce::has_flag(instants[0].flags, ce::event_flags::instant),
		      "instant/flag — 표식이 달려 있다");
	}

	// ② 트리에는 없다.
	std::size_t inTree = 0;
	for (const ce::aggregate_row& row : aggregate.hierarchy())
	{
		if (row.marker == ce::marker<"SceneActivated">()) ++inTree;
	}
	check_eq(inTree, std::size_t{ 0 }, "instant/not-in-tree — 트리에 들어가지 않는다");

	std::size_t inFlat = 0;
	for (const ce::aggregate_row& row : aggregate.flat())
	{
		if (row.marker == ce::marker<"SceneActivated">()) ++inFlat;
	}
	check_eq(inFlat, std::size_t{ 0 }, "instant/not-in-flat — Flat 에도 들어가지 않는다");

	// ③ ★ 사건 **뒤에 열린 구간이 부모를 지킨다.** 이것이 이 검사의 핵심이다.
	std::uint32_t outerRow = 0;
	bool outerFound = false;
	for (std::uint32_t i = 0; i < aggregate.hierarchy().size(); ++i)
	{
		if (aggregate.hierarchy()[i].marker != ce::marker<"Outer">()) continue;
		outerRow = i;
		outerFound = true;
	}
	check(outerFound, "instant/outer — 바깥 구간이 표에 있다");
	if (outerFound)
	{
		const ce::aggregate_row& outer = aggregate.hierarchy()[outerRow];
		bool innerIsChild = false;
		for (std::uint32_t i = outer.child_begin;
		     i < outer.child_end && i < aggregate.hierarchy().size(); ++i)
		{
			if (aggregate.hierarchy()[i].marker == ce::marker<"Inner">()) innerIsChild = true;
		}
		check(innerIsChild,
		      "instant/keeps-parent — 사건 뒤에 열린 구간이 부모를 지킨다");
	}

	// ④ 두 합의 동치가 그대로다. 사건을 한쪽에서만 빼면 여기가 깨진다.
	check_eq(aggregate.timeline_total_ticks(), aggregate.hierarchy_total_ticks(),
	         "instant/totals — 사건이 섞여도 두 합이 같다");

	service.shutdown();
}

//-----------------------------------------------------------------------------
// GPU bar 의 tooltip 이 싣는 귀속(§7.3): 제출 번호 · 뷰 · 큐.
//
// ★ 이것이 없으면 같은 프레임의 씬뷰와 게임뷰 제출이 **이름만 같은 두 줄**로
//   보인다. §0.5.10 에서 수집의 83% 가 남의 제출을 읽고 있었는데도 오래
//   안 보였던 이유가 그 구분의 부재였다.
//-----------------------------------------------------------------------------
void test_gpu_span_origin()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	ce::gpu_span_context origin;
	origin.submission = 4242;
	origin.view = 7;
	origin.queue = 2;

	const ce::profile_tick base = ce::profiler_service::now();
	for (std::uint32_t frame = 1; frame <= 3; ++frame)
	{
		if (frame == 3)
		{
			service.submit_gpu_span(ce::marker<"GpuPass">(), base + 10, base + 20, 2, origin);
			service.publish_gpu_spans();
		}
		service.publish_frame(frame);
	}

	service.pause();
	const ce::capture_session_ptr capture = service.capture();
	check(static_cast<bool>(capture), "gpu-origin/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	std::size_t seen = 0;
	for (const ce::frame_record& frame : capture->frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (!ce::has_flag(event.flags, ce::event_flags::gpu_span)) continue;
			++seen;
			check_eq<std::uint32_t>(event.submission, 4242u,
			                        "gpu-origin/submission — 제출 번호가 그대로 남는다");
			check_eq<unsigned>(event.view, 7u, "gpu-origin/view — 뷰가 그대로 남는다");
			check_eq<unsigned>(event.queue, 2u, "gpu-origin/queue — 큐가 그대로 남는다");
		}
	}
	check_eq(seen, std::size_t{ 1 }, "gpu-origin/present — GPU 구간이 하나 담겼다");

	// CPU 스코프는 이 칸을 쓰지 않는다. 0 이 아니면 남의 값이 새어 든 것이다.
	std::size_t cpuDirty = 0;
	for (const ce::frame_record& frame : capture->frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (ce::has_flag(event.flags, ce::event_flags::gpu_span)) continue;
			if (0 != event.submission || 0 != event.view || 0 != event.queue) ++cpuDirty;
		}
	}
	check_eq(cpuDirty, std::size_t{ 0 },
	         "gpu-origin/cpu-clean — CPU 스코프의 GPU 칸은 0 이다");

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
	service.submit_gpu_span(ce::marker<"GpuAhead">(), base + 1, base + 2, 3, {});
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
	service.submit_gpu_span(ce::marker<"GpuGone">(), base + 1, base + 2, 1, {});
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

	// ★ 그리고 그 경계는 **결정적으로** 서 있어야 한다. 위의 단정은 경합이
	//   실제로 겹쳤을 때만 붉어지므로, 같은 코드가 어떤 실행에서는 초록이다.
	//   소유 위반은 겹치든 말든 세어지므로 회차에 기대지 않는다.
	check_eq(summary.foreign_stream_touches, std::uint64_t{ 0 },
	         "concurrent-publish/owned — 남의 스트림을 만진 호출이 없다");
	check_eq(summary.unbalanced_scopes, std::uint64_t{ 0 },
	         "concurrent-publish/balanced — 불균형 스코프 0");

	service.shutdown();
}


// ── 감사가 재현한 넷 ─────────────────────────────────────────────────
//
// 외부 감사가 사본에서 재현한 것들이다. 자극을 먼저 세우고 고친다 — 재현되지
// 않는 지적은 고쳤는지 알 수 없다.

// 이벤트가 담긴 프레임 칸의 번호. 없으면 0xFFFFFFFF.
std::uint32_t frame_holding(const ce::capture_session& capture, ce::marker_id id)
{
	for (const ce::frame_record& frame : capture.frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (event.marker == id) return frame.engine_frame;
		}
	}
	return 0xFFFFFFFFu;
}

const ce::profile_event* find_event(const ce::capture_session& capture, ce::marker_id id)
{
	for (const ce::frame_record& frame : capture.frames())
	{
		for (const ce::profile_event& event : frame.events)
		{
			if (event.marker == id) return &event;
		}
	}
	return nullptr;
}

// ① Pause 시점에 열려 있던 구간은 잘린 채로 남아야 한다.
//
// ★ 프레임 경계에서 열린 구간을 닫지 않는 것은 **의도**다(프레임을 넘는 구간).
//   하지만 pause 에는 다음 프레임이 없다 — 얼린 캡처가 그 구간의 마지막 기회다.
void test_pause_truncates_open_scope()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	service.begin_scope(ce::marker<"OpenAtPause">());
	service.publish_frame(1);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "pause-open/capture — 얼린 캡처가 있다");
	if (!capture) { service.end_scope(); service.shutdown(); return; }

	const ce::profile_event* event =
		find_event(*capture, ce::marker<"OpenAtPause">());
	check(event != nullptr, "pause-open/present — 열려 있던 구간이 남는다");
	if (event)
	{
		check(ce::has_flag(event->flags, ce::event_flags::truncated_end),
		      "pause-open/truncated — 끝을 못 봤다고 표시된다");
	}

	// 짝을 맞춘다. 여기서 또 기록되면 같은 구간이 두 번 남는다.
	service.end_scope();
	service.record(2);
	service.publish_frame(2);
	service.pause();

	const ce::capture_session_ptr second = service.capture();
	if (second)
	{
		std::size_t total = 0;
		for (const ce::frame_record& frame : second->frames())
		{
			for (const ce::profile_event& e : frame.events)
			{
				if (e.marker == ce::marker<"OpenAtPause">()) ++total;
			}
		}
		check_eq<std::size_t>(total, 1, "pause-open/once — 같은 구간이 두 번 남지 않는다");
	}

	service.shutdown();
}

// ② 늦게 온 CPU 구간은 **끝난 시각이 속한 프레임**에 담겨야 한다.
//
// ★ writer 가 자기 일정으로 봉인하게 되면서 생긴 노출이다(§0.5.15). 워커가
//   한 번 적고 잠들면 그 청크는 워커가 깨어날 때까지 오지 않는다.
void test_late_cpu_attribution()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	std::atomic<int> step{ 0 };
	std::thread worker([&service, &step]()
	{
		service.register_thread("LateWorker");
		{ ce::profile_scope scope{ service, ce::marker<"LateCpu">() }; }
		step.store(1, std::memory_order_release);

		// 주인이 프레임을 여러 번 닫는 동안 **아무것도 적지 않는다.**
		while (step.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }

		// 이제 한 번 더 적어 옛 청크가 봉인되게 한다.
		{ ce::profile_scope scope{ service, ce::marker<"LateCpuTail">() }; }
		step.store(3, std::memory_order_release);

		while (step.load(std::memory_order_acquire) < 4) { std::this_thread::yield(); }
		service.unregister_thread();
	});

	while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
	service.publish_frame(1);
	service.publish_frame(2);
	service.publish_frame(3);
	step.store(2, std::memory_order_release);
	while (step.load(std::memory_order_acquire) < 3) { std::this_thread::yield(); }
	service.publish_frame(4);
	step.store(4, std::memory_order_release);
	worker.join();
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "late-cpu/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	check_eq(frame_holding(*capture, ce::marker<"LateCpu">()), std::uint32_t{ 1 },
	         "late-cpu/frame — 끝난 시각이 속한 프레임 칸에 담긴다");

	service.shutdown();
}

// ③ Clear 이전 세대의 청크가 새 링으로 재유입되면 안 된다.
//
// ★ **녹화를 멈추지 않고** 지운다. 이것이 세대 검사가 필요한 유일한 자리다 —
//   멈췄다 다시 켜면 새 프레임이 전부 Clear 뒤에 열리므로 시각만 봐도 옛
//   이벤트가 갈 칸이 없다. 하지만 녹화 중에 지우면 **지금 열려 있는 프레임의
//   시작 시각은 Clear 보다 앞**이고, 그 칸이 옛 이벤트를 그대로 받아 준다.
void test_clear_generation()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);
	service.publish_frame(1);

	std::atomic<int> step{ 0 };
	std::thread worker([&service, &step]()
	{
		service.register_thread("ClearWorker");

		while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
		{ ce::profile_scope scope{ service, ce::marker<"PreClear">() }; }
		step.store(2, std::memory_order_release);

		// 여기서 잔다. 이 청크는 아직 봉인되지 않았다.
		while (step.load(std::memory_order_acquire) < 3) { std::this_thread::yield(); }

		// 깨어나 한 번 더 적으면 옛 청크가 봉인돼 수집기로 간다.
		{ ce::profile_scope scope{ service, ce::marker<"PostClear">() }; }
		step.store(4, std::memory_order_release);

		while (step.load(std::memory_order_acquire) < 5) { std::this_thread::yield(); }
		service.unregister_thread();
	});

	step.store(1, std::memory_order_release);
	while (step.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }

	// 녹화를 멈추지 않고 지운다.
	service.clear();

	step.store(3, std::memory_order_release);
	while (step.load(std::memory_order_acquire) < 4) { std::this_thread::yield(); }
	step.store(5, std::memory_order_release);
	worker.join();

	service.publish_frame(10);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "clear-generation/capture — 얼린 캡처가 있다");
	if (!capture) { service.shutdown(); return; }

	check_eq(count_marker(*capture, ce::marker<"PreClear">()), std::size_t{ 0 },
	         "clear-generation/dropped — 지운 세대의 이벤트가 돌아오지 않는다");
	check(count_marker(*capture, ce::marker<"PostClear">()) > 0,
	      "clear-generation/kept — 새 세대의 이벤트는 들어온다");

	service.shutdown();
}

// ④ pause 의 얼림 요청은 **남의 스레드**에서도 열린 구간을 잘라야 한다.
//
// ★ ①은 pause 를 부른 스레드 자신의 스트림만 자극한다. 그쪽은 pause 가 직접
//   자르므로, honor_seal_request 의 얼림 가지를 걷어도 ①은 초록이다 —
//   두 층이 같은 절을 막으면 단정이 눈먼다. 그래서 계속 적고 있는 워커를
//   따로 세운다.
void test_pause_truncates_worker_scope()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	std::atomic<int> step{ 0 };
	std::thread worker([&service, &step]()
	{
		service.register_thread("BusyWorker");

		// 바깥 구간을 열어 둔 채 계속 적는다. write() 마다 얼림 요청을 본다.
		service.begin_scope(ce::marker<"WorkerOuter">());
		step.store(1, std::memory_order_release);

		while (step.load(std::memory_order_acquire) < 2)
		{
			ce::profile_scope inner{ service, ce::marker<"WorkerTick">() };
			busy_ticks(1);
		}

		service.end_scope();
		step.store(3, std::memory_order_release);

		while (step.load(std::memory_order_acquire) < 4) { std::this_thread::yield(); }
		service.unregister_thread();
	});

	while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
	service.publish_frame(1);
	service.pause();

	const ce::live_summary summary = service.summary();
	check_eq(summary.pause_unacked_streams, std::uint32_t{ 0 },
	         "pause-worker/acked — 계속 적는 워커는 얼림에 응답한다");

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "pause-worker/capture — 얼린 캡처가 있다");
	if (capture)
	{
		const ce::profile_event* event =
			find_event(*capture, ce::marker<"WorkerOuter">());
		check(event != nullptr, "pause-worker/present — 워커의 열린 구간이 남는다");
		if (event)
		{
			check(ce::has_flag(event->flags, ce::event_flags::truncated_end),
			      "pause-worker/truncated — 끝을 못 봤다고 표시된다");
		}
	}

	step.store(2, std::memory_order_release);
	while (step.load(std::memory_order_acquire) < 3) { std::this_thread::yield(); }
	step.store(4, std::memory_order_release);
	worker.join();
	service.shutdown();
}



// ── 경계 넷 (3차 감사) ───────────────────────────────────────────────
//
// 세대와 짝 맞춤이 **청크 단위**로만 서 있어서 생긴 것들이다. 세대는 청크에
// 찍히는데 스코프는 청크보다 오래 살고, 짝 예약은 개수만 세는데 잘린 구간의
// 짝은 가장 **바깥**이라 다음에 오는 종료와 순서가 뒤집힌다.

// ⑤ Clear 뒤에 적은 것은 새 캡처에 남아야 한다.
void test_clear_keeps_new_events()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	{ ce::profile_scope scope{ service, ce::marker<"BeforeClear">() }; }
	service.clear();
	{ ce::profile_scope scope{ service, ce::marker<"AfterClear">() }; }

	service.publish_frame(2);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "clear-new/capture — 얼린 캡처가 있다");
	if (capture)
	{
		check_eq(count_marker(*capture, ce::marker<"AfterClear">()), std::size_t{ 1 },
		         "clear-new/kept — Clear 뒤에 적은 것은 남는다");
		check_eq(count_marker(*capture, ce::marker<"BeforeClear">()), std::size_t{ 0 },
		         "clear-new/dropped — Clear 앞의 것은 남지 않는다");
	}

	service.shutdown();
}

// ⑥ Clear **전에 열린** 스코프는 나중에 닫혀도 새 캡처에 들어오면 안 된다.
//
// ★ 세대를 청크에만 찍으면 이 구간을 못 막는다. 스코프는 청크보다 오래 산다.
void test_clear_drops_scope_opened_before()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	service.begin_scope(ce::marker<"OpenedBeforeClear">());
	service.clear();
	service.end_scope();

	service.publish_frame(2);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "clear-open/capture — 얼린 캡처가 있다");
	if (capture)
	{
		check_eq(count_marker(*capture, ce::marker<"OpenedBeforeClear">()), std::size_t{ 0 },
		         "clear-open/dropped — 지운 세대에서 연 구간은 돌아오지 않는다");
	}

	service.shutdown();
}

// ⑦ 다시 녹화한 뒤의 스코프는 **제 짝**으로 닫혀야 한다.
//
// ★ 잘린 구간의 짝을 개수로만 예약하면, 그 예약을 **새 스코프의 종료**가 먼저
//   먹는다. 그러면 새 구간은 열린 채 남아 다음 pause 에서 잘린 것으로 기록된다.
void test_scope_pairing_after_resume()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	service.begin_scope(ce::marker<"OuterAcrossPause">());
	service.publish_frame(1);
	service.pause();

	service.record(2);
	{
		ce::profile_scope scope{ service, ce::marker<"AfterResume">() };
		busy_ticks(1);
	}

	// 닫힌 시각을 여기서 못 박는다. 짝이 어긋나면 이 구간은 **아래의**
	// end_scope 까지 열린 채 끌려가므로 끝 시각이 이 표식을 넘는다.
	const ce::profile_tick closed = ce::profiler_service::now();
	busy_ticks(400);

	service.end_scope();   // 잘린 바깥 구간의 진짜 짝

	service.publish_frame(2);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "resume-pair/capture — 얼린 캡처가 있다");
	if (capture)
	{
		const ce::profile_event* event =
			find_event(*capture, ce::marker<"AfterResume">());
		check(event != nullptr, "resume-pair/present — 다시 녹화한 뒤의 구간이 남는다");
		if (event)
		{
			check(!ce::has_flag(event->flags, ce::event_flags::truncated_end),
			      "resume-pair/complete — 제대로 닫힌 구간이 잘린 것으로 기록되지 않는다");
			check(event->tick_end <= closed,
			      "resume-pair/closed-on-time — 제 짝이 닫았지 남의 종료가 끌고 가지 않았다");
		}
	}

	service.shutdown();
}

// ⑧ 얼림에 응답했다면 그 스트림의 것이 **이미 전달돼 있어야** 한다.
//
// ★ ack 는 "봉인까지 끝났다" 는 뜻이어야 한다. 자르는 도중 write() 가 다시
//   들어와 ack 를 먼저 올리면, 아직 청크에 없는 것을 두고 수집기가 거둬 간다.
//   한 번으로는 잡히지 않는 경합이라 여러 번 돌린다.
void test_pause_ack_implies_delivery()
{
	for (int round = 0; round < 24; ++round)
	{
		ce::profiler_service service;
		service.initialize({});
		service.register_thread("Main");
		service.record(1);

		std::atomic<int> step{ 0 };
		std::thread worker([&service, &step]()
		{
			service.register_thread("AckWorker");
			service.begin_scope(ce::marker<"AckOuter">());
			step.store(1, std::memory_order_release);

			while (step.load(std::memory_order_acquire) < 2)
			{
				ce::profile_scope inner{ service, ce::marker<"AckTick">() };
				busy_ticks(1);
			}

			service.end_scope();
			step.store(3, std::memory_order_release);
			while (step.load(std::memory_order_acquire) < 4) { std::this_thread::yield(); }
			service.unregister_thread();
		});

		while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
		service.publish_frame(1);
		service.pause();

		const ce::live_summary summary = service.summary();
		const ce::capture_session_ptr capture = service.capture();
		const bool delivered =
			(capture != nullptr) &&
			(count_marker(*capture, ce::marker<"AckOuter">()) == 1);

		if (0 == summary.pause_unacked_streams && !delivered)
		{
			check(false, "ack-delivery/sealed — 응답했으면 그 구간이 이미 전달돼 있다");
		}

		step.store(2, std::memory_order_release);
		while (step.load(std::memory_order_acquire) < 3) { std::this_thread::yield(); }
		step.store(4, std::memory_order_release);
		worker.join();
		service.shutdown();
	}

	check(true, "ack-delivery/rounds — 24 회 모두 응답과 전달이 함께 간다");
}


// ⑨ 얼린 캡처는 **온전한지 아닌지**를 스스로 말해야 한다.
//
// ★ 잠든 워커는 봉인 요청에 응답하지 못한다. 그 꼬리가 캡처에 없는데도
//   frozen 으로 조용히 끝나면, 읽는 쪽은 "그 스레드가 조용했다" 와 "못 받았다"
//   를 구분할 수 없다 — 빈 집합을 성공으로 읽는 바로 그 양식이다.
void test_capture_reports_incompleteness()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	std::atomic<int> step{ 0 };
	std::thread worker([&service, &step]()
	{
		service.register_thread("SleepingWorker");
		{ ce::profile_scope scope{ service, ce::marker<"SleepTick">() }; }
		step.store(1, std::memory_order_release);

		// 여기서 잔다. 봉인 요청이 와도 들어줄 자리를 지나지 않는다.
		while (step.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }
		service.unregister_thread();
	});

	while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
	service.publish_frame(1);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "incomplete/capture — 얼린 캡처가 있다");
	if (capture)
	{
		check(!capture->complete(),
		      "incomplete/flag — 못 받은 꼬리가 있으면 온전하지 않다고 말한다");
		check(capture->unacked_streams() > 0,
		      "incomplete/count — 몇 스트림을 못 받았는지 센다");
	}

	step.store(2, std::memory_order_release);
	worker.join();
	service.shutdown();
}

// ⑩ 다 응답했으면 온전하다고 말해야 한다. ⑨ 의 반대쪽이 없으면 "언제나
//    온전하지 않다" 도 초록으로 지나간다.
void test_capture_reports_completeness()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	{ ce::profile_scope scope{ service, ce::marker<"LoneTick">() }; }
	service.publish_frame(1);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "complete/capture — 얼린 캡처가 있다");
	if (capture)
	{
		check(capture->complete(), "complete/flag — 다 응답했으면 온전하다");
		check_eq(capture->unacked_streams(), std::uint32_t{ 0 },
		         "complete/count — 못 받은 스트림이 없다");
	}

	service.shutdown();
}


// ⑪ 종료는 **주인만** 자기 스트림을 닫는다.
//
// ★ 주인이 살아 있는 스트림을 종료 스레드가 finish() 하면, 주인이 쓰고 있는
//   저장소를 만진다 — `collector-seals-others` 가 죽던 것과 같은 길이다.
//   닫지 못한 것은 세어 둬야 "그 스레드가 조용했다" 와 구분된다.
void test_shutdown_owns_only_its_stream()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");
	service.record(1);

	std::atomic<int> step{ 0 };
	std::thread worker([&service, &step]()
	{
		service.register_thread("NeverUnregisters");
		{ ce::profile_scope scope{ service, ce::marker<"ShutdownTick">() }; }
		step.store(1, std::memory_order_release);

		// 여기서 잔다. unregister_thread 를 부르지 않고 종료를 맞는다.
		while (step.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }
	});

	while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
	service.publish_frame(1);
	service.shutdown();

	const ce::live_summary summary = service.summary();
	check_eq(summary.abandoned_streams, std::uint64_t{ 1 },
	         "shutdown-own/abandoned — 주인이 살아 있어 닫지 못한 것을 센다");
	check_eq(summary.foreign_stream_touches, std::uint64_t{ 0 },
	         "shutdown-own/untouched — 남의 스트림을 만지지 않았다");

	step.store(2, std::memory_order_release);
	worker.join();
}

// ⑫ 종료 뒤 남은 thread_local 자리는 **없는 것으로** 읽혀야 한다.
//
// ★ shutdown 은 부른 스레드의 자리만 끊을 수 있다. 다른 스레드의 자리는 죽은
//   스트림을 계속 가리키므로, 같은 스레드가 다시 등록하러 오면 "이미 등록됨"
//   으로 돌아가 해제된 포인터를 그대로 쓴다.
void test_tls_slot_epoch()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("Main");

	std::atomic<int> step{ 0 };
	std::thread worker([&service, &step]()
	{
		service.register_thread("Recycled");
		{ ce::profile_scope scope{ service, ce::marker<"FirstLife">() }; }
		step.store(1, std::memory_order_release);

		// 종료와 재초기화를 기다린다. 자리는 그대로 남아 있다.
		while (step.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }

		// 같은 스레드가 다시 등록한다. 세대를 보지 않으면 죽은 스트림으로 간다.
		service.register_thread("SecondLife");
		{ ce::profile_scope scope{ service, ce::marker<"SecondLife">() }; }
		step.store(3, std::memory_order_release);

		while (step.load(std::memory_order_acquire) < 4) { std::this_thread::yield(); }
		service.unregister_thread();
	});

	service.record(1);
	while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
	service.publish_frame(1);
	service.shutdown();

	service.initialize({});
	service.register_thread("Main");
	service.record(10);
	step.store(2, std::memory_order_release);
	while (step.load(std::memory_order_acquire) < 3) { std::this_thread::yield(); }

	service.publish_frame(10);
	step.store(4, std::memory_order_release);
	worker.join();
	service.publish_frame(11);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "tls-epoch/capture — 얼린 캡처가 있다");
	if (capture)
	{
		check(count_marker(*capture, ce::marker<"SecondLife">()) > 0,
		      "tls-epoch/registered — 같은 스레드가 새 서비스에 다시 잡힌다");
		check_eq(count_marker(*capture, ce::marker<"FirstLife">()), std::size_t{ 0 },
		         "tls-epoch/no-carryover — 지난 서비스의 것은 넘어오지 않는다");
	}

	check_eq(service.summary().foreign_stream_touches, std::uint64_t{ 0 },
	         "tls-epoch/owned — 죽은 자리를 따라가 남의 것을 만지지 않았다");

	service.shutdown();
}


// ⑬ 수집기가 아닌 스레드의 제어 호출은 **수집기로 넘어가** 적용된다.
//
// ★ 링은 프레임 경계를 도는 스레드의 것이다. 창이나 콘솔 스레드가 pause 에서
//   직접 링을 만지면 수집기와 겹친다 — 스트림에서 죽던 것과 같은 경계다.
//   그래서 줄을 세우고, 부른 쪽은 **적용될 때까지 기다린다.** 기다리지 않으면
//   부른 직후의 capture() 가 비어 "얼렸는데 아무것도 없다" 가 된다.
void test_control_from_other_thread()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	std::atomic<int> gate{ 0 };      // 1 = 수집기 멈춤 요청, 2 = 재개
	std::atomic<int> parked{ 0 };
	std::atomic<int> started{ 0 };
	std::atomic<bool> running{ true };

	std::thread collector([&service, &gate, &parked, &started, &running]()
	{
		service.register_thread("Collector");
		std::uint32_t frame = 1;
		while (running.load(std::memory_order_acquire))
		{
			if (1 == gate.load(std::memory_order_acquire))
			{
				// 프레임을 닫지 않고 선다. 이 동안 들어온 제어 요청은
				// 재개하기 전까지 적용될 수 없다.
				parked.store(1, std::memory_order_release);
				while (2 != gate.load(std::memory_order_acquire))
				{
					std::this_thread::yield();
				}
				parked.store(0, std::memory_order_release);
			}

			{ ce::profile_scope scope{ service, ce::marker<"CollectorTick">() }; }
			service.publish_frame(frame++);

			// ★ 이 표식이 선 뒤에야 수집기 스레드가 정해진다. engine_frame 은
			//   record() 가 이미 올려 두므로 그것으로는 "수집기가 돌았다" 를
			//   판정할 수 없다.
			started.store(1, std::memory_order_release);
		}
		service.unregister_thread();
	});

	// 수집기가 자리를 잡을 때까지 기다린다.
	while (0 == started.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	gate.store(1, std::memory_order_release);
	while (0 == parked.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	// ★ 수집기가 멈춰 있는 **동안** 부른다. 부른 쪽이 기다리면 여기서 수집기를
	//   기다리게 되고, 수집기가 이 스레드를 기다리는 배치(UI 의 씬 잠금)에서는
	//   그대로 교착이다. 그래서 곧바로 돌아와야 한다.
	service.pause();

	{
		const ce::live_summary mid = service.summary();
		check(mid.state == ce::recorder_state::pausing,
		      "control-thread/pausing — 수집기가 멈춰 있어도 곧바로 돌아온다");
		check(service.capture() == nullptr,
		      "control-thread/not-yet — 아직 공개하지 않았다고 말한다");
		check(mid.control_requests_deferred > 0,
		      "control-thread/deferred — 수집기가 아닌 호출은 줄을 선다");
	}

	// 수집기를 재개시키면 마무리된다.
	gate.store(2, std::memory_order_release);

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (service.summary().state != ce::recorder_state::frozen
	       && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::yield();
	}

	const ce::live_summary summary = service.summary();
	check(summary.state == ce::recorder_state::frozen,
	      "control-thread/frozen — 수집기가 돌면 얼림이 끝난다");
	check(service.capture() != nullptr,
	      "control-thread/capture — 그때 캡처가 선다");
	check_eq(summary.foreign_stream_touches, std::uint64_t{ 0 },
	         "control-thread/owned — 남의 스트림을 만지지 않았다");

	running.store(false, std::memory_order_release);
	service.record(summary.engine_frame);
	collector.join();
	service.shutdown();
}


// ── 제어 큐 경계 (4차 감사) ──────────────────────────────────────────
//
// 기다림을 넣으면서 잠금 순서를 뒤집었다. UI 는 씬 잠금을 쥔 채 Pause 완료를
// 기다리고, 게임 스레드는 같은 잠금을 통과해야 그 요청을 처리한다.

// ⑭ Pause 요청은 **부른 쪽을 막지 않는다.**
//
// ★ UI 가 쥔 잠금을 수집기가 기다리는 배치를 그대로 세운다. 부른 쪽이 완료를
//   기다리면 둘이 서로를 기다려 상한(2 초)까지 화면이 멈춘다.
void test_pause_does_not_block_caller()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	std::mutex sceneLock;
	std::atomic<int> started{ 0 };
	std::atomic<bool> running{ true };

	std::thread collector([&service, &sceneLock, &started, &running]()
	{
		service.register_thread("Collector");
		std::uint32_t frame = 1;
		while (running.load(std::memory_order_acquire))
		{
			// 게임 스레드도 같은 잠금을 통과해야 프레임을 닫는다.
			std::lock_guard<std::mutex> guard(sceneLock);
			{ ce::profile_scope scope{ service, ce::marker<"LockedTick">() }; }
			service.publish_frame(frame++);
			started.store(1, std::memory_order_release);
		}
		service.unregister_thread();
	});

	while (0 == started.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	std::int64_t elapsedMs = 0;
	{
		// UI 가 쥐는 잠금. 이것을 쥔 채로 Pause 를 부른다.
		std::lock_guard<std::mutex> guard(sceneLock);

		const auto begin = std::chrono::steady_clock::now();
		service.pause();
		elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - begin).count();
	}

	check(elapsedMs < 200,
	      "pause-nonblocking/fast — 잠금을 쥔 채 불러도 곧바로 돌아온다");

	// 잠금을 놓으면 수집기가 마저 돌아 얼림이 끝난다.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (service.summary().state != ce::recorder_state::frozen
	       && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::yield();
	}

	check(service.summary().state == ce::recorder_state::frozen,
	      "pause-nonblocking/frozen — 수집기가 돌면 얼림이 끝난다");
	check(service.capture() != nullptr,
	      "pause-nonblocking/capture — 그때 캡처가 선다");

	running.store(false, std::memory_order_release);
	service.record(service.summary().engine_frame);
	collector.join();
	service.shutdown();
}

// ⑮ Pause 를 부른 스레드 **자신의 기록**이 빠지면 안 된다.
//
// ★ 부른 쪽이 완료를 기다리면, 기다리는 동안 자기 봉인 요청에 응답할 수 없다 —
//   자기가 자기 꼬리를 못 넘긴다. 미응답 1, 그 스레드의 이벤트 0 이 된다.
void test_pause_requester_records_land()
{
	ce::profiler_service service;
	service.initialize({});
	service.record(1);

	std::atomic<int> started{ 0 };
	std::atomic<bool> running{ true };

	std::thread collector([&service, &started, &running]()
	{
		service.register_thread("Collector");
		std::uint32_t frame = 1;
		while (running.load(std::memory_order_acquire))
		{
			{ ce::profile_scope scope{ service, ce::marker<"CollectorBeat">() }; }
			service.publish_frame(frame++);
			started.store(1, std::memory_order_release);
		}
		service.unregister_thread();
	});

	while (0 == started.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	service.register_thread("Requester");
	{ ce::profile_scope scope{ service, ce::marker<"RequesterWork">() }; }
	service.pause();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (service.summary().state != ce::recorder_state::frozen
	       && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::yield();
	}

	const ce::live_summary summary = service.summary();
	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "pause-requester/capture — 얼린 캡처가 있다");
	if (capture)
	{
		check(count_marker(*capture, ce::marker<"RequesterWork">()) > 0,
		      "pause-requester/present — 부른 스레드의 기록이 캡처에 있다");
		check(capture->complete(),
		      "pause-requester/complete — 부른 스레드가 자기 응답을 막지 않는다");
	}
	check_eq(summary.pause_unacked_streams, std::uint32_t{ 0 },
	         "pause-requester/acked — 미응답이 없다");

	running.store(false, std::memory_order_release);
	service.record(summary.engine_frame);
	collector.join();
	service.unregister_thread();
	service.shutdown();
}

// ⑯ 종료는 살아 있는 writer 의 저장소를 해제하지 않는다.
//
// ★ 남의 finish() 를 피해도 곧바로 stream.reset() 으로 파괴하면 같은 일이다.
//   소멸자는 소유 검사 없이 seal_current() 를 부르고, 그 뒤 저장소가 사라진다.
//   확인되지 않은 writer 의 것은 **놓아 둔다** — 확인 뒤에야 해제다.
void test_shutdown_retains_live_storage()
{
	const std::size_t before = ce::profiler_service::retained_stream_count();

	{
		ce::profiler_service service;
		service.initialize({});
		service.register_thread("Main");
		service.record(1);

		std::atomic<int> step{ 0 };
		std::thread worker([&service, &step]()
		{
			service.register_thread("StillRunning");
			{ ce::profile_scope scope{ service, ce::marker<"LiveTick">() }; }
			step.store(1, std::memory_order_release);

			while (step.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }
		});

		while (step.load(std::memory_order_acquire) < 1) { std::this_thread::yield(); }
		service.publish_frame(1);
		service.shutdown();

		check_eq(service.summary().abandoned_streams, std::uint64_t{ 1 },
		         "shutdown-retain/counted — 확인되지 않은 writer 를 센다");
		check_eq(ce::profiler_service::retained_stream_count(), before + 1,
		         "shutdown-retain/kept — 그 저장소를 해제하지 않고 남긴다");

		step.store(2, std::memory_order_release);
		worker.join();
	}
}

// P6-1 — 캡처가 제 어휘를 소유한다.
//
// ★ 지금까지 이름은 **프로세스 전역** registry 로만 풀렸다(marker_info(id)).
//   파일에서 읽은 캡처에는 그것이 통하지 않는다 — 그 프로세스는 남의 빌드가
//   등록한 이름을 등록한 적이 없고, 같은 id 가 전혀 다른 이름을 가리킨다.
//   그러면 표가 **조용히 남의 이름**을 그리고, 어느 게이트도 그것을 못 본다.
//
//   그래서 얼리는 순간 캡처가 제 표를 **글자째** 복사해 들고 간다.
void test_capture_vocabulary()
{
	ce::profiler_service service;
	service.initialize({});
	service.register_thread("VocabThread");
	service.record(1);

	{
		ce::profile_scope outer{ service, ce::marker<"VocabOuter">() };
		busy_ticks(4);
		{
			ce::profile_scope inner{ service, ce::marker<"VocabInner">() };
			busy_ticks(2);
		}
	}

	service.publish_frame(1);
	service.pause();

	const ce::capture_session_ptr capture = service.capture();
	check(capture != nullptr, "vocabulary/capture — 얼린 캡처가 있다");
	if (!capture)
	{
		return;
	}

	const ce::marker_id outer = ce::marker<"VocabOuter">();
	const ce::marker_id inner = ce::marker<"VocabInner">();

	check(capture->marker(outer).name == "VocabOuter",
	      "vocabulary/name-outer — 캡처가 제 이름을 말한다");
	check(capture->marker(inner).name == "VocabInner",
	      "vocabulary/name-inner — 중첩 구간의 이름도 캡처가 말한다");
	check(capture->marker(outer).kind == ce::marker_kind::cpu_scope,
	      "vocabulary/kind — 갈래도 함께 온다");

	// ★ 얼린 뒤에 등록한 것은 이 캡처의 뜻이 아니다. 이 절이 "복사했는가" 와
	//   "전역을 그대로 보고 있는가" 를 가른다 — 전역을 보면 표가 자란다.
	const std::uint32_t before = capture->marker_count();
	const ce::marker_id late =
		ce::intern_runtime_marker("VocabRegisteredAfterFreeze", ce::marker_kind::cpu_scope);
	check_eq(capture->marker_count(), before,
	         "vocabulary/frozen — 얼린 뒤 등록한 마커는 이 캡처에 없다");
	check(capture->marker(late).name.empty(),
	      "vocabulary/unknown — 모르는 id 는 빈 이름이다");
	check(capture->marker(1000000u).name.empty(),
	      "vocabulary/out-of-range — 범위 밖 id 도 안전하다");
	check(capture->marker_count() > 1,
	      "vocabulary/table — 표에 자리표 말고도 무언가 있다");

	// 표가 **인덱스 = id** 로 서야 이벤트가 제 이름을 찾는다.
	bool indexed = true;
	for (const ce::frame_record& frame : capture->frames())
	{
		for (const ce::profile_event& value : frame.events)
		{
			if (value.marker >= capture->marker_count())
			{
				indexed = false;
			}
		}
	}
	check(indexed, "vocabulary/indexed — 모든 이벤트의 marker 가 표 안에 있다");

	// ── 캡처가 제 시계를 들고 다닌다 ────────────────────────────────────
	//
	// ★ 어휘와 같은 결함이 시계에도 있었다. 환산이 **읽는 기계의** QPC
	//   주파수를 쓰면, 다른 기계에서 뜬 캡처의 모든 구간 길이가 두 주파수의
	//   비만큼 틀린다 — 그런데 숫자는 여전히 그럴듯해서 눈으로 못 잡는다.
	const ce::profile_tick frequency = ce::profiler_service::ticks_per_second();
	check(capture->environment().ticks_per_second == frequency,
	      "clock/carried — 캡처가 뜬 기계의 주파수를 들고 있다");
	check(frequency > 0, "clock/frequency — 주파수가 0 이 아니다");

	// 1초치 tick 은 1000 ms 다. 배수가 틀리면 여기서 어긋난다.
	const double oneSecond = capture->milliseconds(frequency);
	check(oneSecond > 999.9 && oneSecond < 1000.1,
	      "clock/milliseconds — 1초치 tick 이 1000 ms 로 환산된다");

	// 주파수를 모르는 캡처는 **0 을 낸다.** 이 기계의 것으로 대신 나누지
	// 않는다 — 그것이 곧 파일 캡처가 거짓 숫자를 내는 길이다.
	const ce::capture_session blank;
	check(blank.milliseconds(frequency) == 0.0,
	      "clock/unknown-zero — 주파수를 모르면 0 ms 다");
}

// ★ 어서션·오류 창을 띄우지 않는다.
//
//   변이 하나가 링의 vector 를 두 스레드가 함께 만지게 만들자 Debug 이터레이터
//   검사가 **모달 창**을 띄웠고, 게이트가 붉어지는 대신 그 자리에서 멈췄다.
//   판정이 종료 코드인 하네스에서 창은 곧 무응답이다. stderr 로 내보내고 곧바로
//   비정상 종료하게 해야 변이가 "잡혔다" 로 읽힌다.
void silence_crt_dialogs()
{
#if defined(_WIN32)
	_set_error_mode(_OUT_TO_STDERR);
	::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#if defined(_DEBUG)
	for (int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
	{
		_CrtSetReportMode(report, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
	}
#endif
#endif
}

int main()
{
	silence_crt_dialogs();

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
	test_reader_graph_window();
	test_reader_fold_cache();
	test_timeline_spans();
	test_timeline_view();
	test_scope_across_state_change();
	test_reader_sync();
	test_gpu_lane();
	test_track_order();
	test_gpu_span_origin();
	test_live_capture_while_recording();
	test_call_distribution();
	test_flat_distribution_union();
	test_frame_boundaries();
	test_instant_events();
	test_gpu_lane_deferred();
	test_gpu_lane_dropped();
	test_concurrent_publish();
	test_pause_truncates_open_scope();
	test_late_cpu_attribution();
	test_clear_generation();
	test_pause_truncates_worker_scope();
	test_clear_keeps_new_events();
	test_clear_drops_scope_opened_before();
	test_scope_pairing_after_resume();
	test_pause_ack_implies_delivery();
	test_capture_reports_incompleteness();
	test_capture_reports_completeness();
	test_shutdown_owns_only_its_stream();
	test_tls_slot_epoch();
	test_control_from_other_thread();
	test_pause_does_not_block_caller();
	test_pause_requester_records_land();
	test_shutdown_retains_live_storage();
	test_capture_vocabulary();

	std::printf("profile core probe: %d checks, %d failures\n", g_checks, g_failures);
	if (g_failures == 0)
	{
		std::printf("PROFILE_CORE_OK=true\n");
	}
	return g_failures == 0 ? 0 : 1;
}
