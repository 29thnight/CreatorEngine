#pragma once
#include "Core.Minimal.h"
enum class EItemType
{
	Basic,
	Mushroom,  //meely
	Mineral,   // range
	Fruit,    //bomb
	Flower,   //heal 웨폰아니고 다른곳으로 
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

constexpr ItemType EToIMap[] = {
	ItemType::Basic,   
	ItemType::Melee,
	ItemType::Range,   
	ItemType::Bomb,   
	ItemType::None    
};


enum class PlayerType
{
	Male,
	Female,
};
AUTO_REGISTER_ENUM(PlayerType)


enum class BulletType
{
	Normal,
	Special,
};



class Entity;
//아마 안쓰는대 확인후 삭제예정
struct AttackContext
{
	Entity* target = nullptr;
	Mathf::Vector3 targetPosition = {};
};