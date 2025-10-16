#pragma once
#include "Core.Minimal.h"
#include <variant>

enum class ItemEnhancementType
{
	None = 0,
	MaxHPUp = 1,
	MoveSpeedUp = 2,
	AtkSpeedUp = 3,
	Atk = 4,
	DashCountUp = 5,
	WeaponDurabilityUp = 6,
	ThrowRangeUp = 7,
	Max,
};

enum class ItemRarity
{
	Common = 0,
	Rare = 1,
	Epic = 2,
};

enum class EnhancementCalcType
{
	Add = 0,
	Mul = 1,
};

constexpr int MAX_ENHANCEMENT_TYPE = static_cast<int>(ItemEnhancementType::Max);

struct ItemInfo
{
	int id{ -1 };
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
		// �����ϰ� xor ����
		return h1 ^ (h2 << 1);
	}
};

// ���Ⱥ� value_type / calc ��å�� "������ Ÿ��"�� ����
template<ItemEnhancementType T>
struct StatMeta;

template<> struct StatMeta<ItemEnhancementType::MaxHPUp> { using value_type = int;   static constexpr EnhancementCalcType calc = EnhancementCalcType::Add; };
template<> struct StatMeta<ItemEnhancementType::Atk> { using value_type = int;   static constexpr EnhancementCalcType calc = EnhancementCalcType::Add; };
template<> struct StatMeta<ItemEnhancementType::DashCountUp> { using value_type = int;   static constexpr EnhancementCalcType calc = EnhancementCalcType::Add; };
template<> struct StatMeta<ItemEnhancementType::WeaponDurabilityUp> { using value_type = int;   static constexpr EnhancementCalcType calc = EnhancementCalcType::Add; };
template<> struct StatMeta<ItemEnhancementType::MoveSpeedUp> { using value_type = float; static constexpr EnhancementCalcType calc = EnhancementCalcType::Mul; };
template<> struct StatMeta<ItemEnhancementType::AtkSpeedUp> { using value_type = float; static constexpr EnhancementCalcType calc = EnhancementCalcType::Mul; };
template<> struct StatMeta<ItemEnhancementType::ThrowRangeUp> { using value_type = float;   static constexpr EnhancementCalcType calc = EnhancementCalcType::Add; };

constexpr EnhancementCalcType CalcOf(ItemEnhancementType t) 
{
	switch (t) 
	{
	case ItemEnhancementType::MoveSpeedUp:
	case ItemEnhancementType::AtkSpeedUp:
		return EnhancementCalcType::Mul;
	case ItemEnhancementType::MaxHPUp:
	case ItemEnhancementType::Atk:
	case ItemEnhancementType::DashCountUp:
	case ItemEnhancementType::WeaponDurabilityUp:
	case ItemEnhancementType::ThrowRangeUp:
	default:
		return EnhancementCalcType::Add;
	}
}

// ����(Concepts): ���� T�� �´� �� Ÿ�Ը� ���
template<ItemEnhancementType T, typename V>
concept ValueMatchesStat = std::same_as<V, typename StatMeta<T>::value_type>;

// ����
constexpr inline bool IsFloatStat(ItemEnhancementType t) {
	return t == ItemEnhancementType::MoveSpeedUp || t == ItemEnhancementType::AtkSpeedUp || t == ItemEnhancementType::ThrowRangeUp;
}

inline bool IsFloatStat(int t) 
{
	ItemEnhancementType et = static_cast<ItemEnhancementType>(t);
	return et == ItemEnhancementType::MoveSpeedUp || et == ItemEnhancementType::AtkSpeedUp || et == ItemEnhancementType::ThrowRangeUp;
}

template<ItemEnhancementType T>
struct EnhancementDelta 
{
	using value_type = typename StatMeta<T>::value_type;
	static constexpr ItemEnhancementType type = T;
	static constexpr EnhancementCalcType calc = StatMeta<T>::calc;

	value_type value{};  // T�� �´� Ÿ�Ը� ��

	// ���� ������
	template<typename V> requires ValueMatchesStat<T, V>
	static constexpr EnhancementDelta Make(V v) { return EnhancementDelta{ v }; }
};

// ������+���Ƽ�� �ϳ��� Ű��
using SourceKey = uint64_t;
inline SourceKey MakeSourceKey(int itemId, int rarity) {
	return (static_cast<uint64_t>(static_cast<uint32_t>(itemId)) << 32) |
		static_cast<uint32_t>(rarity);
}

// ��� ���� ��Ÿ�� variant�� ����(�ʿ��� Ÿ�Ը� ����)
using AnyDelta = std::variant<
	EnhancementDelta<ItemEnhancementType::MaxHPUp>,
	EnhancementDelta<ItemEnhancementType::MoveSpeedUp>,
	EnhancementDelta<ItemEnhancementType::AtkSpeedUp>,
	EnhancementDelta<ItemEnhancementType::Atk>,
	EnhancementDelta<ItemEnhancementType::DashCountUp>,
	EnhancementDelta<ItemEnhancementType::WeaponDurabilityUp>,
	EnhancementDelta<ItemEnhancementType::ThrowRangeUp>
>;