#include "ProfilerSelfTest.h"

#include "Profiler.h"

#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// 유니티 빌드가 켜져 있다(ImGuiHelper x64: EnableUnitySupport=true). 익명
// 네임스페이스는 합본 안에서 다른 파일의 익명 네임스페이스와 한 덩어리가 되므로
// 이름 있는 네임스페이스로 격리한다.
namespace ProfilerSelfTestDetail
{
	using Event = CPUProfiler::EventData::Event;

	constexpr const char* kNestedL0 = "PSelfTest.Nested.L0";
	constexpr const char* kNestedL1 = "PSelfTest.Nested.L1";
	constexpr const char* kNestedL2 = "PSelfTest.Nested.L2";
	constexpr const char* kCrossFrame = "PSelfTest.CrossFrame";
	constexpr const char* kOverflow = "PSelfTest.Overflow";

	// 픽스처 도중에 예외가 나가면 조인되지 않은 std::thread가 파괴되고,
	// 표준에 따라 std::terminate가 불린다 — 검사가 "실패"를 보고하는 대신
	// 에디터 프로세스가 즉사한다. 무슨 일이 있어도 워커를 풀고 조인한다.
	template<typename Fn>
	struct ScopeGuard
	{
		explicit ScopeGuard(Fn fn) : m_fn(std::move(fn)) {}

		~ScopeGuard()
		{
			// 되감기 중에 예외를 흘리면 그것 자체가 terminate다. 여기서는 삼킨다.
			try { Run(); } catch (...) { }
		}

		// 정상 경로에서 앞당겨 실행한다(뒷정리 결과를 보고 판정해야 하므로).
		// 두 번 돌지 않으므로 소멸자와 함께 써도 안전하다.
		void Run()
		{
			if (m_active)
			{
				m_active = false;
				m_fn();
			}
		}

		ScopeGuard(const ScopeGuard&) = delete;
		ScopeGuard& operator=(const ScopeGuard&) = delete;

	private:
		Fn m_fn;
		bool m_active = true;
	};

	template<typename Fn>
	ScopeGuard<Fn> MakeScopeGuard(Fn fn) { return ScopeGuard<Fn>(std::move(fn)); }

	struct Report
	{
		std::string text;
		int passed = 0;
		int failed = 0;
		int skipped = 0;
		int knownDefects = 0;

		void Line(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			text += buffer;
			text += '\n';
		}

		void Pass(const char* name, const char* detail)
		{
			++passed;
			Line("  [PASS]         %-28s %s", name, detail);
		}

		void Fail(const char* name, const char* expected, const char* actual)
		{
			++failed;
			Line("  [FAIL]         %-28s 기대=%s / 실측=%s", name, expected, actual);
		}

		void Skip(const char* name, const char* reason)
		{
			++skipped;
			Line("  [SKIP]         %-28s %s", name, reason);
		}

		// 이미 아는 결함. 실패로 세지 않는다 — 지금의 참을 못박는 것이 목적이고,
		// P2에서 이 항목이 PASS로 바뀌는 것이 진척의 정의다.
		void KnownDefect(const char* name, const char* expected, const char* actual)
		{
			++knownDefects;
			Line("  [KNOWN-DEFECT] %-28s 기대(P2 이후)=%s / 현재=%s", name, expected, actual);
		}
	};

	// GetThreads()는 m_ThreadData를 그대로 들여다보는 스팬이라 등록이 겹치면
	// 재할당으로 무효가 될 수 있다. 검사는 등록이 멎은 구간에서만 읽는다.
	const CPUProfiler::ThreadData* FindThreadById(uint32_t threadId)
	{
		for (const CPUProfiler::ThreadData& thread : gCPUProfiler.GetThreads())
		{
			if (thread.ThreadID == threadId && nullptr != thread.pTLS)
				return &thread;
		}
		return nullptr;
	}

