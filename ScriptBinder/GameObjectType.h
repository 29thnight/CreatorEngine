#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class GameObjectType
{
	Empty,
	Camera,
	Light,
	Mesh,
	Bone,
	UI,
	Canvas,
	TypeMax
};
AUTO_REGISTER_ENUM(GameObjectType)