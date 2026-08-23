#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class Direction
{
	Up,
	Down,
	Left,
	Right
};

enum class ClipDirection : std::uint8_t
{
	None,
	LeftToRight,
	RightToLeft,
	TopToBottom,
	BottomToTop
};

enum class UIEffects
{
	UIEffects_None = 0x0,
	UIEffects_FlipHorizontally = 0x1,
	UIEffects_FlipVertically = 0x2
};

struct Navigation
{
   public:
   static consteval auto reflect()
   {
       using Self = Navigation;
       return meta::schema<Self>(
           meta::field<&Self::mode>,
           meta::field<&Self::parentHops>,
           meta::field<&Self::childOrdinals>);
   }
	int mode{};

	// U7 — 전역 instanceID 대신 소스 UI를 기준으로 한 계층 로컬 경로를 저장한다.
	//
	// parentHops만큼 부모로 올라간 뒤 childOrdinals의 순서대로 살아 있는 자식을
	// 내려가면 대상이다. 같은 프리팹을 두 번 소환해도 해석 시작점이 각 인스턴스의
	// 소스 UI이므로 다른 인스턴스로 샐 수 없다. 씬 저장과 중첩 계층도 같은 표현을
	// 쓴다 — "프리팹일 때만 뜻이 있는 평면 인덱스"보다 적용 범위가 넓다.
	static constexpr uint32_t InvalidParentHops = (std::numeric_limits<uint32_t>::max)();
	uint32_t parentHops{ InvalidParentHops };
	std::vector<uint32_t> childOrdinals{};

	bool HasTarget() const noexcept { return parentHops != InvalidParentHops; }

	bool operator==(const Navigation& other) const
	{
		return mode == other.mode
			&& parentHops == other.parentHops
			&& childOrdinals == other.childOrdinals;
	}

	bool operator!=(const Navigation& other) const
	{
		return !(*this == other);
	}

	Navigation() = default;
	~Navigation() = default;
};

constexpr int NavDirectionCount = 4;

cbuffer ImageInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
};

enum class TextAlignment : std::uint8_t
{
	Left,
	Center,
};