	// FindThreadById와 같은 조건으로 은퇴 슬롯을 걸러낸다. 비대칭으로 두면
	// 은퇴한 옛 주인의 슬롯을 새 주인으로 착각하는 함정이 남는다.
	const CPUProfiler::ThreadData* FindThreadByName(const char* name)
	{
		for (const CPUProfiler::ThreadData& thread : gCPUProfiler.GetThreads())
		{
			if (0 == strcmp(thread.Name, name) && nullptr != thread.pTLS)
				return &thread;
		}
		return nullptr;
	}

	// 이름이 일치하는 슬롯이 끊겼는지. 은퇴 검증은 pTLS가 널이어야 통과다.
	bool IsSlotRetired(const char* name)
	{
		for (const CPUProfiler::ThreadData& thread : gCPUProfiler.GetThreads())
		{
			if (0 == strcmp(thread.Name, name))
				return nullptr == thread.pTLS;
		}
		return false;
	}

	const Event* FindEvent(const CPUProfiler::ThreadData& thread, uint32 frame, const char* name)
	{
		for (const Event& event : gCPUProfiler.GetEventsForThread(thread, frame))
		{
			if (nullptr != event.pName && 0 == strcmp(event.pName, name))
				return &event;
		}
		return nullptr;
	}

	// 방금 Tick()으로 닫힌 프레임.
	uint32 LastCompletedFrame()
	{
		return gCPUProfiler.GetFrameRange().End - 1;
	}

	double TicksToMicroseconds(uint64 ticks)
	{
		return (double)ticks * 1000000.0 / (double)CPUProfiler::GetTicksPerSecond();
	}

	//-------------------------------------------------------------------------
	// F1 — 중첩 스코프: 깊이와 포함 관계
	//-------------------------------------------------------------------------
	void FixtureNested(Report& report)
	{
		gCPUProfiler.BeginEvent(kNestedL0, __FILE__, __LINE__);
		gCPUProfiler.BeginEvent(kNestedL1, __FILE__, __LINE__);
		gCPUProfiler.BeginEvent(kNestedL2, __FILE__, __LINE__);
		gCPUProfiler.EndEvent();
		gCPUProfiler.EndEvent();
		gCPUProfiler.EndEvent();

		gCPUProfiler.Tick();

		const CPUProfiler::ThreadData* pThread = FindThreadById(::GetCurrentThreadId());
		if (nullptr == pThread)
		{
			report.Fail("nested/thread-lookup", "호출 스레드가 등록 목록에 있음", "없음");
			return;
		}

		const uint32 frame = LastCompletedFrame();
		const Event* pL0 = FindEvent(*pThread, frame, kNestedL0);
		const Event* pL1 = FindEvent(*pThread, frame, kNestedL1);
		const Event* pL2 = FindEvent(*pThread, frame, kNestedL2);

		if (nullptr == pL0 || nullptr == pL1 || nullptr == pL2)
		{
			char actual[128];
			snprintf(actual, sizeof(actual), "L0=%s L1=%s L2=%s",
				pL0 ? "있음" : "없음", pL1 ? "있음" : "없음", pL2 ? "있음" : "없음");
			report.Fail("nested/capture", "세 스코프 모두 수집", actual);
			return;
		}
		report.Pass("nested/capture", "세 스코프 모두 수집됨");

		const uint32 d0 = pL0->Depth;
		const uint32 d1 = pL1->Depth;
		const uint32 d2 = pL2->Depth;
		if (d1 == d0 + 1 && d2 == d1 + 1)
		{
			char detail[128];
			snprintf(detail, sizeof(detail), "깊이 %u < %u < %u (상대값 검사)", d0, d1, d2);
			report.Pass("nested/depth", detail);
		}
		else
		{
			char actual[128];
			snprintf(actual, sizeof(actual), "%u, %u, %u", d0, d1, d2);
			report.Fail("nested/depth", "연속 증가(d, d+1, d+2)", actual);
		}

		const bool contained =
			pL0->TicksBegin <= pL1->TicksBegin && pL1->TicksBegin <= pL2->TicksBegin &&
			pL2->TicksEnd <= pL1->TicksEnd && pL1->TicksEnd <= pL0->TicksEnd;
		if (contained)
		{
			report.Pass("nested/containment", "안쪽 구간이 바깥 구간에 포함됨");
		}
		else
		{
			report.Fail("nested/containment", "L2 ⊂ L1 ⊂ L0", "포함 관계 깨짐");
		}

		if (nullptr != pL0->pFilePath && pL0->LineNumber > 0)
		{
			report.Pass("nested/metadata", "파일·행 정보 보존됨");
		}
		else
		{
			report.Fail("nested/metadata", "파일 경로와 행 번호 보존", "유실");
		}
	}

