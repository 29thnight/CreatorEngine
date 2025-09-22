#pragma once
#include "Core.Minimal.h"

enum class ItemEnhancementType
{
	None = 0,
	MaxHPUp = 1,
	MoveSpeedUp = 2,
	AtkSpeedUp = 3,
	Atk = 4,
	DashCountUp = 5,
	WeaponDurabilityUp = 6,
};

struct ItemInfo
{
	int id{};
	int rarity{};
	std::string name{};
	std::string description{};
	int price{};
	int enhancementType{};
	int enhancementValue{};
};

using ItemUniqueID = std::pair<int, int>; // first: item id, second: rarity

struct ItemUniqueIDHash {
	std::size_t operator()(const ItemUniqueID& key) const noexcept {
		std::size_t h1 = std::hash<int>{}(key.first);
		std::size_t h2 = std::hash<int>{}(key.second);
		// 간단하게 xor 조합
		return h1 ^ (h2 << 1);
	}
};