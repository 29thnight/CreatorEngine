#pragma once

#include "Core.Minimal.h"
#include "TypeTrait.h"

#include <memory>
#include <span>
#include <string_view>

class Material;
class VolumeProfile;
struct TerrainAuthoringRequest;
struct TerrainAuthoringResult;
struct TextAssetAuthoringRequest;
struct TextAssetAuthoringResult;
struct UncatalogedAuthoringRequest;

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

	FileGuid CreateMeta(const file::path& filepath,
		const FileGuid& preferredGuid = {});
	FileGuid WriteTextAssetWithMeta(const file::path& destination,
		std::string_view payload, const FileGuid& preferredGuid);
	// target과 sidecar를 같은 transaction으로 옮긴다. 성공 시 기존 GUID를
	// 반환하며, 새 GUID를 발급하는 fallback은 두지 않는다.
	FileGuid RenameAsset(const file::path& source, const file::path& destination);
	bool WriteModelCache(const file::path& destination,
		std::span<const std::byte> bytes);
	bool WriteEmbeddedTexture(const file::path& destination,
		std::span<const std::byte> bytes, uint32 width, uint32 height);
	bool WriteTerrain(const TerrainAuthoringRequest& request,
		TerrainAuthoringResult& result);
	bool WriteFoliage(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result);
	bool WriteBlackBoard(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result);
	bool WriteCollisionMatrix(const UncatalogedAuthoringRequest& request);
	bool WriteTagManager(const UncatalogedAuthoringRequest& request);
	bool WriteInputActionMap(const UncatalogedAuthoringRequest& request);
	bool WriteAnimatorController(const UncatalogedAuthoringRequest& request);
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