	//-------------------------------------------------------------------------
	// F2 — 멀티스레드: 스레드별 귀속과 안전한 은퇴
	//-------------------------------------------------------------------------
	struct Rendezvous
	{
		std::mutex mutex;
		std::condition_variable cv;
		int emitted = 0;
		bool collected = false;
	};

	void FixtureMultithread(Report& report)
	{
		constexpr int kWorkers = 3;

		char threadNames[kWorkers][64]{};
		char eventNames[kWorkers][64]{};
		for (int i = 0; i < kWorkers; ++i)
		{
			snprintf(threadNames[i], sizeof(threadNames[i]), "[PSelfTest-W%d]", i);
			snprintf(eventNames[i], sizeof(eventNames[i]), "PSelfTest.Worker%d.Scope", i);
		}

		Rendezvous rv;
		std::vector<std::thread> workers;
		workers.reserve(kWorkers);

		// 중간에 무엇이 던지든 워커를 풀고 조인한다. 조인 안 된 thread가
		// 파괴되면 std::terminate라 검사가 아니라 에디터가 죽는다.
		auto guard = MakeScopeGuard([&rv, &workers]
		{
			{
				std::lock_guard<std::mutex> lock(rv.mutex);
				rv.collected = true;
			}
			rv.cv.notify_all();
			for (std::thread& worker : workers)
			{
				if (worker.joinable())
					worker.join();
			}
		});

		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back([&rv, &threadNames, &eventNames, i]
			{
				gCPUProfiler.RegisterThread(threadNames[i]);
				gCPUProfiler.BeginEvent(eventNames[i], __FILE__, __LINE__);
				gCPUProfiler.EndEvent();

				std::unique_lock<std::mutex> lock(rv.mutex);
				++rv.emitted;
				rv.cv.notify_all();
				rv.cv.wait(lock, [&rv] { return rv.collected; });
				lock.unlock();

				// 수집이 끝난 뒤에 은퇴한다. 이것 없이 스레드가 죽으면 ThreadData가
				// 사라진 thread_local을 계속 가리키고, 이후 모든 Tick()이 해제된
				// 저장소를 읽는다 — 검사가 에디터를 망가뜨린 채 끝난다.
				gCPUProfiler.UnregisterThread();
			});
		}

		{
			std::unique_lock<std::mutex> lock(rv.mutex);
			rv.cv.wait(lock, [&rv] { return rv.emitted == kWorkers; });
		}

		gCPUProfiler.Tick();
		const uint32 frame = LastCompletedFrame();

		int found = 0;
		int misattributed = 0;
		for (int i = 0; i < kWorkers; ++i)
		{
			const CPUProfiler::ThreadData* pThread = FindThreadByName(threadNames[i]);
			if (nullptr == pThread)
				continue;

			const Event* pEvent = FindEvent(*pThread, frame, eventNames[i]);
			if (nullptr == pEvent)
				continue;

			++found;
			if (pEvent->ThreadIndex != pThread->Index)
				++misattributed;
		}

		// 워커를 풀고 조인한다(가드가 한다). 은퇴 검증은 조인 뒤라야 의미가 있다.
		guard.Run();

