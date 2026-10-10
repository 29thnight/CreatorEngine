#pragma once

#include "Texture.h"
#include "TextureFramePins.h"
#include "AuthoringNodeView.h" // D3-a-5b
#include "AssetMetaRegistry.h"
#include "ClassProperty.h"
#include "AssetBundle.h"
#include "JobScheduler.h"
#include "ShaderMetaHandle.h"
#include "AssetDepot/AssetLink.h"
#include "AssetDepot/AssetMountId.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelSceneAssetInputs.h"
#include "MaterialGraphRuntime.h"
#include "../Utility_Framework/Ownership.h"
#include "AssetDepot/TextureAssetRuntime.h"
#include "AssetDepot/LegacyResourceCache.h"
#include "AssetDepot/ModelAssetRuntime.h"
#include "AssetDepot/MaterialAssetRuntime.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

template <typename T>
using DataContainer = std::unordered_map<std::string, asset_cache_detail::Entry<T>>;

class Material;

struct PreparedTextureImageRequest final
{
    own::shared_owner<const Texture> descriptor;
    AssetDepot::AssetRequest<Texture::CodecImage> request;
};

class FontAsset;

struct AssetBundleLoadResult
{
    std::size_t submitted{};
    std::size_t completed{};
    AssetDepot::AssetRequestStatus status{ AssetDepot::AssetRequestStatus::Pending };
    AssetDepot::AssetRequestError error{ AssetDepot::AssetRequestError::None };
    std::string message;
    std::vector<assets::ModelAssetGeneration::Shared> models;
    std::vector<own::shared_owner<const Material>> materials;
    std::vector<own::shared_owner<const Texture>> textures;
    std::vector<std::shared_ptr<FontAsset>> fonts;
    own::shared_owner<TextureFramePins> textureImages;
    std::vector<PreparedTextureImageRequest> imageRequests;
    // Nonblocking, including for direct LoadAssetBundle result owners.
    bool PollImages();
};

// Main system for storing runtime data
class ModelLoader;
class Model;
class Material;
class FontAsset;
struct ShaderMeta;
namespace Authoring { class WriteNode; }
namespace experiment { struct Material; } // I5-D5c1 저작 원본 보관
namespace experiment::cooked
{
    class CookedAssetCatalog;
    class ArtifactByteSource;
    struct AssetSetMountOptions;
} // I7-C1
namespace experiment { struct AssetId; } // I7-C2

enum class RuntimeAssetType
{
	Auto,
	CatalogOnly,
	Model,
	Material,
	Texture,
	UITexture,
	SpriteSheet,
	ShaderMeta,
    MaterialGraph,
    Font,
};

enum class RuntimeAssetChangeKind
{
	CatalogUpsert,
	ContentReload,
	Removed,
};

// Authoring prepares complete immutable storage off-thread. Only the existing
// asset-change drain installs the resolver generation; accepted owners survive.
struct RuntimeTexturePublication final
{
    enum class State { Queued, Loading, Failed, Superseded };
    experiment::cooked::AssetSetManifest manifest{};
    std::vector<std::byte> manifestBytes{};
    own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};
    std::shared_ptr<std::atomic<std::uint64_t>> latestRevision{};
    std::uint64_t revision{};
    std::atomic<State> state{ State::Queued };
    // Written before Loading (release), then read through Snapshot (acquire).
    AssetDepot::AssetRequest<Texture> request{};
};

// Editor 같은 authoring Host가 완전히 게시한 파일의 결과만 이 계약으로 넘긴다.
// Runtime은 source/meta 작성 방법을 알지 않고 catalog와 cache generation만 갱신한다.
struct RuntimeAssetChange
{
	RuntimeAssetChangeKind kind{ RuntimeAssetChangeKind::CatalogUpsert };
	RuntimeAssetType assetType{ RuntimeAssetType::Auto };
	FileGuid guid{};
	file::path path{};
    own::shared_owner<RuntimeTexturePublication> texturePublication{};
};

class DataSystem : public Singleton<DataSystem>
{
public:
	enum class TextureFileType
	{
		Texture,
		MaterialTexture,
		TerrainTexture,
		HDR,
		UITexture,
		SpriteSheet,
	};

	enum class AssetType
	{
		Model,
		Material,
		Skeleton,
	};

private:
    friend class Singleton<DataSystem>;

	DataSystem() = default;
	~DataSystem();
public:
    // Compatibility probe for older diagnostics. All durable Texture aliases
    // now travel with typed scene/proxy/frame owners; no retirement root is needed.
    static constexpr bool RequiresLegacyRetiredGeneration(RuntimeAssetType) noexcept
    {
        return false;
    }

    // Typed CPU acquisition validates each concrete representation separately.
    // Metadata Ready is distinct from a verified graph/code program generation.
    // Current links never resurrect an old generation.
    // Misses are I/O-free; callers request work explicitly and poll its state.
    template<class T>
        requires (!std::is_same_v<T, Texture::CodecImage>)
    [[nodiscard]] own::shared_owner<const T> TryAcquire(AssetDepot::AssetLink<T> link,
        const AssetDepot::TextureAssetVariant& variant = {})
    {
        if constexpr (std::is_same_v<T, Texture>)
        {
            return TryAcquireTexture(link, variant);
        }
        else if constexpr (std::is_same_v<T, ShaderMeta>)
        {
            return TryAcquireShaderMeta(link);
        }
        else if constexpr (std::is_same_v<T, Material> || std::is_same_v<T, material_graph::Generation>
            || std::is_same_v<T, experiment::Material> || std::is_same_v<T, LX::Runtime::ShaderGeneration>)
        {
            return TryAcquireCurrentMaterialPipelineAsset(link);
        }
        else
        {
            static_assert(std::is_same_v<T, assets::ModelAnimationDescriptor>
                || std::is_same_v<T, assets::ModelSkeletonPayload>
                || std::is_same_v<T, assets::ModelAnimationPayload>
                || std::is_same_v<T, assets::ModelMeshDescriptor>, "Unsupported AssetDepot type.");
            return TryAcquireCurrentModelAsset(link);
        }
    }

    template<class T>
        requires (!std::is_same_v<T, Texture::CodecImage>)
    [[nodiscard]] AssetDepot::AssetRequest<T> RequestAsync(AssetDepot::AssetLink<T> link,
        const AssetDepot::TextureAssetVariant& variant = {})
    {
        if constexpr (std::is_same_v<T, Texture>)
        {
            return RequestTextureAsync(link, variant);
        }
        else if constexpr (std::is_same_v<T, ShaderMeta>)
        {
            return RequestShaderMetaAsync(link);
        }
        else if constexpr (std::is_same_v<T, Material> || std::is_same_v<T, material_graph::Generation>
            || std::is_same_v<T, experiment::Material> || std::is_same_v<T, LX::Runtime::ShaderGeneration>)
        {
            return RequestCurrentMaterialPipelineAssetAsync(link);
        }
        else
        {
            static_assert(std::is_same_v<T, assets::ModelAnimationDescriptor>
                || std::is_same_v<T, assets::ModelSkeletonPayload>
                || std::is_same_v<T, assets::ModelAnimationPayload>
                || std::is_same_v<T, assets::ModelMeshDescriptor>, "Unsupported AssetDepot type.");
            return RequestCurrentModelAssetAsync(link);
        }
    }

