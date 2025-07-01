#pragma once
#include "AssetEntry.h"
#include "AssetBundle.generated.h"

struct AssetBundle
{
   ReflectAssetBundle
	[[Serializable]]
	AssetBundle() = default;
	AssetBundle(const std::string& name, const file::path& path)
		: name(name), path(path) {}
	~AssetBundle() = default;

	void AddAsset(const AssetEntry& assetEntry)
	{
		assets.push_back(assetEntry);
	}

	void RemoveAsset(const AssetEntry& assetEntry)
	{
		auto it = std::remove(assets.begin(), assets.end(), assetEntry);
		if (it != assets.end())
		{
			assets.erase(it, assets.end());
		}
	}

	bool ContainsAsset(const AssetEntry& assetEntry) const
	{
		return std::find(assets.begin(), assets.end(), assetEntry) != assets.end();
	}

	void ClearAssets()
	{
		assets.clear();
	}

	[[Property]]
	std::string name; // Name of the asset bundle
	[[Property]]
	file::path path; // Path to the asset bundle file
	[[Property]]
	std::vector<AssetEntry> assets; // List of assets contained in the bundle
};