		if (found == kWorkers)
		{
			char detail[128];
			snprintf(detail, sizeof(detail), "워커 %d개 모두 자기 슬롯에 수집됨", kWorkers);
			report.Pass("multithread/capture", detail);
		}
		else
		{
			char actual[128];
			snprintf(actual, sizeof(actual), "%d/%d", found, kWorkers);
			report.Fail("multithread/capture", "워커 전원의 이벤트 수집", actual);
		}

		if (0 == misattributed)
		{
			report.Pass("multithread/attribution", "이벤트의 ThreadIndex가 슬롯과 일치");
		}
		else
		{
			char actual[128];
			snprintf(actual, sizeof(actual), "%d건 불일치", misattributed);
			report.Fail("multithread/attribution", "전부 일치", actual);
		}

		// 은퇴가 실제로 슬롯을 끊었는지 슬롯을 직접 본다. 하나라도 안 끊기면
		// 그 슬롯은 이미 죽은 TLS를 가리키고 다음 Tick()이 그것을 읽는다.
		int retired = 0;
		for (int i = 0; i < kWorkers; ++i)
		{
			if (IsSlotRetired(threadNames[i]))
				++retired;
		}

		if (retired == kWorkers)
		{
			report.Pass("multithread/retire", "종료한 스레드의 슬롯이 전부 끊김");
		}
		else
		{
			char actual[128];
			snprintf(actual, sizeof(actual), "%d/%d만 끊김", retired, kWorkers);
			report.Fail("multithread/retire", "워커 수만큼 은퇴", actual);
		}
	}

	//-------------------------------------------------------------------------
	// F3 — 프레임을 넘는 스코프
	//
	// 게임 스레드에서 하면 안 된다. Tick()은 스택 맨 위를 무조건 닫으므로
	// 열린 스코프가 있으면 "CPU Frame" 대신 그것을 닫고, 게임 스레드의 스택이
	// 프레임마다 한 칸씩 깊어져 MAX_STACK_DEPTH(32)에서 죽는다. 워커에서만 본다.
	//-------------------------------------------------------------------------
	struct CrossFrameSync
	{
		std::mutex mutex;
		std::condition_variable cv;
		bool opened = false;
		bool ticked = false;
		bool closed = false;
		bool collected = false;
	};

	void FixtureCrossFrame(Report& report)
	{
		CrossFrameSync sync;
		const char* kThreadName = "[PSelfTest-Cross]";

		std::thread worker([&sync, kThreadName]
		{
			gCPUProfiler.RegisterThread(kThreadName);
			gCPUProfiler.BeginEvent(kCrossFrame, __FILE__, __LINE__);

			std::unique_lock<std::mutex> lock(sync.mutex);
			sync.opened = true;
			sync.cv.notify_all();
			sync.cv.wait(lock, [&sync] { return sync.ticked; });
			lock.unlock();

			gCPUProfiler.EndEvent();

			lock.lock();
			sync.closed = true;
			sync.cv.notify_all();
			sync.cv.wait(lock, [&sync] { return sync.collected; });
			lock.unlock();

			gCPUProfiler.UnregisterThread();
		});

		// 어느 단계에서 빠져나가든 워커를 모든 대기에서 풀고 조인한다.
		auto guard = MakeScopeGuard([&sync, &worker]
		{
			{
				std::lock_guard<std::mutex> lock(sync.mutex);
				sync.ticked = true;
				sync.collected = true;
			}
			sync.cv.notify_all();
			if (worker.joinable())
				worker.join();
		});

		{
			std::unique_lock<std::mutex> lock(sync.mutex);
			sync.cv.wait(lock, [&sync] { return sync.opened; });
		}

		// 스코프가 열린 채로 프레임 경계를 넘긴다.
		gCPUProfiler.Tick();
		const uint32 frameDuring = LastCompletedFrame();

		{
			std::lock_guard<std::mutex> lock(sync.mutex);
			sync.ticked = true;
		}
		sync.cv.notify_all();

		{
			std::unique_lock<std::mutex> lock(sync.mutex);
			sync.cv.wait(lock, [&sync] { return sync.closed; });
		}

		gCPUProfiler.Tick();
		const uint32 frameAfter = LastCompletedFrame();

		const CPUProfiler::ThreadData* pThread = FindThreadByName(kThreadName);
		const bool inDuring = (nullptr != pThread) && (nullptr != FindEvent(*pThread, frameDuring, kCrossFrame));
		const bool inAfter = (nullptr != pThread) && (nullptr != FindEvent(*pThread, frameAfter, kCrossFrame));

		guard.Run();

		if (inDuring || inAfter)
		{
			report.Pass("cross-frame/preserve",
				inAfter ? "닫힌 다음 프레임에 보존됨" : "경계 프레임에 보존됨");
		}
		else
		{
			// 현재 구조의 필연: Tick()이 TicksEnd==0인 이벤트를 건너뛰면서
			// pTLS->NumEvents를 0으로 되돌리는데, 스택은 그 인덱스를 계속 쥐고 있다.
			// 스코프가 닫혀도 그 슬롯은 다시 수집되지 않는다.
			report.KnownDefect("cross-frame/preserve", "구간 보존", "조용히 유실");
		}
	}

	//-------------------------------------------------------------------------
	// F4 — 용량 초과: 누락을 세는가
	//-------------------------------------------------------------------------
	void FixtureOverflow(Report& report)
	{
		const CPUProfiler::Stats before = gCPUProfiler.GetStats();
		const uint32 capacity = before.EventCapacity;
		if (0 == capacity)
		{
			report.Skip("overflow/count", "이벤트 상한을 읽지 못했다(Initialize 미호출?)");
			return;
		}

		const uint32 emit = capacity + capacity / 4;
		for (uint32 i = 0; i < emit; ++i)
		{
			gCPUProfiler.BeginEvent(kOverflow, __FILE__, __LINE__);
			gCPUProfiler.EndEvent();
		}

		gCPUProfiler.Tick();
		const CPUProfiler::Stats after = gCPUProfiler.GetStats();

		const uint64 droppedEvents = after.TotalDroppedEvents - before.TotalDroppedEvents;
		const uint64 droppedNames = after.TotalDroppedNames - before.TotalDroppedNames;

		if (droppedEvents > 0)
		{
			char detail[192];
			snprintf(detail, sizeof(detail), "%u개 발생 → %llu개 누락 기록(상한 %u)",
				emit, (unsigned long long)droppedEvents, capacity);
			report.Pass("overflow/count", detail);
		}
		else
		{
			report.Fail("overflow/count", "상한 초과분이 누락으로 기록됨", "누락 0");
		}

		if (after.LastFrameEvents <= capacity)
		{
			report.Pass("overflow/bounds", "수집량이 상한을 넘지 않음");
		}
		else
		{
			char actual[128];
			snprintf(actual, sizeof(actual), "%u > %u", after.LastFrameEvents, capacity);
			report.Fail("overflow/bounds", "수집량 <= 상한", actual);
		}

		if (droppedNames > 0)
		{
			char detail[192];
			snprintf(detail, sizeof(detail), "이름 예산(%u바이트) 소진 %llu건이 기록됨",
				after.NameCapacity, (unsigned long long)droppedNames);
			report.Pass("overflow/name-budget", detail);
		}
		else
		{
			report.Skip("overflow/name-budget", "이번 부하로는 이름 예산이 소진되지 않았다");
		}
	}
}