    [[nodiscard]] AssetDepot::AssetRequest<assets::ModelGeometryPayload> RequestAsync(
        own::shared_owner<const assets::ModelMeshDescriptor> descriptor);
    [[nodiscard]] own::shared_owner<const assets::ModelGeometryPayload> TryAcquire(
        const own::shared_owner<const assets::ModelMeshDescriptor>& descriptor);
    [[nodiscard]] AssetDepot::AssetRequest<assets::ModelMeshDescriptor> RequestAsync(
        own::shared_owner<const assets::ModelAnimationDescriptor> descriptor, std::size_t meshIndex);
    [[nodiscard]] own::shared_owner<const assets::ModelMeshDescriptor> TryAcquire(
        const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor, std::size_t meshIndex);
    // An unresolved material is a new lookup, admitted only while the model's
    // resolver revision remains current. It must never mix newer dependencies.
    [[nodiscard]] AssetDepot::AssetRequest<Material> RequestAsync(
        const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor,
        AssetDepot::AssetLink<Material> material);
    // Explicit old authoring workflow; conversion/image copies run in tracked
    // asset work, not on the UI/scene thread. Never used for mounted v3 records.
    [[nodiscard]] AssetDepot::AssetRequest<Material> RequestLegacyModelMaterialAsync(
        own::shared_owner<const assets::ModelAssetGeneration> generation, FileGuid materialId);
    [[nodiscard]] bool HasModelMeshDescriptor(AssetDepot::AssetLink<assets::ModelMeshDescriptor> link) const;
    void SetModelGeometryCacheBudgets(std::size_t descriptors, std::size_t geometry);

    // Exact payload access requires an already-owned descriptor. These overloads
    // never resolve a child against the latest catalog and survive logical unmount.
    [[nodiscard]] AssetDepot::AssetRequest<assets::ModelSkeletonPayload> RequestSkeletonAsync(
        own::shared_owner<const assets::ModelAnimationDescriptor> descriptor);
    [[nodiscard]] AssetDepot::AssetRequest<assets::ModelAnimationPayload> RequestAnimationAsync(
        own::shared_owner<const assets::ModelAnimationDescriptor> descriptor, std::size_t clipIndex);
    [[nodiscard]] own::shared_owner<const assets::ModelSkeletonPayload> TryAcquireSkeleton(
        const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor);
    [[nodiscard]] own::shared_owner<const assets::ModelAnimationPayload> TryAcquireAnimation(
        const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor, std::size_t clipIndex);
    [[nodiscard]] bool HasModelAnimationDescriptor(
        AssetDepot::AssetLink<assets::ModelAnimationDescriptor> link) const;
    void SetModelAssetCacheBudgets(std::size_t descriptors, std::size_t skeletons, std::size_t clips);
    // Independent compatible-decode retention; zero both animation budget APIs
    // to remove their retained owners. Separate skinned-mesh cache entries and
    // consumer/work pins can still keep skeleton storage alive.
    void SetModelAnimationStorageCacheBudgets(std::size_t skeletons, std::size_t clips);
    [[nodiscard]] AssetDepot::ModelAssetCacheSnapshot SnapshotModelAssetCache() const;
    [[nodiscard]] AssetDepot::AssetRequestStatistics SnapshotAssetRequests() const noexcept
    {
        return m_assetRequestCounters->Snapshot();
    }


    template<class T>
        requires std::is_same_v<T, Texture::CodecImage>
    [[nodiscard]] own::shared_owner<const T> TryAcquire(
        const own::shared_owner<const Texture>& descriptor)
    {
        static_assert(std::is_same_v<T, Texture::CodecImage>, "Unsupported texture payload type.");
        return TryAcquireTextureImage(descriptor);
    }
    template<class T>
        requires std::is_same_v<T, Texture::CodecImage>
    [[nodiscard]] AssetDepot::AssetRequest<T> RequestAsync(
        const own::shared_owner<const Texture>& descriptor)
    {
        static_assert(std::is_same_v<T, Texture::CodecImage>, "Unsupported texture payload type.");
        return RequestTextureImageAsync(descriptor);
    }
    void SetTextureImageCacheBudget(std::size_t bytes);
    [[nodiscard]] AssetDepot::TextureImageCacheSnapshot SnapshotTextureImageCache() const;

    void SetTextureAssetCacheBudget(std::size_t bytes);
    [[nodiscard]] AssetDepot::TextureAssetCacheSnapshot SnapshotTextureAssetCache() const;

    // Authored cooked descriptors only. CPU Ready does not compile source or
    // imply a prepared shader program, pipeline state or GPU upload.
    void SetShaderMetaAssetCacheBudget(std::size_t bytes);
    [[nodiscard]] AssetDepot::ShaderMetaAssetCacheSnapshot SnapshotShaderMetaAssetCache() const;
    void SetMaterialAssetCacheBudgets(std::size_t programs, std::size_t materials);
    void SetCodeMaterialAssetCacheBudgets(std::size_t programs, std::size_t materials);
    [[nodiscard]] AssetDepot::MaterialAssetCacheSnapshot SnapshotMaterialAssetCache() const;

