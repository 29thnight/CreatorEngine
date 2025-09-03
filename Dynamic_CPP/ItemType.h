#pragma once
#include "Core.Minimal.h"
enum class EItemType
{
	Mushroom,
	Mineral,
	Fruit,
};


enum class ItemType
{
	Meely,
	Range,
	Explosion,
	Basic,
	None,
};

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