std::string GetProfilerStatsReport()
{
	using namespace ProfilerSelfTestDetail;

	const CPUProfiler::Stats stats = gCPUProfiler.GetStats();
	const URange range = gCPUProfiler.GetFrameRange();

	Report report;
	report.Line("[profile.stats] 프로파일러 자체 비용과 용량 소진");
	report.Line("  상태            %s / 보존 프레임 [%u, %u)",
		gCPUProfiler.IsPaused() ? "일시정지" : "기록 중", range.Begin, range.End);

	if (0 == stats.TickCount)
	{
		report.Line("  아직 Tick()이 한 번도 돌지 않았다.");
		return report.text;
	}

	const double avgUs = TicksToMicroseconds(stats.TotalTickTicks / stats.TickCount);
	report.Line("  Tick 비용       평균 %.1fus / 최대 %.1fus / 마지막 %.1fus  (%llu회)",
		avgUs,
		TicksToMicroseconds(stats.PeakTickTicks),
		TicksToMicroseconds(stats.LastTickTicks),
		(unsigned long long)stats.TickCount);

	const double eventPct = stats.EventCapacity
		? (double)stats.PeakFrameEvents * 100.0 / (double)stats.EventCapacity : 0.0;
	report.Line("  이벤트/프레임   마지막 %u / 최대 %u / 상한 %u  (최대가 상한의 %.0f%%)",
		stats.LastFrameEvents, stats.PeakFrameEvents, stats.EventCapacity, eventPct);

	const double namePct = stats.NameCapacity
		? (double)stats.PeakFrameNameBytes * 100.0 / (double)stats.NameCapacity : 0.0;
	report.Line("  이름 바이트     마지막 %u / 최대 %u / 상한 %u  (최대가 상한의 %.0f%%)",
		stats.LastFrameNameBytes, stats.PeakFrameNameBytes, stats.NameCapacity, namePct);

	report.Line("  누적 누락       이벤트 %llu / 이름 %llu",
		(unsigned long long)stats.TotalDroppedEvents,
		(unsigned long long)stats.TotalDroppedNames);
	report.Line("  불균형 스코프   %u  (스코프가 열린 채 은퇴한 건수)", stats.MalformedScopes);

	const Span<const CPUProfiler::ThreadData> threads = gCPUProfiler.GetThreads();
	report.Line("  스레드 슬롯     %zu개 (은퇴 %u개)", threads.size(), stats.RetiredThreads);
	for (const CPUProfiler::ThreadData& thread : threads)
	{
		report.Line("    [%u] %-24s tid=%u %s",
			thread.Index, thread.Name, thread.ThreadID,
			(nullptr != thread.pTLS) ? "" : "(은퇴)");
	}

	return report.text;
}

