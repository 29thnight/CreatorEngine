// Long-running collector boundary probe. One process owns all intentionally
// abandoned streams and exits after checking that shutdown did not race a writer.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

#include "ProfileScope.h"
#include "ProfileService.h"

namespace
{
	using clock_type = std::chrono::steady_clock;
	constexpr std::uint32_t kBurstFrames = 16384;
	constexpr int kWriters = 4;

    bool record_sync(ce::profiler_service& service, std::uint32_t first_frame)
    {
        service.record(first_frame);
        service.wait_until_idle();
        if (service.state() != ce::recorder_state::recording)
        {
            std::fprintf(stderr, "Record did not start before the workload\n");
            return false;
        }
        return true;
    }

    bool pause_sync(ce::profiler_service& service)
    {
        service.pause();
        service.wait_until_idle();
        const auto deadline = clock_type::now() + std::chrono::seconds(5);
        ce::recording_status status = service.recording_status();
        while (status.state != ce::recording_state::finalized &&
               status.state != ce::recording_state::failed && clock_type::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            status = service.recording_status();
        }
        if (service.state() != ce::recorder_state::frozen ||
            status.state != ce::recording_state::finalized)
        {
            std::fprintf(stderr, "Pause did not finalize the disk recording\n");
            return false;
        }
        // finalized는 파일 봉인 완료만 뜻한다. 메모리 링의 기존 수치와
        // 별개로 디스크 큐 및 원본 생산자 손실도 없어야 녹화를 유효하게 본다.
        const ce::recording_source_losses& source = status.source_losses;
        if (status.dropped_frames != 0 || status.dropped_events != 0 ||
            status.dropped_counters != 0 || status.source_dropped_counters != 0 ||
            source.dropped_events != 0 || source.dropped_frame_boundaries != 0 ||
            source.late_events != 0 || source.late_gpu_spans != 0)
        {
            std::fprintf(stderr,
                "Disk recording lost data: writerFrames=%llu writerEvents=%llu "
                "writerCounters=%llu sourceCounters=%llu sourceEvents=%llu "
                "sourceBoundaries=%llu sourceLateEvents=%llu sourceLateGpuSpans=%llu\n",
                static_cast<unsigned long long>(status.dropped_frames),
                static_cast<unsigned long long>(status.dropped_events),
                static_cast<unsigned long long>(status.dropped_counters),
                static_cast<unsigned long long>(status.source_dropped_counters),
                static_cast<unsigned long long>(source.dropped_events),
                static_cast<unsigned long long>(source.dropped_frame_boundaries),
                static_cast<unsigned long long>(source.late_events),
                static_cast<unsigned long long>(source.late_gpu_spans));
            return false;
        }
        return true;
    }

    bool clear_sync(ce::profiler_service& service)
    {
        const std::uint64_t ticket = service.clear();
        service.wait_until_idle();
        if (ticket == 0 || !service.control_applied(ticket))
        {
            std::fprintf(stderr, "Clear was rejected or did not complete\n");
            return false;
        }
        return true;
    }

	int seconds_from_args(int argc, char** argv)
	{
		if (argc != 2) return 20;
		const int value = std::atoi(argv[1]);
		return value >= 5 && value <= 300 ? value : 20;
	}

