#pragma once

#include "Core.Minimal.h"
#include "TypeTrait.h"

#include <memory>
#include <span>
#include <string_view>

class Material;
class VolumeProfile;

// Editor-owned source asset database. It owns watcher/meta generation and all
// authoring writes extracted from DataSystem; Core retains read-only catalog use.
class EditorAssetDatabase final
{
public:
	enum class ImportKind
	{
		Model,
		Texture,
		MaterialTexture,
		TerrainTexture,
		HDR,
		UITexture,
		SpriteSheet,
	};

	static EditorAssetDatabase& Get() noexcept;

	~EditorAssetDatabase();

	bool Initialize();
	void Shutdown() noexcept;
	bool IsInitialized() const noexcept;

	FileGuid CreateMeta(const file::path& filepath);
	bool WriteModelCache(const file::path& destination,
		std::span<const std::byte> bytes);
	bool WriteEmbeddedTexture(const file::path& destination,
		std::span<const std::byte> bytes, uint32 width, uint32 height);
	bool CopyTerrainTexture(const file::path& source,
		const file::path& destination);
	file::path ImportSourceAsset(const file::path& source, ImportKind kind);
	bool IsSupportExtension(std::string_view extension) const;
	bool SaveMaterial(Material* material);
	bool CreateVolumeProfile(const file::path& directory);
	bool SaveExistingVolumeProfile(FileGuid guid, VolumeProfile* volume);

private:
	EditorAssetDatabase() = default;
	EditorAssetDatabase(const EditorAssetDatabase&) = delete;
	EditorAssetDatabase& operator=(const EditorAssetDatabase&) = delete;

	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