bool RunProfilerSelfTest(std::string& outLog)
{
	using namespace ProfilerSelfTestDetail;

	Report report;
	report.Line("[profile.selftest] 현행 CPU 프로파일러 특성화 검사 (PHASE 14 P0)");
	report.Line("  주의: 프레임 경계를 스스로 넘으므로 라이브 캡처를 교란한다.");

	// 일시정지 상태에서는 BeginEvent가 아무것도 하지 않는다. 그대로 돌면
	// 모든 항목이 "수집 안 됨"으로 실패해 원인을 가린다.
	if (gCPUProfiler.IsPaused())
	{
		report.Line("");
		report.Line("  프로파일러가 일시정지 상태다 — 재개한 뒤 다시 실행할 것.");
		outLog = report.text;
		return false;
	}

	// 히스토리(5프레임)가 아직 안 찼으면 GetEventsForThread의 범위 검사에 걸린다.
	if (gCPUProfiler.GetFrameRange().End < 2)
	{
		report.Line("");
		report.Line("  프레임이 아직 충분히 돌지 않았다 — 몇 프레임 뒤에 다시 실행할 것.");
		outLog = report.text;
		return false;
	}

	report.Line("");
	FixtureNested(report);
	FixtureMultithread(report);
	FixtureCrossFrame(report);
	FixtureOverflow(report);

	report.Line("");
	report.text += GetProfilerStatsReport();

	report.Line("");
	report.Line("  통과 %d / 실패 %d / 건너뜀 %d / 기지 결함 %d",
		report.passed, report.failed, report.skipped, report.knownDefects);

	const bool ok = (0 == report.failed);
	report.Line("PROFILE_SELFTEST_OK=%s", ok ? "true" : "false");

	outLog = report.text;
	return ok;
}
