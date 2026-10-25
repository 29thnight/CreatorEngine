#pragma once
#include "../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Foundation/Core.Minimal.h"

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