	void Initialize();
    // Lifecycle owner only: stop admission and drain every accepted asset job
    // before clearing CPU caches. Existing renderer completion/retire rules apply.
    void Finalize();
	// Asset bundle operations
	AssetBundleLoadResult LoadAssetBundle(const AssetBundle& bundle);
    class AssetBundlePreparation final
    {
    public:
        [[nodiscard]] AssetBundleLoadResult Snapshot() const;
        [[nodiscard]] job_handle Completion() const;
        void Cancel();
    private:
        friend class DataSystem;
        mutable std::mutex mutex;
        std::uint64_t epoch{};
        std::uint64_t resolverRevision{};
        std::atomic<bool> cancelled{};
        AssetBundleLoadResult result;
        std::vector<AssetDepot::AssetRequest<Texture>> descriptorRequests;
        job_handle work;
    };
    // Nonblocking submission returns the actual result owner. Jobs and shutdown
    // drain snapshots retain it independently; the lookup registry is weak.
    own::shared_owner<AssetBundlePreparation> LoadAssetBundleAsync(const AssetBundle& bundle);
    // CPU preparation uses the common scheduler. Poll/publish only at the scene
    // owner boundary; GPU upload/readiness remains owned by the renderer.
    struct PreparedRuntimeAsset;
    using ModelPreparation = own::shared_owner<PreparedRuntimeAsset>;
    // Set allowMounted=false only for an explicit unmounted authoring fallback.
    // Admission rejects a mounted winner before any full scene preparation.
    ModelPreparation PrepareModelAssetByPath(std::string_view path, bool allowMounted = true);
    // Full model scene preparation is explicit. Root enumeration/acquisition
    // remains metadata-only and individual mesh/clip requests stay independent.
    // Collider demand is fixed per ticket; construction uses its effective choice.
    ModelPreparation PrepareModelAsset(AssetDepot::AssetLink<assets::ModelAnimationDescriptor> link,
        assets::ModelColliderPreparationPolicy colliderPolicy = assets::ModelColliderPreparationPolicy::CookedDefault);
    [[nodiscard]] bool HasPreparedModelScene(const ModelPreparation& preparation) const;
    bool ReadPreparedModelScene(const ModelPreparation& preparation,
        assets::ModelSceneAssetInputs& result, std::string& error) const;
    job_handle ModelPreparationCompletion(const ModelPreparation& preparation) const;
    assets::ModelAssetGeneration::Shared ReadPreparedModel(
        const ModelPreparation& preparation, std::string& error) const;
    bool PublishPreparedModel(const ModelPreparation& preparation, std::string& error);
    struct SceneAssetPreparation;
    // A short root-publication linearization guard, not an asset residency pin.
    // No mutex stays held while scene deserializers/callbacks run. Its caller must
    // stay on the scene owner and destroy it before DataSystem lifecycle teardown.
    class SceneAssetHandoff final
    {
    public:
        SceneAssetHandoff() = default;
        SceneAssetHandoff(const SceneAssetHandoff&) = delete;
        SceneAssetHandoff& operator=(const SceneAssetHandoff&) = delete;
        SceneAssetHandoff(SceneAssetHandoff&& other) noexcept;
        SceneAssetHandoff& operator=(SceneAssetHandoff&&) = delete;
        ~SceneAssetHandoff();
        explicit operator bool() const noexcept { return m_system != nullptr; }
    private:
        friend class DataSystem;
        explicit SceneAssetHandoff(DataSystem& system) noexcept : m_system(&system) {}
        DataSystem* m_system{};
    };
    [[nodiscard]] SceneAssetHandoff BeginSceneAssetHandoff(
        const own::shared_owner<SceneAssetPreparation>& preparation, AssetDepot::AssetRequestStatus& status);

    struct AssetPreparationProgress
    {
        std::size_t activeRequests{};
        std::size_t completed{};
        std::size_t total{}; // CPU preparation items in this stage; zero during discovery.
        std::string phase;
        std::string name;
    };
    own::shared_owner<SceneAssetPreparation> PrepareSceneAssets(
        const Authoring::ReadNode& root, const AssetBundle& bundle, const file::path& scene);
    bool PollSceneAssets(const own::shared_owner<SceneAssetPreparation>& preparation,
        bool wait, bool publish, std::string& error);
    void CancelSceneAssets(const own::shared_owner<SceneAssetPreparation>& preparation);
    [[nodiscard]] bool IsSceneAssetPreparationCurrent(
        const own::shared_owner<SceneAssetPreparation>& preparation) const;
    [[nodiscard]] own::shared_owner<TextureFramePins> SceneTextureImagePins(
        const own::shared_owner<SceneAssetPreparation>& preparation) const;
    [[nodiscard]] AssetPreparationProgress SnapshotAssetPreparationProgress() const;
	void RetainAssets(const AssetBundle& bundle);
	void ClearRetainedAssets();
	[[nodiscard]] size_t SnapshotRetainedAssetCount() const;
	void UnloadUnusedAssets();
	//Resource Model — PHASE 3.75 MBC9: 모델의 유일한 런타임 표현은 immutable
	// assets::ModelAssetGeneration이다(legacy Model·Assimp·역브리지·병행 핸들 은퇴).
	// schema-v2 generation을 전부 검증한 뒤 aggregate 하나를 cache에 게시한다.
	// cache의 key/handle은 이름·주소를 쓰지 않는다({ModelId, generation}).
	[[nodiscard]] assets::ModelAssetGeneration::Shared LoadModelAssetGeneration(
		FileGuid guid);
	// 경로(소스 파일) → registry GUID → generation. 에디터 드롭·CLI의 경로 창구.
	[[nodiscard]] assets::ModelAssetGeneration::Shared LoadModelAssetGenerationByPath(
		std::string_view filePath);
	// 파일 stem(확장자 없는 이름) → GUID → generation. 이름 신원(Foliage·CLI)의 창구.
	[[nodiscard]] assets::ModelAssetGeneration::Shared FindModelAssetGenerationByStem(
		std::string_view stem);
	// sidecar(ModelImporter.CreateMeshCollider)의 씬 인스턴스화 옵션. 없으면 false.
	[[nodiscard]] bool ReadModelCreateMeshCollider(FileGuid guid) const;
	[[nodiscard]] assets::ModelAssetGeneration::Shared ResolveModelAssetGeneration(
		assets::ModelAssetGenerationHandle handle) const;
	[[nodiscard]] assets::ModelAssetGenerationCacheSnapshot
		SnapshotModelAssetGenerations() const;
	[[nodiscard]] std::vector<assets::ModelAssetGeneration::Shared>
		SnapshotCurrentModelAssetGenerations() const;
	// MBC11 — generation 로드의 출처 계수(읽기 전용). cooked catalog가 마운트돼 그 안의
	// generation 레코드로 읽었으면 catalog, 아니면 Library(저작 정본).
	struct ModelGenerationSourceSnapshot final
	{
		std::uint64_t fromCatalog{ 0 };
		std::uint64_t fromLibrary{ 0 };
		std::uint64_t failed{ 0 };
	};
	[[nodiscard]] ModelGenerationSourceSnapshot SnapshotModelGenerationSources() const noexcept;
	// PHASE 3.75 MBC7 — generation closure의 embedded texture를 GPU 업로드 가능한
	// Texture generation owner로 만든다. 키는 {TextureId, generation}이라 reimport가
	// generation을 갈아 끼우면 이전 texture owner는 다시 나오지 않는다(retire와 함께
	// 떨어진다). 이 캐시가 임베디드 등록부(RegisterEmbeddedTexture)·이름 폴백·로드
	// 순서에 기대지 않는 유일한 embedded texture 창구다 — MeshRenderer가 모델
	// generation을 먼저 붙들고 재질의 texture GUID를 그 closure에서 푼다(§6.2).
	// 이 generation에 없는 TextureId면 nullptr(호출자가 다른 축으로 내려간다).
	struct ModelGenerationTextureCacheSnapshot
	{
		std::size_t live{};
		std::uint64_t created{};
		std::uint64_t hits{};
		std::uint64_t retired{};
		std::uint64_t rejected{};
	};
	[[nodiscard]] own::shared_owner<const Texture> ResolveModelGenerationTexture(
		const assets::ModelAssetGeneration& generation,
		const Uuid::Uuid16& textureId);
	[[nodiscard]] ModelGenerationTextureCacheSnapshot
		SnapshotModelGenerationTextures() const;
	// 재질의 texture property(GUID 정본) 중 이 generation closure 안의 TextureId를
	// generation owner로 묶는다. 표준 5슬롯과 ShaderMeta 임의 texture property를
	// 같은 규칙으로 다룬다. 묶은 수를 돌려준다(0이면 이 재질은 이 모델의
	// embedded texture를 참조하지 않는다).
	std::size_t BindModelGenerationTextures(Material& material,
		const assets::ModelAssetGeneration& generation);
	// I5-D1a — experiment::Model → legacy ::Model 역브리지(전환기, I6 은퇴).
	// 렌더 소유가 아직 legacy인 동안 experiment 로드 결과를 기존 파이프에
	// 소비시키는 어댑터다(I5-M의 ConvertToLegacyMaterial과 같은 지위).
	// Model 컨테이너가 private+friend라 DataSystem 멤버로만 시공 가능하다 —
	// 정의는 DataSystem.cpp(별도 TU).
	// I7-C1 — cooked catalog 기동. `<derivedRoot>/Derived/asset-manifest.cemf`를
	// 읽어 자산 GUID→cooked artifact 표를 세운다. 파일이 없으면 **무동작**이다
	// (에디터 작업 트리에는 Derived가 없다 — 마운트 실패가 아니라 미게시다).
	// 이것이 M2 resolver의 cooked 우선 해석과 모델 cookedPath의 유일한 출처다.
	bool MountCookedCatalog(const file::path& derivedRoot, std::string& outError);

