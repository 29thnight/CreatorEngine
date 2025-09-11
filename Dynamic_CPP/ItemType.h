#pragma once
#include "Core.Minimal.h"
enum class EItemType
{
	Basic,
	Mushroom,  //meely
	Mineral,   // range
	Fruit,    //bomb
	Flower,   //heal �����ƴϰ� �ٸ������� 
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

constexpr ItemType EToIMap[] = {
	ItemType::Basic,   
	ItemType::Meely,   
	ItemType::Range,   
	ItemType::Bomb,   
	ItemType::None    
};



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