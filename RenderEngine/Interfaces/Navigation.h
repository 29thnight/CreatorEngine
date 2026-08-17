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
	int mode{};
	HashedGuid navObject{};

	bool operator==(const Navigation& other) const
	{
		return mode == other.mode && navObject == other.navObject;
	}

	bool operator!=(const Navigation& other) const
	{
		return !(*this == other);
	}

   static consteval auto describe()
   {
       return meta::describe<Navigation>(
           meta::member<&Navigation::mode>(),
           meta::member<&Navigation::navObject>());
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