    // AssetDepot metadata transactions. v2 packages remain on MountCookedCatalog;
    // a new AssetSet must be explicitly cooked as CEMF v3. replaceMount retires
    // an old logical mount in the same candidate transaction, preserving owners.
    [[nodiscard]] AssetDepot::AssetMountId MountAssetSet(
        std::span<const std::byte> manifestBytes,
        own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource,
        const experiment::cooked::AssetSetMountOptions& options,
        std::vector<experiment::cooked::AssetManifestIssue>& outIssues,
        AssetDepot::AssetMountId replaceMount = {});
    [[nodiscard]] bool UnmountAssetSet(AssetDepot::AssetMountId mountId,
        std::vector<experiment::cooked::AssetManifestIssue>& outIssues);

    template<class T>
    [[nodiscard]] std::vector<AssetDepot::AssetLink<T>> ListRootLinks(
        AssetDepot::AssetMountId mountId) const
    {
        std::vector<AssetDepot::AssetLink<T>> result;
        for (const auto& reference : ListAssetSetRoots(mountId, AssetDepot::AssetLink<T>::kKind))
        {
            AssetDepot::AssetLink<T> link;
            if (AssetDepot::AssetLink<T>::FromReference(reference, link))
            {
                result.push_back(link);
            }
        }
        return result;
    }

	// 수명 안전: 호출자가 strong owner를 잡은 동안만 raw 포인터를 쓴다
	// (마운트가 렌더 중에 표를 갈아 끼워도 진행 중인 해석이 살아 있어야 한다).
	[[nodiscard]] own::shared_owner<const experiment::cooked::CookedAssetCatalog>
		GetCookedCatalog() const;
	[[nodiscard]] std::size_t CookedCatalogEntryCount() const;
	[[nodiscard]] std::size_t CookedCatalogSourceAssetCount() const;
	// I7-C2 — 신선도 판정. cooked artifact가 소스보다 낡았으면 그 entry는 없는
	// 것으로 친다(빈 경로) — 모델은 source 디코더로, 텍스처는 source 폴백으로
	// 간다. 두 정책 모두 이미 서 있어서 여기서 경로만 끊으면 된다.
	//
	// 판정 기준은 **mtime**이다(아티팩트가 소스보다 오래되면 낡음). 아티팩트
	// 안에 소스 시각을 넣는 길은 막혀 있다 — ModelCookProducer가 결정적 cook을
	// 위해 일부러 지운다("같은 Assets tree를 어느 staging 경로에 놓아도 동일한
	// CEMC"). 내구적인 답은 소스 **내용 해시**를 CEMF에 싣는 것이고 그것은
	// 포맷 확장이라 별도 슬라이스다 — 그때까지 이 heuristic이 자리를 지킨다.
	[[nodiscard]] file::path ResolveCookedArtifact(
		const experiment::AssetId& assetId) const;
	// D5-d document cutover. Produced runtime assets use a fresh cooked artifact
	// when one exists. Editor may fall back to the authoring source; packaged
	// Player fails closed instead of silently reopening source YAML/bytes.
	[[nodiscard]] file::path ResolveCatalogAssetPath(FileGuid assetGuid) const;
	[[nodiscard]] std::size_t CookedCatalogStaleCount() const;

