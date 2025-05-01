#pragma once
#include "Core.Minimal.h"

enum class GameObjectType
{
	Empty,
	Camera,
	Light,
	Mesh,
	Bone,
	TypeMax
};
AUTO_REGISTER_ENUM(GameObjectType)