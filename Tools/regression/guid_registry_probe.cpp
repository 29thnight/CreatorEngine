// 인스턴스 GUID 장부(TypeTrait::GUIDCreator) 프로브.
//
// 장부는 원래 TypeTrait.h 의 `static std::set<HashedGuid> g_guids;` 였다 — TU 마다
// 사본이 있었고 잠금이 없었다(docs/analysis/PlayerModuleBoundaryAnalysis.md §10.6).
// 지금은 TypeTrait.cpp 의 표 하나에 잠금을 건다. 이 프로브가 재는 것:
//
//   ① 동시 발급 유일성 — 두 TU 의 여러 스레드가 동시에 MakeGUID 를 부르고, 다른
//      스레드가 같은 표를 넣고 지우며 흔드는 동안, 발급된 값이 하나도 겹치지 않는다.
//      잠금이 없으면 std::set 을 여러 스레드가 고치다 표가 깨지거나 죽는다.
//   ② 비용 — §10.6 이 "락 비용을 재고(Release 기준) 진행할 것" 이라고 적었다.
//      CoCreateGuid 만의 비용과 MakeGUID(표 + 잠금) 비용, 경합 아래 비용을 INFO 로
//      낸다. 판정에는 넣지 않는다 — 기계마다 다른 값이다.
//
// "정의가 TypeTrait.cpp 하나에만 있다" 는 구조는 스크립트가 따로 본다(이 파일과
// peer 를 TypeTrait.cpp 없이 링크하면 실패해야 한다).
//
// 판정은 종료 코드와 표식 GUID_REGISTRY_OK 다.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>

#include "TypeTrait.h"

namespace guid_probe
{
	void make_from_peer(std::size_t count, std::vector<HashedGuid>& out);
	void churn_from_peer(std::size_t count);
}

namespace
{
	int g_checks = 0;
	int g_failures = 0;

	void check(bool condition, const char* what)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::fprintf(stderr, "FAIL: %s\n", what);
		}
	}

	constexpr std::size_t kThreads = 8;
	constexpr std::size_t kPerThread = 20000;
	constexpr std::size_t kChurnThreads = 2;
	constexpr std::size_t kTimingCalls = 100000;

	double nanoseconds_per_call(std::chrono::steady_clock::duration elapsed, std::size_t calls)
	{
		return std::chrono::duration<double, std::nano>(elapsed).count() / static_cast<double>(calls);
	}

	// ① 동시 발급 유일성.
	void test_concurrent_uniqueness()
	{
		std::vector<std::vector<HashedGuid>> issued(kThreads);
		std::atomic<bool> go{ false };
		std::vector<std::thread> workers;
		workers.reserve(kThreads + kChurnThreads);

		for (std::size_t t = 0; t < kThreads; ++t)
		{
			workers.emplace_back([&issued, &go, t]
			{
				while (!go.load(std::memory_order_acquire)) { std::this_thread::yield(); }
				// 반은 이 TU 에서, 반은 peer TU 에서 부른다.
				if (t % 2 == 0)
				{
					issued[t].reserve(kPerThread);
					for (std::size_t i = 0; i < kPerThread; ++i)
					{
						issued[t].push_back(TypeTrait::GUIDCreator::MakeGUID());
					}
				}
				else
				{
					guid_probe::make_from_peer(kPerThread, issued[t]);
				}
			});
		}
		for (std::size_t t = 0; t < kChurnThreads; ++t)
		{
			workers.emplace_back([&go]
			{
				while (!go.load(std::memory_order_acquire)) { std::this_thread::yield(); }
				guid_probe::churn_from_peer(kPerThread);
			});
		}

		go.store(true, std::memory_order_release);
		for (std::thread& worker : workers)
		{
			worker.join();
		}

		std::vector<HashedGuid> all;
		all.reserve(kThreads * kPerThread);
		for (const std::vector<HashedGuid>& part : issued)
		{
			all.insert(all.end(), part.begin(), part.end());
		}
		check(all.size() == kThreads * kPerThread, "uniqueness/count — 모든 발급이 돌아왔다");

		// 발급된 값은 지운 적이 없으므로 표에 남아 있고, 그러니 다시 발급될 수 없다.
		// 겹치는 값이 있다면 확인과 삽입 사이가 열려 있거나 표가 갈린 것이다.
		std::sort(all.begin(), all.end());
		const auto duplicate = std::adjacent_find(all.begin(), all.end());
		check(duplicate == all.end(), "uniqueness/distinct — 동시에 발급된 GUID 가 겹치지 않는다");
	}

	// ② 비용(INFO). 판정에 넣지 않는다.
	void measure_cost()
	{
		std::size_t sink = 0;

		auto start = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < kTimingCalls; ++i)
		{
			sink += ConvertGUIDToHash(GenerateGUID());
		}
		const double generateOnly = nanoseconds_per_call(std::chrono::steady_clock::now() - start, kTimingCalls);

		start = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < kTimingCalls; ++i)
		{
			sink += static_cast<std::size_t>(TypeTrait::GUIDCreator::MakeGUID());
		}
		const double makeSingle = nanoseconds_per_call(std::chrono::steady_clock::now() - start, kTimingCalls);

		std::atomic<bool> go{ false };
		std::vector<std::thread> workers;
		workers.reserve(kThreads);
		for (std::size_t t = 0; t < kThreads; ++t)
		{
			workers.emplace_back([&go]
			{
				while (!go.load(std::memory_order_acquire)) { std::this_thread::yield(); }
				for (std::size_t i = 0; i < kPerThread; ++i)
				{
					(void)TypeTrait::GUIDCreator::MakeGUID();
				}
			});
		}
		start = std::chrono::steady_clock::now();
		go.store(true, std::memory_order_release);
		for (std::thread& worker : workers)
		{
			worker.join();
		}
		const double makeContended = nanoseconds_per_call(std::chrono::steady_clock::now() - start, kThreads * kPerThread);

		std::printf("INFO: generate_only_ns=%.1f\n", generateOnly);
		std::printf("INFO: make_guid_single_ns=%.1f\n", makeSingle);
		std::printf("INFO: make_guid_contended_ns=%.1f (threads=%zu, wall/total)\n", makeContended, kThreads);
		std::printf("INFO: sink=%zu\n", sink % 10);
	}
}

int main()
{
	test_concurrent_uniqueness();
	measure_cost();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	if (g_failures != 0)
	{
		return 1;
	}
	std::printf("GUID_REGISTRY_OK=true\n");
	return 0;
}
