#pragma once

#include "Core.Minimal.h"
#include "TypeTrait.h"

#include <memory>
#include <cstdint>
#include <chrono>
#include <optional>
#include <span>
#include <string_view>

class Material;
namespace LX { class LXMaterialAsset; class LXInputAsset; }
class SceneRenderProfile;
struct TerrainAuthoringRequest;
struct TerrainAuthoringResult;
struct TextAssetAuthoringRequest;
struct TextAssetAuthoringResult;
struct UncatalogedAuthoringRequest;
namespace ce::physics { struct CollisionGeometrySource; }
namespace experiment::cooked { struct TextureImportSettings; }
namespace Authoring { class WriteNode; }

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
    // Owner-thread polling; only prepared replacements touch live scene references.
    void TickAutomaticChanges();
    void FlushAutomaticSaves();
    // Thread-safe diagnostic snapshot; absent means this sidecar has no queued save.
    [[nodiscard]] std::optional<std::chrono::milliseconds> PendingAutomaticSaveAge(const file::path& sidecar) const;
    [[nodiscard]] std::string AutomaticSaveError(const file::path& sidecar) const;
    void ClearAutomaticSaveError(const file::path& sidecar);
    bool SaveImportMetadata(const file::path& source, std::string_view payload, std::string& error);
    void QueueAutomaticImportMetadata(const file::path& source, std::string payload);
    void QueueAutomaticTextureSettings(const file::path& source,
        const experiment::cooked::TextureImportSettings& settings);
    void QueueAutomaticRenderProfile(FileGuid guid, const SceneRenderProfile& profile);
    // Event revision only: the host refreshes its AudioCatalog on the game thread.
    [[nodiscard]] std::uint64_t AudioRevision() const noexcept;

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
	bool WriteLayerSettings(const UncatalogedAuthoringRequest& request);
	bool CreateCollisionGeometry(const file::path& destination,
		const ce::physics::CollisionGeometrySource& source);
	bool ReplaceCollisionGeometry(const file::path& destination,
		const ce::physics::CollisionGeometrySource& expected,
		const ce::physics::CollisionGeometrySource& replacement);
    bool SaveInputGraph(const LX::LXInputAsset& graph, const file::path& path, std::string& error);
	file::path ImportSourceAsset(const file::path& source, ImportKind kind);
	bool RecoverModel(const file::path& source, FileGuid expectedId);
	bool SetModelMeshletsAndReimport(const file::path& source, bool enabled);
	bool SetModelLodsAndReimport(const file::path& source, std::uint32_t levels);
    [[nodiscard]] static bool IsTextureSource(const file::path& source);
    // Typed settings only; GUID and source provenance remain importer-owned.
    static void WriteTextureImportSettings(Authoring::WriteNode root,
        const experiment::cooked::TextureImportSettings& settings);
    [[nodiscard]] bool SetTextureImportSettingsAndReimport(const file::path& source,
        const experiment::cooked::TextureImportSettings& settings, std::string& error);
    [[nodiscard]] std::string TextureImportStatus(const file::path& source) const;
    [[nodiscard]] bool TextureImportReady(const file::path& source) const;
	struct ModelRecoveryStats
	{
		std::uint64_t attempts{}, succeeded{}, suppressed{}, declined{}, sourceReloads{};
	};
	ModelRecoveryStats GetModelRecoveryStats() const;
	bool IsSupportExtension(std::string_view extension) const;
	bool SaveMaterial(Material* material);
    bool SaveMaterialGraph(const Material& material, const LX::LXMaterialAsset& graph, const file::path& graphPath,
                           FileGuid graphGuid, std::string& error);
	bool CreateSceneRenderProfile(const file::path& directory, std::string_view name,
		file::path& createdPath, std::string& error);
    bool CreateFolder(const file::path& parent, std::string_view name,
        file::path& createdPath, std::string& error);
	bool SaveExistingSceneRenderProfile(FileGuid guid, SceneRenderProfile* volume);

private:
	EditorAssetDatabase() = default;
	EditorAssetDatabase(const EditorAssetDatabase&) = delete;
	EditorAssetDatabase& operator=(const EditorAssetDatabase&) = delete;

	struct Impl;
    // Accepted scheduler work retains the authoring state without a GT wait.
    std::shared_ptr<Impl> m_impl;
};
