// Controlled core workload for the four profiler modes in §11.4. This is a
// CPU-side microbenchmark; it is not a substitute for Editor/Player frame times.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

#include "ProfileScope.h"
#include "ProfileService.h"

namespace
{
	using clock_type = std::chrono::steady_clock;
	std::atomic<std::uint64_t> sink{ 0 };

	std::uint64_t thread_cpu_100ns(HANDLE handle)
	{
		if (!handle) return 0;
		FILETIME created{}, exited{}, kernel{}, user{};
		if (!::GetThreadTimes(handle, &created, &exited, &kernel, &user)) return 0;
		ULARGE_INTEGER k{}, u{};
		k.LowPart = kernel.dwLowDateTime;
		k.HighPart = kernel.dwHighDateTime;
		u.LowPart = user.dwLowDateTime;
		u.HighPart = user.dwHighDateTime;
		return k.QuadPart + u.QuadPart;
	}

	double percentile_ms(std::vector<double> samples, double fraction)
	{
		if (samples.empty()) return 0.0;
		std::sort(samples.begin(), samples.end());
		const std::size_t rank = static_cast<std::size_t>(std::ceil(fraction * samples.size()));
		return samples[(std::max)(std::size_t{ 1 }, rank) - 1];
	}

	std::uint64_t work_unit(std::uint64_t value)
	{
		for (int i = 0; i < 12; ++i)
		{
			value ^= value << 13;
			value ^= value >> 7;
			value ^= value << 17;
		}
		return value;
	}

	void pace_collector()
	{
		const auto until = clock_type::now() + std::chrono::microseconds(500);
		while (clock_type::now() < until) std::this_thread::yield();
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) return 2;
	const std::string_view mode = argv[1];
	const int frames = std::atoi(argv[2]);
	if (frames < 100 || frames > 100000) return 2;
	const bool shipping = mode == "shipping";
	const bool stopped = mode == "stopped";
	const bool cpu = mode == "cpu";
	const bool cpuGpu = mode == "cpu-gpu";
	if (!(shipping || stopped || cpu || cpuGpu)) return 2;
#if CE_SHIPPING
	if (!shipping) return 2;
#else
	if (shipping) return 2;
#endif