	//Resource Texture
	own::shared_owner<const Texture> LoadTextureGUID(FileGuid guid);
	own::shared_owner<const Texture> LoadTexture(std::string_view filePath, TextureFileType type = TextureFileType::Texture);
    // Source identity is the Assets-relative path including extension (absolute
    // outside Assets). The cache also separates role/compression/color space;
    // Texture::m_assetPath stays the source path suitable for serialization.
	own::shared_owner<const Texture> LoadSharedTexture(std::string_view filePath, TextureFileType type = TextureFileType::Texture);
	std::vector<std::pair<std::string, own::shared_owner<const Texture>>> SnapshotTextures(TextureFileType type = TextureFileType::Texture);
    // CPU SDF fonts use catalog GUID + normalized resolved path identity, never
    // the basename. Accepts a catalog GUID or a source path; an empty path
    // selects the bundled engine default.
    std::shared_ptr<FontAsset> LoadFontShared(std::string_view path, std::string& error);
    std::vector<std::pair<std::string, std::shared_ptr<FontAsset>>> SnapshotFonts() const;
    std::size_t SnapshotFontCount() const;
    std::uint64_t GetFontCacheRevision() const noexcept
    {
        return m_fontCacheRevision.load(std::memory_order_acquire);
    }
	//Resource Material
	void InsertMaterial(own::shared_owner<const Material> material);
	own::shared_owner<const Material> FindCachedMaterial(std::string_view name);
    own::shared_owner<const Material> FindCachedMaterial(FileGuid guid);
    std::uint64_t MaterialAssetRevision() const noexcept
    {
        return m_materialAssetRevision.load(std::memory_order_relaxed);
    }
    bool ReloadMaterialAsset(FileGuid guid, std::string& error);
    bool PublishMaterialAsset(own::shared_owner<Material> candidate, const own::shared_owner<const Material>& expectedCurrent,
                              std::string& error);
	std::vector<std::pair<std::string, own::shared_owner<const Material>>> SnapshotMaterials();
	own::shared_owner<const Material> RegisterImportedMaterial(
		own::shared_owner<const Material> material, std::string_view baseName);
	// M5-B1: standalone Material YAML의 단일 codec. scene embedded material은
	// typed reflection이 값을 복원한 뒤 같은 runtime finalize 규약을 공유한다.
	bool SerializeMaterialPayload(Material& material,
		Authoring::WriteNode outNode) const;
	bool DeserializeMaterialPayload(Material& material, const Authoring::NodeView& node);
	// I5-D5c1 — 저작 원본 보관 창구. 새 정본(schema+shaderAssetId) 문서는
	// experiment::Material로 읽힌 뒤 legacy로 변환되고 **원본이 버려져 왔다** —
	// 그래서 sealing이 매 프레임 legacy를 experiment로 되돌린다(왕복). 이 창구는
	// 그 원본을 함께 돌려준다. legacy 표기 문서에는 원본이 없으므로
	// outAuthored는 채워지지 않는다(반환값은 그대로 성공).
	bool DeserializeMaterialPayload(Material& material,
        const Authoring::NodeView& node, experiment::Material* outAuthored, bool persistRecovery = true,
        std::span<const own::shared_owner<const Texture>> preparedTextures = {});
	// I5-D5c1 — base 재질 자산의 저작 원본. 씬의 ref 표기가 base를 legacy로만
	// 로드해 왔다(LoadMaterialShared). 실패·legacy 표기 자산은 nullptr다.
    [[nodiscard]] own::shared_owner<const Material> LoadMaterialByGuid(FileGuid guid);
    [[nodiscard]] bool RebuildCookedMaterialInstance(Material& material, std::string& error,
        std::span<const own::shared_owner<const Texture>> preparedTextures = {},
        std::span<const experiment::MaterialProperty> propertyOverrides = {});
	own::shared_owner<const experiment::Material> LoadAuthoredMaterialShared(
		FileGuid assetGuid);
	// Model cache는 이 versioned envelope 안에 위 YAML payload를 넣는다. 기존
	// 무버전 binary record 판별은 probe 뒤 ModelLoader의 read-only 호환 경로가 맡는다.
	bool HasVersionedMaterialBinaryPayload(std::istream& input) const;
	bool SerializeMaterialBinaryPayload(Material& material, std::ostream& output) const;
	bool DeserializeMaterialBinaryPayload(Material& material, std::istream& input);
	void FinalizeMaterialRuntime(Material& material);
	own::shared_owner<const Material> LoadMaterial(std::string_view name);
	// 소유권을 공유하는 조회. 컴포넌트처럼 참조를 보관하는 쪽은 이것을 써야
	// 캐시에서 제거되어도 사용 중인 머티리얼이 파괴되지 않는다.
	own::shared_owner<const Material> LoadMaterialShared(std::string_view name);
    own::shared_owner<const Material> LoadMaterialShared(FileGuid guid);
	own::shared_owner<const Texture> LoadSharedMaterialTexture(std::string_view filePath, bool isCompress,
        std::optional<bool> srgb = std::nullopt);
	own::shared_owner<Material> CreateMaterial();
	// Asset Metadata
	FileGuid GetFileGuid(const file::path& filepath) const;
    // Transitional synchronous engine-internal handoff: the immutable owner and
    // numeric identity are acquired together. The handle itself never pins data.
    own::shared_owner<const ShaderMeta> LoadShaderMetaOwner(FileGuid guid,
        ShaderMetaHandle& outHandle, std::string& outError);
    // CPU cache retention only; zero still permits owner-returning loads.
    void SetShaderMetaRetainedBudgetBytes(std::size_t bytes);
    std::size_t ShaderMetaRetainedBytes() const;
    std::size_t ShaderMetaRetainedBudgetBytes() const;

    own::shared_owner<const material_graph::Generation> LoadMaterialGraphGeneration(FileGuid guid, std::string& error,
                                                                                  bool reload = false);
    own::shared_owner<const material_graph::Generation> ResolveMaterialGraphGeneration(FileGuid guid) const;
    file::path GetMaterialGraphSourcePath(FileGuid guid) const;
    // Explicit synchronous warm-up retains verified graph generations, so later
    // entity loads consume the prepared owners without a second cache decode.
    // Synchronous scene-owner entry point; pool workers cannot call or wait here.
    // Historical lists are hints here; PrepareSceneAssets discovers current
    // dependencies for the normal asynchronous editor path.
    void PrewarmSceneMaterials(const file::path& scene);
    void CommitSceneMaterials(const file::path& scene);
    bool ConfigureModelMaterialGraph(Material& material, const assets::ModelAssetGeneration& model,
                                      const assets::ModelMaterialAsset& source, std::string& error);
    // Editor import/load recovery publishes a separate editable graph; never a native pass fallback.
    bool ConfigureEditableDefaultMaterialGraph(Material& material, std::string_view cause, std::string& error);
    bool ConfigureMaterialGraph(Material& material, const material_graph::InstanceDescription& description,
                                 std::string& error, bool reload = false,
                                 const assets::ModelAssetGeneration* model = nullptr);
    bool ConfigureMaterialGraphAuthoring(Material& material, const LX::LXMaterialAsset& asset,
                                         const material_graph::InstanceDescription& description, std::string& error);
    bool ConfigureMaterialGraph(Material& material, own::shared_owner<const material_graph::Generation> generation,
                                const material_graph::InstanceDescription& description, std::string& error,
                                const assets::ModelAssetGeneration* model = nullptr);
    bool PrepareMaterialGraphAuthoring(const Material& source, const LX::LXMaterialAsset& asset,
                                      const material_graph::InstanceDescription& description,
                                      own::shared_owner<Material>& candidate, std::string& error);
    // Capture paths on the caller thread. This CPU-only job owns all inputs and
    // has no DataSystem lifetime dependency or shared generated-source filename.
    static own::shared_owner<const material_graph::Generation> CompileMaterialGraphAuthoring(
        const LX::LXMaterialAsset& asset, FileGuid graphGuid, const file::path& shaderDirectory,
        const file::path& cacheDirectory, std::string& error);
    bool PublishMaterialGraphReload(own::shared_owner<const material_graph::Generation> generation,
        const own::shared_owner<const material_graph::Generation>& expected, std::string& error);
    bool PublishCodeShaderReload(const own::shared_owner<const LX::Runtime::ShaderGeneration>& expected,
        own::shared_owner<const LX::Runtime::ShaderGeneration>& shader, std::string& error);
    own::shared_owner<const ShaderMeta> ResolveShaderMeta(ShaderMetaHandle handle) const;
	bool LoadShaderMetaGUID(FileGuid guid, ShaderMeta& outMeta,
		std::string& outError);

	// Authoring Host가 파일/meta 게시를 끝낸 뒤 전달하는 유일한 변경 경계다.
	// Player는 생산자를 설치하지 않고 startup catalog만 읽는다.
	// A rejected resident-model reload returns false and keeps its last valid
	// generation. Callers must not report that reload as successfully applied.
	bool ApplyAssetChange(const RuntimeAssetChange& change);
	// Watcher I/O thread는 cache/catalog를 직접 바꾸지 않고 이 큐에 게시한다.
	// Editor game thread가 프레임 경계에서 DrainQueuedAssetChanges를 호출해
	// generation 변경과 이후 load의 관측 순서를 직렬화한다.
	void QueueAssetChange(RuntimeAssetChange change);
	std::size_t DrainQueuedAssetChanges();
	FileGuid GetFilenameToGuid(const std::string& filename) const;
	FileGuid GetStemToGuid(const std::string& stem) const;
	file::path GetFilePath(FileGuid fileguid) const;

private:
    // Transitional synchronous caches: lookup and retained CPU budget share each entry.
    static constexpr std::size_t kLegacyTextureBudgetBytes = 256u * 1024u * 1024u;
    static constexpr std::size_t kLegacyMaterialBudgetBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t kLegacyCacheBudgetEntries = 512u;
	DataContainer<Material>		Materials;
	DataContainer<Texture>		Textures;
	DataContainer<Texture>		UITextures;
	DataContainer<Texture>		SpriteSheets;
	std::unordered_map<int, std::unordered_set<std::string>> m_retainedAssets;

public:
	std::string m_trasfarShader{};
private:

