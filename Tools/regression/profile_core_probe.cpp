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

	std::printf("profile core probe: %d checks, %d failures\n", g_checks, g_failures);
	if (g_failures == 0)
	{
		std::printf("PROFILE_CORE_OK=true\n");
	}
	return g_failures == 0 ? 0 : 1;
}
