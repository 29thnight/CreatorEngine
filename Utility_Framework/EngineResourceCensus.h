#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

// 엔진이 직접 세는 리소스 카운터.
//
// D3D11 디버그 레이어의 ReportLiveDeviceObjects는 실행 중에 부르면 커맨드 리스트
// 재활용 상태를 망가뜨려 이후 렌더에서 죽는다(DeviceResources.h 주석 참고).
// 그래서 런타임 집계는 디버그 레이어를 쓰지 않고 여기서 직접 센다.
//
// 세는 대상은 "수명이 씬/에셋을 따라 움직이는 것"으로 한정한다. 각 렌더 패스가
// 초기화 때 한 번 만들고 끝까지 들고 있는 상수 버퍼 같은 것은 누수 관심사가
// 아니라서 넣지 않는다. 씬을 오갔을 때 제자리로 돌아와야 하는 값만 남긴다.
//
// 릴리스 빌드에서도 동작한다(원자적 증감 한 번이 전부).
namespace Diagnostics
{
	enum class EngineResource : size_t
	{
		Texture,
		Mesh,
		Material,
		Model,
		Count
	};

	inline constexpr std::array<std::string_view, static_cast<size_t>(EngineResource::Count)> kEngineResourceNames
	{
		"Texture",
		"Mesh",
		"Material",
		"Model"
	};

	namespace Detail
	{
		// inline 변수라 번역 단위마다 복사되지 않는다.
		// (DLL 경계를 넘으면 별개가 되므로, 세는 쪽과 읽는 쪽은 같은 모듈에 있어야 한다)
		inline std::array<std::atomic<int64_t>, static_cast<size_t>(EngineResource::Count)> g_liveCounts{};
	}

	inline void TrackCreate(EngineResource type) noexcept
	{
		Detail::g_liveCounts[static_cast<size_t>(type)].fetch_add(1, std::memory_order_relaxed);
	}

	inline void TrackDestroy(EngineResource type) noexcept
	{
		Detail::g_liveCounts[static_cast<size_t>(type)].fetch_sub(1, std::memory_order_relaxed);
	}

	inline int64_t LiveCount(EngineResource type) noexcept
	{
		return Detail::g_liveCounts[static_cast<size_t>(type)].load(std::memory_order_relaxed);
	}

	struct ResourceSnapshot
	{
		std::array<int64_t, static_cast<size_t>(EngineResource::Count)> counts{};

		int64_t Total() const noexcept
		{
			int64_t total = 0;
			for (const int64_t count : counts) total += count;
			return total;
		}
	};

	inline ResourceSnapshot CaptureResourceSnapshot() noexcept
	{
		ResourceSnapshot snapshot{};
		for (size_t i = 0; i < snapshot.counts.size(); ++i)
		{
			snapshot.counts[i] = Detail::g_liveCounts[i].load(std::memory_order_relaxed);
		}
		return snapshot;
	}

	// "Texture:12, Mesh:3, ..." 형태로 만든다. 0인 항목은 생략한다.
	inline std::string FormatSnapshot(const ResourceSnapshot& snapshot)
	{
		std::string text;
		for (size_t i = 0; i < snapshot.counts.size(); ++i)
		{
			if (snapshot.counts[i] == 0) continue;
			if (!text.empty()) text += ", ";
			text += std::string(kEngineResourceNames[i]) + ":" + std::to_string(snapshot.counts[i]);
		}
		return text.empty() ? std::string("없음") : text;
	}

	// "Texture:12(+3), Mesh:3(0), ..." 형태. 증감이 0이어도 현재값은 보여준다.
	inline std::string FormatDelta(const ResourceSnapshot& current, const ResourceSnapshot& baseline)
	{
		std::string text;
		for (size_t i = 0; i < current.counts.size(); ++i)
		{
			const int64_t delta = current.counts[i] - baseline.counts[i];
			if (current.counts[i] == 0 && delta == 0) continue;

			if (!text.empty()) text += ", ";
			text += std::string(kEngineResourceNames[i]) + ":" + std::to_string(current.counts[i]);
			text += (delta > 0) ? "(+" + std::to_string(delta) + ")"
				 : (delta < 0) ? "(" + std::to_string(delta) + ")"
				 : "(0)";
		}
		return text.empty() ? std::string("없음") : text;
	}

	// 상속만 하면 생성자가 몇 개든 정확히 세어진다.
	// 복사·이동도 각각 새 인스턴스이므로 따로 증가시킨다(원본은 자기 소멸자에서 감소).
	template <EngineResource Type>
	struct CountedResource
	{
		CountedResource() noexcept { TrackCreate(Type); }
		CountedResource(const CountedResource&) noexcept { TrackCreate(Type); }
		CountedResource(CountedResource&&) noexcept { TrackCreate(Type); }
		CountedResource& operator=(const CountedResource&) noexcept { return *this; }
		CountedResource& operator=(CountedResource&&) noexcept { return *this; }
		~CountedResource() noexcept { TrackDestroy(Type); }
	};
}
