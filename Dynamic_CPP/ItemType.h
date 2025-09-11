#pragma once
#include "Core.Minimal.h"
enum class EItemType
{
	Mushroom,
	Mineral,
	Fruit,
};
AUTO_REGISTER_ENUM(EItemType)

enum class ItemType
{
	None,
	Basic,
	Melee,
	Range,
	Bomb,
};
AUTO_REGISTER_ENUM(ItemType)

enum class BuffType
{
	None,
	Melee,
	Range,
	Explosion,
};
AUTO_REGISTER_ENUM(BuffType)

class Entity;
struct AttackContext
{
	Entity* target = nullptr;
	Mathf::Vector3 targetPosition = {};
};