	constexpr int kWarmup = 128;
	constexpr int kScopesPerFrame = 64;
	constexpr int kGpuSpansPerFrame = 8;
	ce::profiler_service service;
	if (!shipping)
	{
		ce::profiler_config config;
		config.retained_frames = 600;
		service.initialize(config);
		if (!stopped) service.record(1);
	}
	const ce::marker_id cpuMarker = ce::marker<"OverheadCpu">();
	const ce::marker_id gpuMarker = ce::marker<"OverheadGpu", ce::marker_kind::gpu_span>();
	std::uint32_t collectorId = shipping ? 0 : service.summary().collector_os_thread_id;
	for (int attempt = 0; !shipping && collectorId == 0 && attempt < 100; ++attempt)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		collectorId = service.summary().collector_os_thread_id;
	}
	HANDLE collector = collectorId
		? ::OpenThread(THREAD_QUERY_INFORMATION, FALSE, collectorId) : nullptr;

	auto run_frame = [&](std::uint32_t frame)
	{
		std::uint64_t value = frame + 0x9E3779B97F4A7C15ull;
		for (int i = 0; i < kScopesPerFrame; ++i)
		{
			ce::profile_scope scope{ service, cpuMarker };
			value = work_unit(value + static_cast<std::uint64_t>(i));
		}
		if (cpuGpu)
		{
			for (int i = 0; i < kGpuSpansPerFrame; ++i)
			{
				const ce::profile_tick tick = ce::profiler_service::now();
				service.submit_gpu_span(gpuMarker, tick, tick + 1, frame,
					ce::gpu_span_context{ frame, 1, 0 });
			}
			service.publish_gpu_spans();
		}
		service.publish_frame(frame);
		sink.fetch_xor(value, std::memory_order_relaxed);
	};

	for (int frame = 1; frame <= kWarmup; ++frame)
	{
		run_frame(static_cast<std::uint32_t>(frame));
		pace_collector();
	}
	if (!shipping && !stopped)
	{
		service.wait_until_idle();
		service.clear();
		service.record(kWarmup + 1);
	}
	const std::uint64_t collectorCpuBefore = thread_cpu_100ns(collector);
	std::vector<double> activeMs;
	activeMs.reserve(static_cast<std::size_t>(frames));
	std::size_t peakCaptureBytes = 0;
	std::uint32_t peakChunks = 0;
	const auto began = clock_type::now();
	for (int i = 0; i < frames; ++i)
	{
		const auto activeBegan = clock_type::now();
		run_frame(static_cast<std::uint32_t>(kWarmup + 1 + i));
		const auto activeEnded = clock_type::now();
		activeMs.push_back(std::chrono::duration<double, std::milli>(activeEnded - activeBegan).count());
		if (!shipping && (i & 63) == 0)
		{
			const ce::live_summary sample = service.summary();
			peakCaptureBytes = (std::max)(peakCaptureBytes, sample.memory_bytes);
			peakChunks = (std::max)(peakChunks, sample.chunk_count);
		}
		pace_collector();
	}
	if (!shipping && !stopped)
	{
		service.wait_until_idle();
		service.pause();
		service.wait_until_idle();
	}
	const auto ended = clock_type::now();
	const std::uint64_t collectorCpuAfter = thread_cpu_100ns(collector);
	if (collector) ::CloseHandle(collector);
	const ce::live_summary result = shipping ? ce::live_summary{} : service.summary();
	peakCaptureBytes = (std::max)(peakCaptureBytes, result.memory_bytes);
	peakChunks = (std::max)(peakChunks, result.chunk_count);
	if (!shipping) service.shutdown();
	const double elapsed = std::chrono::duration<double>(ended - began).count();
	const double meanMs = [&]()
	{
		double total = 0;
		for (double value : activeMs) total += value;
		return total / activeMs.size();
	}();
	const std::uint64_t scopeCalls = static_cast<std::uint64_t>(frames)
		* static_cast<std::uint64_t>(kScopesPerFrame);
	const std::uint64_t emittedEvents = (shipping || stopped) ? 0 :
		static_cast<std::uint64_t>(frames)
		* static_cast<std::uint64_t>(2 * kScopesPerFrame + (cpuGpu ? kGpuSpansPerFrame : 0));
	const bool valid = (shipping || stopped ||
		(result.collector_dropped_frames == 0 && result.dropped_events == 0
		 && result.malformed_pages == 0 && result.collector_queued_frames == 0))
		&& (!shipping || ce::registered_marker_count() == 0)
		&& (shipping || (collectorId != 0 && collector != nullptr));
	std::printf("{\"valid\":%s,\"mode\":\"%.*s\",\"shippingCompileOut\":%s,"
		"\"frames\":%d,\"scopesPerFrame\":%d,\"gpuSpansPerFrame\":%d,"
		"\"elapsedSec\":%.6f,\"activeMeanMs\":%.6f,\"activeP50Ms\":%.6f,"
		"\"activeP95Ms\":%.6f,\"activeP99Ms\":%.6f,\"collectorCpuMs\":%.6f,"
		"\"scopeCallsPerSec\":%.3f,\"emittedEventsPerSec\":%.3f,"
		"\"payloadBytesPerSec\":%.3f,"
		"\"droppedEvents\":%llu,\"droppedFrames\":%llu,\"peakCaptureBytes\":%llu,"
		"\"peakChunks\":%u,\"registeredMarkers\":%u,\"sink\":%llu}\n",
		valid ? "true" : "false", static_cast<int>(mode.size()), mode.data(),
		shipping ? "true" : "false", frames, kScopesPerFrame,
		cpuGpu ? kGpuSpansPerFrame : 0, elapsed, meanMs,
		percentile_ms(activeMs, 0.50), percentile_ms(activeMs, 0.95),
		percentile_ms(activeMs, 0.99),
		static_cast<double>(collectorCpuAfter - collectorCpuBefore) / 10000.0,
		static_cast<double>(scopeCalls) / elapsed,
		static_cast<double>(emittedEvents) / elapsed,
		static_cast<double>(emittedEvents) * sizeof(ce::profile_event) / elapsed,
		static_cast<unsigned long long>(result.dropped_events),
		static_cast<unsigned long long>(result.collector_dropped_frames),
		static_cast<unsigned long long>(peakCaptureBytes), peakChunks,
		ce::registered_marker_count(),
		static_cast<unsigned long long>(sink.load(std::memory_order_relaxed)));
	return valid ? 0 : 1;
}
