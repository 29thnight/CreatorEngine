#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Navigation.generated.h"

enum class Direction
{
	Up,
	Down,
	Left,
	Right
};
AUTO_REGISTER_ENUM(Direction)

struct Navigation
{
	[[Property]]
	Direction mode = Direction::Right;
	[[Property]]
	HashedGuid navObject{};

	bool operator==(const Navigation& other) const
	{
		return mode == other.mode && navObject == other.navObject;
	}

	bool operator!=(const Navigation& other) const
	{
		return !(*this == other);
	}

   ReflectNavigation
	[[Serializable]]
	Navigation() = default;
	~Navigation() = default;
};

constexpr int NavDirectionCount = 4;
