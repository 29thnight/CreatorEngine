#include "SerializationProfiler.h"

#include <chrono>

namespace
{
	using Clock = std::chrono::steady_clock;

	std::atomic<bool> g_enabled{ false };

	// 원자 배열 두 벌. 씬 로드는 메인 스레드지만 저장/소환 경로가 워커에서 불릴 수
	// 있으므로 relaxed 누산으로 둔다 — 판정은 로드 완료 뒤 한 번만 읽는다.
	std::array<std::atomic<uint64_t>, SerializationProfile::kStageCount> g_nanos{};
	std::array<std::atomic<uint64_t>, SerializationProfile::kStageCount> g_calls{};

	// Reset이 지우지 않는 부팅 슬롯. CLI가 켜기 전에 이미 끝난 구간을 잃지 않는다.
	std::array<std::atomic<uint64_t>, SerializationProfile::kStageCount> g_bootNanos{};
	std::array<std::atomic<uint64_t>, SerializationProfile::kStageCount> g_bootCalls{};
}

namespace SerializationProfile
{
	std::string_view StageName(Stage stage)
	{
		switch (stage)
		{
		case Stage::SceneLoadTotal:    return "SceneLoadTotal";
		case Stage::SceneParse:        return "SceneParse";
		case Stage::EntityDeserialize: return "EntityDeserialize";
		case Stage::ComponentLoad:     return "ComponentLoad";
		case Stage::AssetCatalog:      return "AssetCatalog";
		case Stage::PrefabInstantiate: return "PrefabInstantiate";
		case Stage::PrefabParse:       return "PrefabParse";
		default:                       return "Unknown";
		}
	}

	bool IsSceneLoadChild(Stage stage)
	{
		// SceneLoadTotal 안에서만 발생하는 단계들. AssetCatalog는 부팅 구간이고
		// PrefabInstantiate는 자기 자신이 루트이므로 제외한다.
		return stage == Stage::SceneParse
			|| stage == Stage::EntityDeserialize
			|| stage == Stage::ComponentLoad;
	}

	void SetEnabled(bool enabled)
	{
		g_enabled.store(enabled, std::memory_order_release);
	}

	bool IsEnabled()
	{
		return g_enabled.load(std::memory_order_acquire);
	}

	void Reset()
	{
		for (uint32_t i = 0; i < kStageCount; ++i)
		{
			g_nanos[i].store(0, std::memory_order_relaxed);
			g_calls[i].store(0, std::memory_order_relaxed);
		}
	}

	Snapshot Take()
	{
		Snapshot snapshot{};
		for (uint32_t i = 0; i < kStageCount; ++i)
		{
			snapshot.stages[i].nanoseconds = g_nanos[i].load(std::memory_order_relaxed);
			snapshot.stages[i].calls = g_calls[i].load(std::memory_order_relaxed);
		}
		return snapshot;
	}

	void RecordBootStage(Stage stage, uint64_t nanoseconds, uint64_t calls)
	{
		const uint32_t index = static_cast<uint32_t>(stage);
		if (index >= kStageCount) return;
		g_bootNanos[index].fetch_add(nanoseconds, std::memory_order_relaxed);
		g_bootCalls[index].fetch_add(calls, std::memory_order_relaxed);
	}

	Snapshot TakeBoot()
	{
		Snapshot snapshot{};
		for (uint32_t i = 0; i < kStageCount; ++i)
		{
			snapshot.stages[i].nanoseconds = g_bootNanos[i].load(std::memory_order_relaxed);
			snapshot.stages[i].calls = g_bootCalls[i].load(std::memory_order_relaxed);
		}
		return snapshot;
	}

	void AddSample(Stage stage, uint64_t nanoseconds)
	{
		const uint32_t index = static_cast<uint32_t>(stage);
		if (index >= kStageCount) return;
		g_nanos[index].fetch_add(nanoseconds, std::memory_order_relaxed);
		g_calls[index].fetch_add(1, std::memory_order_relaxed);
	}

	Scope::Scope(Stage stage)
		: m_stage(stage)
		, m_active(IsEnabled())
		, m_startTicks(0)
	{
		if (!m_active) return;
		m_startTicks = static_cast<uint64_t>(
			Clock::now().time_since_epoch().count());
	}

	Scope::~Scope()
	{
		if (!m_active) return;
		const uint64_t end = static_cast<uint64_t>(
			Clock::now().time_since_epoch().count());
		// steady_clock의 period가 나노초가 아닐 수 있으므로 환산한다.
		const auto elapsed = Clock::duration(
			static_cast<Clock::rep>(end - m_startTicks));
		const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
		AddSample(m_stage, static_cast<uint64_t>(nanos.count()));
	}
}
