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
	Basic,
	Meely,
	Range,
	Bomb,
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


class Entity;
struct AttackContext
{
	Entity* target = nullptr;
	Mathf::Vector3 targetPosition = {};
};