	void write_scopes(ce::profiler_service& service, std::atomic<bool>& running,
	                  std::atomic<std::uint64_t>& attempted)
	{
		service.register_thread("StressWriter");
		const ce::marker_id marker = ce::marker<"StressScope">();
		while (running.load(std::memory_order_acquire))
		{
			{ ce::profile_scope scope{ service, marker }; }
			attempted.fetch_add(1, std::memory_order_relaxed);
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
		service.unregister_thread();
	}
}

int main(int argc, char** argv)
{
	const int seconds = seconds_from_args(argc, argv);
	std::uint64_t attemptedFrames = 0;
	std::uint64_t collectedFrames = 0;
	std::uint64_t droppedFrames = 0;
	std::uint64_t writerScopes = 0;
	std::uint64_t cycles = 0;
	std::uint64_t malformedPages = 0;
	std::uint64_t foreignTouches = 0;
	bool accounted = true;
	bool drained = true;
	bool complete = true;
    bool controlsSucceeded = true;
	const auto began = clock_type::now();
	{
		ce::profiler_config config;
		config.retained_frames = kBurstFrames;
        config.max_queued_frames = kBurstFrames;
		config.memory_budget = 128u * 1024u * 1024u;
		ce::profiler_service service;
		service.initialize(config);
		std::atomic<bool> running{ true };
		std::atomic<std::uint64_t> scopes{ 0 };
		std::vector<std::thread> writers;
		for (int i = 0; i < kWriters; ++i)
		{
			writers.emplace_back(write_scopes, std::ref(service), std::ref(running),
			                     std::ref(scopes));
		}
		std::uint32_t frame = 1;
		const auto deadline = began + std::chrono::seconds(seconds);
		do
		{
            // Each burst owns a fresh session. Close the previous writer before
            // Clear/Record so the reset cannot be rejected or race disk finalization.
            if (service.state() == ce::recorder_state::recording)
            {
                controlsSucceeded = pause_sync(service);
            }
            if (!controlsSucceeded || !clear_sync(service) || !record_sync(service, frame))
            {
                controlsSucceeded = false;
                break;
            }
			for (std::uint32_t i = 0; i < kBurstFrames; ++i)
			{
				service.publish_frame(frame++);
			}
			service.wait_until_idle();
			const ce::live_summary sample = service.summary();
			++cycles;
			attemptedFrames += kBurstFrames;
			collectedFrames += sample.retained_frames;
			droppedFrames += sample.collector_dropped_frames;
			accounted &= sample.retained_frames == kBurstFrames
				&& sample.collector_dropped_frames == 0
				&& sample.dropped_events == 0;
			drained &= sample.collector_queued_frames == 0;
			malformedPages += sample.malformed_pages;
			foreignTouches += sample.foreign_stream_touches;
		}
		while (clock_type::now() < deadline);

		running.store(false, std::memory_order_release);
		for (std::thread& writer : writers) writer.join();
		writerScopes = scopes.load(std::memory_order_relaxed);
        if (service.state() == ce::recorder_state::recording)
        {
            controlsSucceeded = pause_sync(service) && controlsSucceeded;
        }
		complete = service.summary().capture_complete;
		service.shutdown();
	}

	// A separate instance is stopped while writers and the frame submitter are
	// still active. They remain valid until joined; shutdown must retain rather
	// than free their stream storage.
	std::uint64_t activeShutdownScopes = 0;
	std::uint64_t activeShutdownFrames = 0;
	std::uint64_t postShutdownScopeAttempts = 0;
	std::uint64_t abandoned = 0;
	std::uint64_t shutdownForeign = 0;
	{
		ce::profiler_service service;
		ce::profiler_config config;
		// 종료 경합만 보는 단계는 제출자가 계속 도므로 의도적으로 상한을 둔다.
		config.max_queued_frames = 256;
		service.initialize(config);
        if (!record_sync(service, 1))
        {
            return 1;
        }
		std::atomic<bool> running{ true };
		std::atomic<std::uint64_t> scopes{ 0 };
		std::atomic<std::uint64_t> frames{ 0 };
		std::atomic<bool> openScope{ false };
		std::atomic<bool> resumeOpenScope{ false };
		std::vector<std::thread> writers;
		for (int i = 0; i < kWriters; ++i)
		{
			writers.emplace_back(write_scopes, std::ref(service), std::ref(running),
			                     std::ref(scopes));
		}
		std::thread parkedWriter([&]()
		{
			service.register_thread("OpenAtShutdown");
			service.begin_scope(ce::marker<"OpenAtShutdown">());
			openScope.store(true, std::memory_order_release);
			while (!resumeOpenScope.load(std::memory_order_acquire))
				std::this_thread::yield();
			service.end_scope();
			service.unregister_thread();
		});
		while (!openScope.load(std::memory_order_acquire))
			std::this_thread::yield();
		std::thread submitter([&]()
		{
			std::uint32_t frame = 1;
			while (running.load(std::memory_order_acquire))
			{
				service.publish_frame(frame++);
				frames.fetch_add(1, std::memory_order_relaxed);
			}
		});
		std::this_thread::sleep_for(std::chrono::seconds(2));
		const std::uint64_t scopesBeforeShutdown = scopes.load(std::memory_order_relaxed);
		service.shutdown();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		postShutdownScopeAttempts = scopes.load(std::memory_order_relaxed) - scopesBeforeShutdown;
		running.store(false, std::memory_order_release);
		resumeOpenScope.store(true, std::memory_order_release);
		for (std::thread& writer : writers) writer.join();
		parkedWriter.join();
		submitter.join();
		activeShutdownScopes = scopes.load(std::memory_order_relaxed);
		activeShutdownFrames = frames.load(std::memory_order_relaxed);
		const ce::live_summary final = service.summary();
		abandoned = final.abandoned_streams;
		shutdownForeign = final.foreign_stream_touches;
	}

	const double elapsed = std::chrono::duration<double>(clock_type::now() - began).count();
    const bool passed = controlsSucceeded && accounted && drained && complete && cycles > 0
		&& malformedPages == 0 && foreignTouches == 0 && writerScopes > 0
		&& activeShutdownScopes > 0 && activeShutdownFrames > 0
		&& postShutdownScopeAttempts > 0 && abandoned >= kWriters + 1
		&& shutdownForeign == 0;
	std::printf("{\"passed\":%s,\"seconds\":%.3f,\"cycles\":%llu,"
		"\"attemptedFrames\":%llu,\"collectedFrames\":%llu,"
		"\"droppedFrames\":%llu,"
		"\"writerScopes\":%llu,\"malformedPages\":%llu,"
		"\"foreignTouches\":%llu,\"accounted\":%s,\"drained\":%s,"
		"\"complete\":%s,\"activeShutdownScopes\":%llu,"
		"\"activeShutdownFrames\":%llu,\"postShutdownScopeAttempts\":%llu,"
		"\"abandoned\":%llu,"
		"\"shutdownForeign\":%llu}\n",
		passed ? "true" : "false", elapsed,
		static_cast<unsigned long long>(cycles),
		static_cast<unsigned long long>(attemptedFrames),
		static_cast<unsigned long long>(collectedFrames),
		static_cast<unsigned long long>(droppedFrames),
		static_cast<unsigned long long>(writerScopes),
		static_cast<unsigned long long>(malformedPages),
		static_cast<unsigned long long>(foreignTouches),
		accounted ? "true" : "false", drained ? "true" : "false",
		complete ? "true" : "false",
		static_cast<unsigned long long>(activeShutdownScopes),
		static_cast<unsigned long long>(activeShutdownFrames),
		static_cast<unsigned long long>(postShutdownScopeAttempts),
		static_cast<unsigned long long>(abandoned),
		static_cast<unsigned long long>(shutdownForeign));
	return passed ? 0 : 1;
}