	// 캐시별 보호 규약(모델 generation cache는 자체 동기화 — ModelAssetGenerationCache).
	//   m_materialMutex : Materials
	//   m_textureMutex  : Textures / UITextures / SpriteSheets
	//   m_fontMutex     : m_fonts
	// 이 맵들은 LoadAssetBundle이 스레드풀로 병렬 로딩하므로,
	// 조회·삽입 시 반드시 해당 뮤텍스를 잡아야 한다.
	std::mutex m_textureMutex;
	std::mutex m_materialMutex;
    std::atomic<std::uint64_t> m_materialAssetRevision{};
	mutable std::mutex m_fontMutex;
	mutable std::mutex m_retainedAssetsMutex;

	// I5-D5c1 — base 재질 자산의 저작 원본 캐시(GUID 키). legacy Materials
	// 캐시와 별개다: 그쪽은 변환 산물이고 이쪽이 원본이다. 자체 뮤텍스를
	// 쓴다 — LoadAuthoredMaterialShared는 파일 파싱 중 legacy 캐시를 건드리지
	// 않으므로 m_materialMutex와 겹칠 이유가 없다.
	std::mutex m_authoredMaterialMutex;
	std::unordered_map<FileGuid,
		asset_cache_detail::Entry<experiment::Material>> m_authoredMaterials;

	// MBC5 정본 cache. {ModelId,generation}을 주소 키로 쓰고 current generation
	// 교체/retire를 aggregate 전체에 원자 적용한다.
	assets::ModelAssetGenerationCache m_modelAssetGenerations;
	// MBC7 — generation embedded texture owner. 값은 generation 픽셀에서 만든
	// Texture고, 소유자 색인은 retire가 generation 단위로 걷기 위한 역참조다.
	mutable std::mutex m_modelGenerationTextureMutex;
	std::map<assets::ModelTextureHandle, asset_cache_detail::Entry<Texture>>
		m_modelGenerationTextures;
	std::map<assets::ModelAssetGenerationHandle,
		std::vector<assets::ModelTextureHandle>> m_modelGenerationTextureOwners;
	mutable ModelGenerationTextureCacheSnapshot m_modelGenerationTextureStats;
	// I7-C1 — cooked catalog. immutable 표라 교체는 포인터 하나 바꾸기다.
	own::shared_owner<const experiment::cooked::CookedAssetCatalog> m_cookedCatalog;
	mutable std::mutex m_cookedCatalogMutex;
    // Boot-only I/O, before asset consumers start. Publishes the complete configured
    // set group or preserves the old resolver; never exposed as a cache-hit path.
    [[nodiscard]] bool MountConfiguredAssetSets(const file::path& assetRoot, std::string& failure);
    // Guarded with m_cookedCatalogMutex; never reset/reused on reinitialize.
    std::uint64_t m_assetDepotRevision{};
    std::uint64_t m_nextAssetMountId{ 1u };
    // Frame-boundary authoring overlays, guarded by m_assetPreparationMutex.
    std::map<FileGuid, AssetDepot::AssetMountId> m_authoredTextureMounts;
    bool PublishAuthoredTexture(const RuntimeAssetChange& change);
    [[nodiscard]] std::vector<experiment::cooked::TypedAssetReference> ListAssetSetRoots(
        AssetDepot::AssetMountId mountId, experiment::cooked::CookedAssetKind kind) const;
	// I7-C2 — 마운트 때 한 번 판정한 stale 집합. 해석마다 stat을 두 번 하면
	// sealing이 매 프레임 그 값을 문다.
	std::unordered_set<FileGuid> m_cookedStaleAssets;
	std::atomic<std::uint64_t> m_generationFromCatalog{ 0 };
	std::atomic<std::uint64_t> m_generationFromLibrary{ 0 };
	std::atomic<std::uint64_t> m_generationLoadFailed{ 0 };

private:
    static constexpr std::size_t kMaxCachedFonts = 32u;
    std::unordered_map<std::string, std::shared_ptr<FontAsset>> m_fonts;
    std::atomic<std::uint64_t> m_fontCacheRevision{ 1 };
    std::shared_ptr<FontAsset> LoadFontFile(const file::path& path, std::string& error) const;
    bool ReloadCachedFont(const file::path& path, FileGuid guid, std::string& error);
    [[nodiscard]] own::shared_owner<const Texture> TryAcquireTexture(
        AssetDepot::AssetLink<Texture> link, const AssetDepot::TextureAssetVariant& variant);
    [[nodiscard]] AssetDepot::AssetRequest<Texture> RequestTextureAsync(
        AssetDepot::AssetLink<Texture> link, const AssetDepot::TextureAssetVariant& variant);
    // Dependency continuations use the parent's admitted resolver, never a
    // fresh public lookup which could silently bind a newer override.
    [[nodiscard]] AssetDepot::AssetRequest<Texture> RequestTextureAsyncFromSnapshot(
        AssetDepot::AssetLink<Texture> link, const AssetDepot::TextureAssetVariant& variant,
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog, std::uint64_t epoch);
    void RunTextureAssetWork(own::shared_owner<AssetDepot::TextureAssetWork> work);
    void CompleteTextureAssetWorkLocked(const own::shared_owner<AssetDepot::TextureAssetWork>& work,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        std::string message = {}, own::shared_owner<const Texture> texture = {});
    [[nodiscard]] own::shared_owner<const Texture::CodecImage> TryAcquireTextureImage(
        const own::shared_owner<const Texture>& descriptor);
    [[nodiscard]] AssetDepot::AssetRequest<Texture::CodecImage> RequestTextureImageAsync(
        const own::shared_owner<const Texture>& descriptor);
    [[nodiscard]] own::shared_owner<AssetDepot::TextureImageWork> StartTextureImageWorkLocked(
        const AssetDepot::TextureImageKey& key,
        const experiment::cooked::ResolvedAssetEntry& resolved,
        own::shared_owner<const AssetDepot::TextureImageSource> source = {});
    void RunTextureImageWork(own::shared_owner<AssetDepot::TextureImageWork> work);
    void CompleteTextureImageWorkLocked(const own::shared_owner<AssetDepot::TextureImageWork>& work,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        std::string message = {}, own::shared_owner<const Texture::CodecImage> image = {},
        own::shared_owner<const AssetDepot::TextureImageSource> source = {});
    void TrimTextureAssetsLocked(AssetDepot::TextureAssetRetiredEntries& retired);
    void StageTextureAssetRetirementLocked(AssetDepot::TextureAssetRetiredEntries& retired);
    void InvalidateTextureAssetsLocked(AssetDepot::TextureAssetRetiredEntries& retired) noexcept;
    AssetDepot::TextureAssetRuntimeState m_textureAssets{};

