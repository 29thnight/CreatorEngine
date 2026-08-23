#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "AssetEntry.h"

struct AssetBundle
{
   public:
   static consteval auto reflect()
   {
       using Self = AssetBundle;
       return meta::schema<Self>(
           meta::field<&Self::name>,
           meta::field<&Self::path>,
           meta::field<&Self::assets>);
   }
	AssetBundle() = default;
	AssetBundle(const std::string& name, const file::path& path)
		: name(name), path(path.string()) {}
	AssetBundle(const std::string& name, const std::string& path)
		: name(name), path(path) {
	}
	~AssetBundle() = default;

	void AddAsset(const AssetEntry& assetEntry)
	{
		if (-1 != assetEntry.assetTypeID)
		{
			assets.push_back(assetEntry);
		}
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

	std::string name; // Name of the asset bundle
	std::string path; // Path to the asset bundle file
	std::vector<AssetEntry> assets; // List of assets contained in the bundle
};
