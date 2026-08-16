#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class ManagedAssetType
{
	Model,
	Material,
	Texture,
	SpriteFont,
	// 아래 두 종은 나중에 추가되었다.
	// assetTypeID가 정수로 직렬화되므로 기존 값의 순서를 바꾸면 에셋 번들 파일과
	// 호환이 깨진다. 새 항목은 반드시 끝에만 추가할 것.
	UITexture,
	SpriteSheet
};
AUTO_REGISTER_ENUM(ManagedAssetType);

struct AssetEntry
{
   static consteval auto describe()
   {
       return meta::describe<AssetEntry>(
           meta::member<&AssetEntry::assetTypeID>(),
           meta::member<&AssetEntry::assetName>());
   }
	AssetEntry() = default;
	AssetEntry(ManagedAssetType assetTypeID, const file::path& assetName)
		: assetTypeID((int)assetTypeID), assetName(assetName.string()) {
	}
	~AssetEntry() = default;
	AssetEntry(const AssetEntry&) = default;
	AssetEntry(AssetEntry&&) noexcept = default;

	AssetEntry& operator=(const AssetEntry&) = default;
	AssetEntry& operator=(AssetEntry&&) noexcept = default;

	void Clear()
	{
		assetTypeID = -1;
		assetName.clear();
	}

	int assetTypeID{ -1 };
	std::string assetName{};

	friend auto operator<=>(const AssetEntry& lhs, const AssetEntry& rhs)
	{
		return std::tie(lhs.assetTypeID, lhs.assetName)
			<=> std::tie(rhs.assetTypeID, rhs.assetName);
	}

	friend bool operator==(const AssetEntry& lhs, const AssetEntry& rhs)
	{
		return lhs.assetTypeID == rhs.assetTypeID && lhs.assetName == rhs.assetName;
	}
};