    template<class T>
    AssetDepot::ModelAssetCache<T>& ModelAssetCacheLocked();
    template<class T>
    own::shared_owner<const T> TryAcquireCurrentModelAsset(AssetDepot::AssetLink<T> link);
    template<class T>
    AssetDepot::AssetRequest<T> RequestCurrentModelAssetAsync(AssetDepot::AssetLink<T> link);
    template<class T>
    AssetDepot::AssetRequest<T> RequestModelAssetFromSnapshot(AssetDepot::AssetLink<T> link,
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog, std::uint64_t epoch);
    ModelPreparation PrepareModelAssetFromSnapshot(AssetDepot::AssetLink<assets::ModelAnimationDescriptor> link,
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog, std::uint64_t epoch,
        assets::ModelColliderPreparationPolicy colliderPolicy = assets::ModelColliderPreparationPolicy::CookedDefault);
    AssetDepot::AssetRequest<assets::ModelGeometryPayload> RequestModelColliderGeometry(
        experiment::cooked::ResolvedAssetEntry resolved,
        AssetDepot::AssetRequest<assets::ModelAnimationDescriptor> descriptor, std::uint64_t epoch,
        assets::ModelColliderPreparationPolicy colliderPolicy);
    template<class T>
    AssetDepot::AssetRequest<T> RequestResolvedModelAssetAsync(
        experiment::cooked::ResolvedAssetEntry resolved,
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog,
        bool exactGeneration, std::uint64_t epoch,
        experiment::cooked::ResolvedAssetEntry skeleton = {});
    template<class T>
    own::shared_owner<AssetDepot::ModelAssetWork<T>> StartModelAssetWorkLocked(
        const experiment::cooked::ResolvedAssetEntry& resolved,
        const own::shared_owner<const experiment::cooked::CookedAssetCatalog>& catalog,
        bool exactGeneration, std::uint64_t epoch,
        const experiment::cooked::ResolvedAssetEntry& skeleton);
    template<class T>
    void RunModelAssetWork(own::shared_owner<AssetDepot::ModelAssetWork<T>> work);
    template<class T>
    void CompleteModelAssetWorkLocked(const own::shared_owner<AssetDepot::ModelAssetWork<T>>& work,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        std::string message = {}, own::shared_owner<const T> asset = {});
    template<class T>
    own::shared_owner<const T> TryAcquireResolvedModelAssetLocked(
        const experiment::cooked::ResolvedAssetEntry& resolved, bool exactGeneration);
    void TrimModelAssetsLocked();
    void StageModelAssetRetirementLocked(AssetDepot::ModelAssetRetiredEntries& retired);
    void InvalidateModelAssetsLocked(AssetDepot::ModelAssetRetiredEntries& retired) noexcept;
    AssetDepot::ModelAssetRuntimeState m_modelAssets{};

    [[nodiscard]] own::shared_owner<const ShaderMeta> TryAcquireShaderMeta(
        AssetDepot::AssetLink<ShaderMeta> link);
    [[nodiscard]] AssetDepot::AssetRequest<ShaderMeta> RequestShaderMetaAsync(
        AssetDepot::AssetLink<ShaderMeta> link);
    void RunShaderMetaAssetWork(own::shared_owner<AssetDepot::ShaderMetaAssetWork> work);
    void CompleteShaderMetaAssetWorkLocked(const own::shared_owner<AssetDepot::ShaderMetaAssetWork>& work,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        std::string message = {}, const own::shared_owner<const ShaderMeta>& candidate = {});
    void StageMaterialAssetRetirementLocked(AssetDepot::MaterialAssetRetiredEntries& retired);
    void InvalidateMaterialAssetsLocked(AssetDepot::MaterialAssetRetiredEntries& retired) noexcept;
    AssetDepot::MaterialAssetRuntimeState m_materialAssets{};

    template<class T>
    AssetDepot::MaterialPipelineAssetCache<T>& MaterialPipelineAssetCacheLocked();
    template<class T>
    own::shared_owner<const T> TryAcquireCurrentMaterialPipelineAsset(AssetDepot::AssetLink<T> link);
    template<class T>
    AssetDepot::AssetRequest<T> RequestCurrentMaterialPipelineAssetAsync(AssetDepot::AssetLink<T> link);
    template<class T>
    AssetDepot::AssetRequest<T> RequestMaterialPipelineAssetFromSnapshot(AssetDepot::AssetLink<T> link,
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog, std::uint64_t epoch);
    template<class T>
    void RunMaterialPipelineAssetWork(own::shared_owner<AssetDepot::MaterialPipelineAssetWork<T>> work);
    template<class T>
    void CompleteMaterialPipelineAssetWorkLocked(
        const own::shared_owner<AssetDepot::MaterialPipelineAssetWork<T>>& work,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        std::string message = {}, const own::shared_owner<const T>& candidate = {});

