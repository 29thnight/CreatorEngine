#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include <mathematics/vector2.hpp>
#include <type_traits>

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

// 값은 플래그 조합(가로|세로 = 0x3)으로 들어올 수 있다 — enum class라
// 명명된 상수 밖의 값도 유효한 상태다.
constexpr bool HasUIEffect(UIEffects value, UIEffects flag) noexcept
{
	return 0 != (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag));
}

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

// ImageComponent/SpriteSheetComponent가 실제로 보존하는 것은 원본 텍스처의
// 픽셀 크기뿐이다. world/screenSize는 생산자와 소비자가 모두 없었고 이 구조를
// GPU constant buffer로 올리는 경로도 없으므로 CPU 값 DTO로 명시한다.
struct ImageInfo
{
	math::vector2 size{};
};

static_assert(std::is_same_v<decltype(ImageInfo::size), math::vector2>);
static_assert(std::is_standard_layout_v<ImageInfo>);
static_assert(std::is_trivially_copyable_v<ImageInfo>);
static_assert(sizeof(ImageInfo) == sizeof(float) * 2u);

enum class TextAlignment : std::uint8_t
{
	Left,
	Center,
};
