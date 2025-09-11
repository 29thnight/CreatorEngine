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
	Meely,
	Range,
	Explosion,
	Basic,
	None,
};
AUTO_REGISTER_ENUM(ItemType)

enum class BuffType
{
	Meely,
	Range,
	Explosion,
	None,
};
AUTO_REGISTER_ENUM(BuffType)

class Entity;
struct AttackContext
{
	Entity* target = nullptr;
	Mathf::Vector3 targetPosition = {};
};