    own::shared_owner<AssetBundlePreparation> SubmitAssetBundle(const AssetBundle& bundle);
    // Caller holds m_assetPreparationMutex through registration and submission.
    // exactGeneration is only for an already-pinned artifact/dependency closure;
    // it bypasses current-lookup invalidation, never lifecycle shutdown.
    job_handle SubmitAssetWorkLocked(job_group work,
        std::span<const job_handle> dependencies = {}, bool exactGeneration = false);
    own::shared_owner<PreparedRuntimeAsset> PrepareRuntimeAsset(
        FileGuid guid, const file::path& path, RuntimeAssetType type,
        std::optional<std::uint64_t> expectedEpoch = {},
        std::optional<std::uint64_t> expectedResolverRevision = {});
    bool PublishRuntimeAsset(const own::shared_owner<PreparedRuntimeAsset>& asset, std::string& error);
    void DrainAssetPreparations();
    bool ValidatePreparedMaterialTextures(Material& material, std::string& error);
    std::vector<assets::ModelAssetGeneration::Shared> SnapshotPreparedModelAssets() const;
    const own::shared_owner<AssetDepot::AssetRequestCounters> m_assetRequestCounters{
        own::make_shared<AssetDepot::AssetRequestCounters>() };
    mutable std::mutex m_assetPreparationMutex;
    std::size_t m_assetRootHandoffs{};
    // Policy is a request recipe, not another asset generation. Each policy's
    // valid preparation ticket remains independently publishable.
    using AssetPreparationKey = std::pair<FileGuid, assets::ModelColliderPreparationPolicy>;
    std::map<AssetPreparationKey, own::weak_owner<PreparedRuntimeAsset>> m_assetPreparations;
    std::vector<own::weak_owner<SceneAssetPreparation>> m_sceneAssetPreparations;
    std::vector<own::shared_owner<PreparedRuntimeAsset>> m_retiredAssetPreparations;
    std::vector<own::weak_owner<AssetBundlePreparation>> m_assetBundlePreparations;
    // Completion owns callback/capture teardown, even after a weak request expires.
    std::vector<job_handle> m_assetWork;
    std::vector<job_handle> m_assetPreparationLanes;
    std::size_t m_nextAssetPreparationLane{};
    std::uint64_t m_assetPreparationEpoch{ 1 };
    bool m_assetPreparationStopping{};
    std::size_t m_assetInvalidationDepth{};
	void LoadAssetCatalog(const file::path& root);
	[[nodiscard]] assets::ModelAssetGeneration::Shared LoadAndPublishModelAssetGeneration(
		FileGuid guid, bool allowEditorRecovery = false, bool publish = true,
        std::optional<std::uint64_t> expectedEpoch = {},
        std::optional<std::uint64_t> expectedResolverRevision = {});
	DataContainer<Texture>& TextureCacheFor(TextureFileType type);
	void RetireCachedAsset(RuntimeAssetType assetType, const file::path& path,
		FileGuid guid, bool remove);
	// MBC7 — 한 generation의 embedded texture owner를 전부 캐시에서 떼어 낸다.
	// 이미 그 owner를 붙든 Material은 자기 shared_ptr로 살려 두므로 그리는 중에
	// 사라지지 않는다 — 새로 해석하는 쪽만 새 generation의 것을 받는다.
	void RetireModelGenerationTextures(assets::ModelAssetGenerationHandle handle);
    struct LegacyCacheRetirement;
    // Retirement storage is constructed before outer locks. Stage may allocate
    // but mutates no cache/status; Detach is the nonthrowing root-commit phase.
    void StageLegacyCacheRetirementLocked(LegacyCacheRetirement& retired);
    void DetachLegacyCachesLocked(LegacyCacheRetirement& retired) noexcept;
	void InvalidateShaderMeta(FileGuid guid, bool remove);
	void SynchronizeLegacyMaterialProperties(Material& material) const;

private:
    own::shared_owner<const Material> AdoptAuthoringMaterialOwner(own::shared_owner<const Material> material);
    bool PublishMaterialGraphGeneration(own::shared_owner<const material_graph::Generation> generation,
                                       const own::shared_owner<const material_graph::Generation>& expectedGeneration,
                                       own::shared_owner<Material> candidate,
                                       const own::shared_owner<const Material>& expectedMaterial, std::string& error);

	//--------- current file count
	uint32 currModelFileCount = 0;
	uint32 currShaderFileCount = 0;
	uint32 currTextureFileCount = 0;
	uint32 currMaterialFileCount = 0;
	//--------- Data Thread and Editor Payload
	std::thread m_DataThread{};
	file::path m_dragDropPath{};
    // Both service root handles use m_cookedCatalogMutex. Writers additionally
    // hold admission first; readers copy a root and release before registry work.
    [[nodiscard]] own::shared_owner<AssetMetaRegistry> SnapshotAssetMetaRegistry() const;
	own::shared_owner<AssetMetaRegistry> m_assetMetaRegistry{};
    struct ShaderMetaCacheSlot
    {
        FileGuid guid{};
        std::uint32_t generation{};
        std::uint64_t resolverRevision{};
        bool occupied{};
        bool codeProgram{};
        own::weak_owner<const ShaderMeta> current;
        own::shared_owner<const ShaderMeta> retained;
        std::array<std::uint8_t, 32> documentDigest{};
        bool hasDocumentDigest{};
        mutable std::uint64_t lastUse{};
        std::size_t retainedBytes{};
    };
    void RetainShaderMetaLocked(ShaderMetaCacheSlot& slot,
        const own::shared_owner<const ShaderMeta>& owner, std::size_t charge,
        std::vector<own::shared_owner<const ShaderMeta>>& released);
    void TrimShaderMetaRetainedLocked(std::size_t targetBytes,
        std::vector<own::shared_owner<const ShaderMeta>>& released);
    std::uint64_t NextShaderMetaUseLocked() const noexcept;
    void ClearShaderMetaCache();
    [[nodiscard]] bool RegisterCodeShaderMetadataLocked(LX::Runtime::ShaderGeneration& shader,
        AssetDepot::MaterialProgramAssetOrigin& origin, std::string& failure);
    mutable std::mutex m_shaderMetaMutex;
    std::unordered_map<FileGuid, std::uint32_t> m_shaderMetaSlotByGuid;
    std::vector<ShaderMetaCacheSlot> m_shaderMetaSlots;
    std::vector<std::uint32_t> m_shaderMetaFreeSlots;
    std::size_t m_shaderMetaRetainedBudgetBytes = 16u * 1024u * 1024u;
    std::size_t m_shaderMetaRetainedBytes{};
    mutable std::uint64_t m_shaderMetaUseSerial{};
    // Monotonic across Finalize; exhaustion fails closed instead of wrapping.
    std::uint64_t m_shaderMetaGenerationSerial{};
    material_graph::GenerationStore m_materialGraphGenerations;
    bool LoadMaterialGraphProgram(FileGuid guid, const file::path& sourcePath, material_graph::CookedProgram& result,
                                  std::string& failure) const;
    std::mutex m_sceneMaterialMutex;
    bool m_recordingSceneMaterials{};
    std::vector<std::pair<FileGuid, file::path>> m_sceneMaterials;
    std::mutex m_pendingAssetChangeMutex;
	std::vector<RuntimeAssetChange> m_pendingAssetChanges;

    // Private transaction-local values only, never another cache/manager.
    // Destruction is deliberately after all outer admission/catalog guards.
    struct LegacyCacheRetirement final
    {
        assets::ModelAssetGenerationCache::RetiredEntries models;
        material_graph::GenerationStore::RetiredEntries graphs;
        std::unordered_map<std::string, std::shared_ptr<FontAsset>> fonts;
        DataContainer<Material> materials;
        std::unordered_map<FileGuid, asset_cache_detail::Entry<experiment::Material>> authoredMaterials;
        DataContainer<Texture> textures;
        DataContainer<Texture> uiTextures;
        DataContainer<Texture> spriteSheets;
        std::map<assets::ModelTextureHandle, asset_cache_detail::Entry<Texture>> modelTextures;
        std::map<assets::ModelAssetGenerationHandle, std::vector<assets::ModelTextureHandle>> modelTextureOwners;
        std::unordered_map<FileGuid, std::uint32_t> shaderSlotsByGuid;
        std::vector<ShaderMetaCacheSlot> shaderSlots;
        std::vector<std::uint32_t> shaderFreeSlots;
        std::map<AssetPreparationKey, own::weak_owner<PreparedRuntimeAsset>> preparations;
        std::vector<own::weak_owner<SceneAssetPreparation>> scenes;
        std::vector<own::weak_owner<AssetBundlePreparation>> bundles;
        std::vector<own::shared_owner<PreparedRuntimeAsset>> preparationPins;
        std::vector<own::shared_owner<SceneAssetPreparation>> scenePins;
        std::vector<own::shared_owner<AssetBundlePreparation>> bundlePins;
    };

};

static auto DataSystems = DataSystem::GetInstance();

