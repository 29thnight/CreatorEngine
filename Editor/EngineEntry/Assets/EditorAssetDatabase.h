#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "../../../Engine/Utility_Framework/Foundation/Core.Minimal.h"
#include "../../../Engine/Utility_Framework/Reflection/TypeTrait.h"

#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

class Material;
namespace LX { class LXMaterialAsset; }
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
    [[nodiscard]] std::optional<std::chrono::milliseconds> PendingAutomaticSaveAge(const std::filesystem::path& sidecar) const;
    [[nodiscard]] std::string AutomaticSaveError(const std::filesystem::path& sidecar) const;
    void ClearAutomaticSaveError(const std::filesystem::path& sidecar);
    bool SaveImportMetadata(const std::filesystem::path& source, std::string_view payload, std::string& error);
    void QueueAutomaticImportMetadata(const std::filesystem::path& source, std::string payload);
    void QueueAutomaticTextureSettings(const std::filesystem::path& source,
        const experiment::cooked::TextureImportSettings& settings);
    void QueueAutomaticRenderProfile(FileGuid guid, const SceneRenderProfile& profile);
    // Event revision only: the host refreshes its AudioCatalog on the game thread.
    [[nodiscard]] std::uint64_t AudioRevision() const noexcept;

	FileGuid CreateMeta(const std::filesystem::path& filepath,
		const FileGuid& preferredGuid = {});
	FileGuid WriteTextAssetWithMeta(const std::filesystem::path& destination,
		std::string_view payload, const FileGuid& preferredGuid);
	// target과 sidecar를 같은 transaction으로 옮긴다. 성공 시 기존 GUID를
	// 반환하며, 새 GUID를 발급하는 fallback은 두지 않는다.
	FileGuid RenameAsset(const std::filesystem::path& source, const std::filesystem::path& destination);
	bool WriteModelCache(const std::filesystem::path& destination,
		std::span<const std::byte> bytes);
	bool WriteEmbeddedTexture(const std::filesystem::path& destination,
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
	bool CreateCollisionGeometry(const std::filesystem::path& destination,
		const ce::physics::CollisionGeometrySource& source);
	bool ReplaceCollisionGeometry(const std::filesystem::path& destination,
		const ce::physics::CollisionGeometrySource& expected,
		const ce::physics::CollisionGeometrySource& replacement);
	bool WriteInputActionMap(const UncatalogedAuthoringRequest& request);
	std::filesystem::path ImportSourceAsset(const std::filesystem::path& source, ImportKind kind);
    bool RecoverModel(const std::filesystem::path& source, FileGuid expectedId);
    bool ReimportModel(const std::filesystem::path& source);
	bool SetModelMeshletsAndReimport(const std::filesystem::path& source, bool enabled);
	bool SetModelLodsAndReimport(const std::filesystem::path& source, std::uint32_t levels);
    [[nodiscard]] static bool IsTextureSource(const std::filesystem::path& source);
    // Typed settings only; GUID and source provenance remain importer-owned.
    static void WriteTextureImportSettings(Authoring::WriteNode root,
        const experiment::cooked::TextureImportSettings& settings);
    [[nodiscard]] bool SetTextureImportSettingsAndReimport(const std::filesystem::path& source,
        const experiment::cooked::TextureImportSettings& settings, std::string& error);
    [[nodiscard]] std::string TextureImportStatus(const std::filesystem::path& source) const;
    [[nodiscard]] bool TextureImportReady(const std::filesystem::path& source) const;
    // Pending cooks or queued resolver publications; excludes texture GPU loading.
    [[nodiscard]] bool HasPendingTexturePublications() const;
	struct ModelRecoveryStats
	{
		std::uint64_t attempts{}, succeeded{}, suppressed{}, declined{}, sourceReloads{};
	};
	ModelRecoveryStats GetModelRecoveryStats() const;
	bool IsSupportExtension(std::string_view extension) const;
	bool SaveMaterial(Material* material);
    struct MaterialGraphSaveResult
    {
        own::shared_owner<const Material> publishedMaterial;
        bool catalogRegistered{};
        std::string warning;
    };
    // 파일 저장과 런타임 게시가 성공한 뒤의 카탈로그 오류는 경고로 따로 전달한다.
    bool SaveMaterialGraph(own::shared_owner<Material> preparedMaterial,
                           const own::shared_owner<const Material>& expectedMaterial,
                           const LX::LXMaterialAsset& graph, const std::filesystem::path& graphPath, FileGuid graphGuid,
                           MaterialGraphSaveResult& result, std::string& error);
	bool CreateSceneRenderProfile(const std::filesystem::path& directory, std::string_view name,
		std::filesystem::path& createdPath, std::string& error);
    bool CreateFolder(const std::filesystem::path& parent, std::string_view name,
        std::filesystem::path& createdPath, std::string& error);
	bool SaveExistingSceneRenderProfile(FileGuid guid, SceneRenderProfile* volume);

private:
	EditorAssetDatabase() = default;
	EditorAssetDatabase(const EditorAssetDatabase&) = delete;
	EditorAssetDatabase& operator=(const EditorAssetDatabase&) = delete;

	struct Impl;
    // Accepted scheduler work retains the authoring state without a GT wait.
    own::shared_owner<Impl> m_impl;
};
