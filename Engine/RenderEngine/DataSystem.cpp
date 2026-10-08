#include "DataSystem.h"
#include <cassert>
#include "AssetDepot/LegacyResourceCharges.h"

#include "Experiment/Cooked/CookedAssetCatalog.h" // I7-C1 (MBC9: ExperimentModelMigration.cpp에서 이주)
#include "Experiment/Cooked/CookedAudioClipSource.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelMaterialGraph.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "Material.h" // MBC9: Model.h 전이 include가 사라져 직접 든다
#include "MaterialGraphSceneCompiler.h"
#include "Mesh.h"
#include "Texture.h"
#include "Sha256.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include <fstream> // I7-C1: manifest 읽기
#include <unordered_set>
#include <future>
#include <ppltasks.h>
#include <ppl.h>
#include "AuthoringBase64.h"
#include "Benchmark.hpp"
// SceneManager.h가 여기 있었다. LoadAssetBundle이 씬 매니저가 들고 있던
// 스레드풀을 빌려 쓰느라 층 3이 층 4를 올려다봤다. 풀의 소유를 층 1로
// 내리면서(현재 JobScheduler.h) 그 이유가 사라졌다 — PHASE 4-3 슬라이스 3.
#include "JobScheduler.h"
// Meta::Serialize / Deserialize. SceneManager.h가 ReflectionYml.h를 대신
// 끌어와 주던 자리다 — 빌려 쓰던 것을 직접 든다.
#include "ReflectionYml.h"
#include "AuthoringParsedDocument.h"
#include "AuthoringCookedDocument.h"
#include "SerializationProfiler.h" // D0: 부팅 catalog 파싱 기준선
#include "AuthoringNodeViewAccess.h" // D3-a-5b
#include "ShaderMeta.h"
#include "ShaderPermutationDomain.h"
#include "StandardMaterialProperty.h"
#include "Experiment/MaterialAuthoringCodec.h"
#include "ExperimentMaterialMigration.h"
#include "Assets/ModelSidecarV2.h"
#include "RHI/RHIFormat.h" // MBC7: generation embedded texture 포맷

#include <algorithm>
#include <iomanip>
#include <array>
#include <cctype>
#include <chrono>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <Windows.h>

// 검색 함수
bool HasImageFile(const file::path& directory)
{
	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_regular_file())
		{
			std::string ext = entry.path().extension().string();
			if (ext == ".png" || ext == ".jpg")
			{
				return true;
			}
		}
	}
	return false;
}

namespace
{
    using asset_cache_detail::LegacyTextureRetainedBytes;
    using asset_cache_detail::LegacyMaterialRetainedBytes;
    using asset_cache_detail::LegacyAuthoredMaterialRetainedBytes;

	constexpr std::array<char, 4> kMaterialPayloadMagic{ 'C', 'E', 'M', 'T' };
	constexpr std::uint16_t kMaterialPayloadVersion = 2;
	constexpr std::uint16_t kMaterialPayloadCookedDocumentEncoding = 2;
	constexpr std::uint32_t kMaxMaterialPayloadBytes = 4u * 1024u * 1024u;

	void WriteU16(std::ostream& output, std::uint16_t value)
	{
		const std::array<char, 2> bytes{
			static_cast<char>(value & 0xffu),
			static_cast<char>((value >> 8u) & 0xffu)
		};
		output.write(bytes.data(), bytes.size());
	}

	void WriteU32(std::ostream& output, std::uint32_t value)
	{
		const std::array<char, 4> bytes{
			static_cast<char>(value & 0xffu),
			static_cast<char>((value >> 8u) & 0xffu),
			static_cast<char>((value >> 16u) & 0xffu),
			static_cast<char>((value >> 24u) & 0xffu)
		};
		output.write(bytes.data(), bytes.size());
	}

	bool ReadU16(std::istream& input, std::uint16_t& value)
	{
		std::array<unsigned char, 2> bytes{};
		input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
		if (!input) return false;
		value = static_cast<std::uint16_t>(bytes[0])
			| (static_cast<std::uint16_t>(bytes[1]) << 8u);
		return true;
	}

	bool ReadU32(std::istream& input, std::uint32_t& value)
	{
		std::array<unsigned char, 4> bytes{};
		input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
		if (!input) return false;
		value = static_cast<std::uint32_t>(bytes[0])
			| (static_cast<std::uint32_t>(bytes[1]) << 8u)
			| (static_cast<std::uint32_t>(bytes[2]) << 16u)
			| (static_cast<std::uint32_t>(bytes[3]) << 24u);
		return true;
	}

    class AssetDecoderApartment
    {
    public:
        AssetDecoderApartment() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        {
            if (FAILED(m_result) && m_result != RPC_E_CHANGED_MODE)
            {
                throw std::runtime_error("Asset image decoder COM initialization failed.");
            }
        }
        ~AssetDecoderApartment()
        {
            if (SUCCEEDED(m_result))
            {
                CoUninitialize();
            }
        }
        AssetDecoderApartment(const AssetDecoderApartment&) = delete;
        AssetDecoderApartment& operator=(const AssetDecoderApartment&) = delete;
    private:
        HRESULT m_result;
    };

	std::string Lowercase(std::string value)
	{
		std::ranges::transform(value, value.begin(), [](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	RuntimeAssetType ResolveRuntimeAssetType(const file::path& path)
	{
		const std::string extension = Lowercase(path.extension().string());
		if (extension == ".fbx" || extension == ".gltf" ||
			extension == ".glb" || extension == ".obj")
		{
			return RuntimeAssetType::Model;
		}

		const std::string parent = Lowercase(path.parent_path().filename().string());
		if (extension == ".asset")
		{
			if (parent == "models") return RuntimeAssetType::Model;
			if (parent == "materials") return RuntimeAssetType::Material;
			return RuntimeAssetType::CatalogOnly;
		}

		if (extension == ".png" || extension == ".dds" ||
			extension == ".jpg" || extension == ".jpeg" || extension == ".hdr")
		{
			if (parent == "ui") return RuntimeAssetType::UITexture;
			if (parent == "spritesheets") return RuntimeAssetType::SpriteSheet;
			return RuntimeAssetType::Texture;
		}
		if (extension == ".shadermeta") return RuntimeAssetType::ShaderMeta;
        if (extension == ".shadergraph")
            return RuntimeAssetType::MaterialGraph;

        return RuntimeAssetType::CatalogOnly;
	}

	file::path ResolveRuntimeAssetPath(std::string_view requestedPath,
		std::string_view fallbackDirectory)
	{
		const file::path requested(requestedPath);
		std::error_code error;
		if (file::is_regular_file(requested, error) && !error) return requested;
		// G2 — 폴더를 가진 상대 경로는 Assets 기준 신원이다(TextureCacheKey 가 만든다).
		// 이름만 온 요청만 유형 폴더에서 다시 찾는다.
		if (requested.is_relative() && requested.has_parent_path())
		{
			const file::path underAssets = PathFinder::Relative() / requested;
			error.clear();
			if (file::is_regular_file(underAssets, error) && !error) return underAssets;
		}
		return PathFinder::Relative(std::string(fallbackDirectory)) / requested.filename();
	}

	// G2 — 텍스처 캐시의 신원. stem 은 신원이 아니다: 다른 폴더의 같은 이름,
	// 같은 이름의 다른 확장자가 한 칸을 나눠 먼저 온 쪽이 이겼다.
	// Assets 안이면 그 기준 상대 경로(저장해도 기계를 옮겨도 같은 파일로 돌아온다),
	// 밖이면 절대 경로. 적재·은퇴·번들 보존이 모두 이 함수 하나로 키를 만든다.
	std::string TextureCacheKey(const file::path& path)
	{
		const file::path absolute = file::absolute(path).lexically_normal();
		const file::path assets = file::absolute(PathFinder::Relative()).lexically_normal();
		const file::path relative = absolute.lexically_relative(assets);
		if (!relative.empty() && *relative.begin() != "..")
			return relative.generic_string();
		return absolute.generic_string();
	}

    std::string TextureRoleCacheKey(std::string_view sourceKey, DataSystem::TextureFileType type)
    {
        // UI/SpriteSheet are separate maps. General/material/terrain/HDR share
        // one map, so their role remains explicit in the representation key.
        switch (type)
        {
        case DataSystem::TextureFileType::MaterialTexture:
            return "role:material:" + std::string(sourceKey);
        case DataSystem::TextureFileType::TerrainTexture:
            return "role:terrain:" + std::string(sourceKey);
        case DataSystem::TextureFileType::HDR:
            return "role:hdr:" + std::string(sourceKey);
        default:
            return std::string(sourceKey);
        }
    }

	std::string MaterialTextureKeyPrefix(const file::path& path)
	{
		return "material:" + file::absolute(path).lexically_normal().generic_string() + ":";
	}

	std::string_view TextureFallbackDirectory(DataSystem::TextureFileType type)
	{
		switch (type)
		{
		case DataSystem::TextureFileType::Texture:         return "Textures\\";
		case DataSystem::TextureFileType::MaterialTexture: return "Materials\\";
		case DataSystem::TextureFileType::TerrainTexture:  return "Terrain\\Texture\\";
		case DataSystem::TextureFileType::HDR:             return "HDR\\";
		case DataSystem::TextureFileType::UITexture:       return "UI\\";
		case DataSystem::TextureFileType::SpriteSheet:     return "SpriteSheets\\";
		}
		return {};
	}

	bool RegisterAssetMeta(AssetMetaRegistry& registry, const FileGuid& guid,
		const file::path& path)
	{
		const AssetMetaRegistrationResult result = registry.Register(guid, path);
		if (AssetMetaRegistrationResult::Registered == result
			|| AssetMetaRegistrationResult::AlreadyRegistered == result)
		{
			return true;
		}

		std::string reason;
		switch (result)
		{
		case AssetMetaRegistrationResult::Invalid:
			reason = "invalid GUID/path";
			break;
		case AssetMetaRegistrationResult::GuidConflict:
			reason = "GUID already maps to " + registry.GetPath(guid).string();
			break;
		case AssetMetaRegistrationResult::PathConflict:
			reason = "path already maps to " + registry.GetGuid(path).ToString();
			break;
		default:
			reason = "unknown registration result";
			break;
		}

		Debug::PrintLog(spdlog::level::err, "Asset catalog rejected meta registration: guid="
			+ guid.ToString() + " path=" + path.string() + " reason=" + reason);
		return false;
	}
}

struct DataSystem::PreparedRuntimeAsset
{
    FileGuid guid;
    file::path path;
    file::path projectRoot;
    RuntimeAssetType type{};
    std::uint64_t epoch{};
    std::uint64_t resolverRevision{};
    std::atomic<bool> cancelled{};
    job_handle work;
    assets::ModelAssetGeneration::Shared model;
    own::shared_owner<const material_graph::PreparedGeneration> graph;
    std::string error;
    bool published{}; // Scene owner only.
};

struct DataSystem::SceneAssetPreparation
{
    file::path projectRoot;
    std::string name;
    std::uint64_t epoch{};
    std::atomic<bool> cancelled{}, finished{};
    std::atomic<unsigned> phase{}; // Discovery, models, graphs, resources, ready.
    std::atomic<std::size_t> completed{}, total{};
    std::vector<own::shared_owner<PreparedRuntimeAsset>> models, graphs;
    std::map<FileGuid, file::path> graphPaths;
    std::vector<std::vector<std::byte>> materialDocuments;
    AssetBundle resources;
    // Written only by the resource worker and retained through the caller's
    // scene-construction handoff. Cache-budget eviction cannot drop preload pins.
    std::vector<own::shared_owner<const Material>> materialPins;
    std::vector<own::shared_owner<const experiment::Material>> authoredMaterialPins;
    std::vector<own::shared_owner<const Texture>> texturePins;
    job_handle resourceWork;
    std::string error;
};

AssetBundleLoadResult DataSystem::AssetBundlePreparation::Snapshot() const
{
    std::lock_guard lock(mutex);
    return result;
}

job_handle DataSystem::AssetBundlePreparation::Completion() const
{
    std::lock_guard lock(mutex);
    return work;
}

void DataSystem::AssetBundlePreparation::Cancel()
{
    std::lock_guard lock(mutex);
    if (result.status == AssetDepot::AssetRequestStatus::Pending)
    {
        cancelled.store(true, std::memory_order_release);
    }
}

job_handle DataSystem::SubmitAssetWorkLocked(job_group work,
    std::span<const job_handle> dependencies, bool exactGeneration)
{
    if (m_assetPreparationStopping || (!exactGeneration && m_assetInvalidationDepth != 0))
    {
        throw std::runtime_error("Asset work is shutting down or invalidated.");
    }
    std::erase_if(m_assetWork, [](const auto& handle) { return handle.is_complete(); });
    // Allocate tracking storage first: no accepted DataSystem capture may be
    // orphaned by an allocation failure after scheduler submission.
    m_assetWork.emplace_back();
    try
    {
        m_assetWork.back() = ce::get_job_scheduler().submit_after(dependencies, std::move(work));
    }
    catch (...)
    {
        m_assetWork.pop_back();
        throw;
    }
    return m_assetWork.back();
}

own::shared_owner<DataSystem::PreparedRuntimeAsset> DataSystem::PrepareRuntimeAsset(
    FileGuid guid, const file::path& path, RuntimeAssetType type,
    std::optional<std::uint64_t> expectedEpoch,
    std::optional<std::uint64_t> expectedResolverRevision)
{
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        throw std::runtime_error("Asset preparation is shutting down.");
    }
    if ((expectedEpoch && *expectedEpoch != m_assetPreparationEpoch)
        || (expectedResolverRevision && *expectedResolverRevision != m_assetDepotRevision))
    {
        return {};
    }
    if (const auto found = m_assetPreparations.find(guid); found != m_assetPreparations.end())
    {
        if (const auto existing = found->second.lock(); existing
            && existing->epoch == m_assetPreparationEpoch && existing->resolverRevision == m_assetDepotRevision
            && !existing->cancelled.load(std::memory_order_acquire)
            && (!existing->work.is_complete() || existing->error.empty()))
        {
            return existing;
        }
        m_assetPreparations.erase(found); // Failed/expired requests may be retried.
    }
    std::erase_if(m_assetPreparations, [](const auto& entry) { return entry.second.expired(); });
    std::erase_if(m_retiredAssetPreparations, [](const auto& entry) { return entry->work.is_complete(); });
    auto asset = own::make_shared<PreparedRuntimeAsset>();
    asset->guid = guid;
    asset->path = path;
    asset->projectRoot = PathFinder::Relative();
    asset->type = type;
    asset->epoch = m_assetPreparationEpoch;
    asset->resolverRevision = m_assetDepotRevision;
    const auto request = type == RuntimeAssetType::MaterialGraph
        ? m_materialGraphGenerations.BeginPreparation(experiment::AssetId{guid.m_guid}, false, asset->error)
        : material_graph::GenerationPreparationRequest{};
    const auto workers = ce::get_thread_pool().size();
    const auto lanes = (std::min)(RHIShaderCompiler::MaxParallelCompiles(),
        workers > 2 ? workers - 2 : std::size_t{1});
    if (m_assetPreparationLanes.size() != lanes)
    {
        m_assetPreparationLanes.resize(lanes);
    }
    auto& lane = m_assetPreparationLanes[m_nextAssetPreparationLane++ % lanes];
    job_group work;
    work.add([this, asset, request]
    {
        try
        {
            {
                std::lock_guard lock(m_assetPreparationMutex);
                if (asset->cancelled.load(std::memory_order_acquire) ||
                    m_assetPreparationStopping || asset->epoch != m_assetPreparationEpoch
                    || asset->resolverRevision != m_assetDepotRevision)
                {
                    asset->error = "Asset preparation was cancelled or invalidated.";
                    return;
                }
            }
            const AssetDecoderApartment apartment;
            if (asset->type == RuntimeAssetType::MaterialGraph)
            {
                asset->graph = material_graph::GenerationStore::Prepare(request,
                    [this, asset](material_graph::CookedProgram& result, std::string& error)
                    {
                        return LoadMaterialGraphProgram(asset->guid, asset->path, result, error);
                    }, asset->error);
            }
            else
            {
                {
                    std::lock_guard admissionLock(m_assetPreparationMutex);
                    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
                        || asset->epoch != m_assetPreparationEpoch || asset->resolverRevision != m_assetDepotRevision
                        || asset->cancelled.load(std::memory_order_acquire))
                    {
                        asset->error = "Model preparation resolver changed.";
                        return;
                    }
                    // Root changes clear this current index under the same lock.
                    asset->model = m_modelAssetGenerations.ResolveCurrent(asset->guid.m_guid);
                }
                if (!asset->model)
                {
                    asset->model = LoadAndPublishModelAssetGeneration(asset->guid, true, false,
                        asset->epoch, asset->resolverRevision);
                }
                if (!asset->model)
                {
                    asset->error = "Model preparation failed: " + asset->path.string();
                }
            }
        }
        catch (const std::exception& error)
        {
            asset->error = error.what();
        }
        catch (...)
        {
            asset->error = "Unexpected asset preparation failure.";
        }
    });
    const std::array dependencies{lane};
    asset->work = lane.valid()
        ? SubmitAssetWorkLocked(std::move(work), dependencies)
        : SubmitAssetWorkLocked(std::move(work));
    lane = asset->work;
    m_assetPreparations.emplace(guid, asset);
    return asset;
}

bool DataSystem::PublishRuntimeAsset(const own::shared_owner<PreparedRuntimeAsset>& asset, std::string& error)
{
    // No file reads, compilation or waits under the publication lock. Asset
    // changes erase their request before a stale completion can publish.
    std::lock_guard lock(m_assetPreparationMutex);
    const auto found = m_assetPreparations.find(asset->guid);
    const auto current = found == m_assetPreparations.end()
        ? own::shared_owner<PreparedRuntimeAsset>{} : found->second.lock();
    if (asset->cancelled.load(std::memory_order_acquire) ||
        m_assetPreparationStopping || m_assetInvalidationDepth != 0 || asset->epoch != m_assetPreparationEpoch || asset->resolverRevision != m_assetDepotRevision ||
        asset->projectRoot != PathFinder::Relative() ||
        !current || current.borrow().unsafe_get() != asset.borrow().unsafe_get())
    {
        error = "Asset changed while preparing the scene: " + asset->path.string();
        return false;
    }
    if (!asset->error.empty())
    {
        error = asset->error;
        return false;
    }
    if (asset->published)
    {
        return true;
    }
    if (asset->type == RuntimeAssetType::MaterialGraph)
    {
        if (!asset->graph || !m_materialGraphGenerations.Publish(*asset->graph, error))
        {
            return false;
        }
    }
    else
    {
        auto result = m_modelAssetGenerations.Publish(asset->model);
        if (!result.Succeeded())
        {
            error = "Prepared model was rejected: " + asset->path.string();
            return false;
        }
        asset->model = result.current;
        if (result.retiredHandle.IsValid())
        {
            RetireModelGenerationTextures(result.retiredHandle);
        }
    }
    asset->published = true;
    return true;
}

DataSystem::ModelPreparation DataSystem::PrepareModelAssetByPath(std::string_view path)
{
    std::uint64_t epoch{}, resolverRevision{};
    own::shared_owner<AssetMetaRegistry> registry;
    {
        std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        epoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
        registry = SnapshotAssetMetaRegistry();
    }
    const auto source = ResolveRuntimeAssetPath(path, "Models\\");
    auto guid = registry ? registry->GetGuid(source) : FileGuid{};
    if (guid == FileGuid{} && registry)
    {
        guid = registry->GetFilenameToGuid(file::path(path).filename().string());
    }
    if (guid == FileGuid{} || !assets::IsUuidV8(guid.m_guid))
    {
        return {};
    }
    return PrepareRuntimeAsset(guid, source, RuntimeAssetType::Model, epoch, resolverRevision);
}

job_handle DataSystem::ModelPreparationCompletion(const ModelPreparation& preparation) const
{
    return preparation ? preparation->work : job_handle{};
}

assets::ModelAssetGeneration::Shared DataSystem::ReadPreparedModel(
    const ModelPreparation& preparation, std::string& error) const
{
    error.clear();
    std::lock_guard lock(m_assetPreparationMutex);
    if (!preparation || m_assetPreparationStopping || m_assetInvalidationDepth != 0 ||
        preparation->cancelled.load(std::memory_order_acquire) || preparation->epoch != m_assetPreparationEpoch ||
        preparation->resolverRevision != m_assetDepotRevision ||
        preparation->projectRoot != PathFinder::Relative() || !preparation->work.is_complete())
    {
        error = "Model preparation is incomplete or invalidated.";
        return {};
    }
    error = preparation->error;
    return preparation->model;
}

bool DataSystem::PublishPreparedModel(const ModelPreparation& preparation, std::string& error)
{
    if (!preparation || !preparation->work.is_complete())
    {
        error = "Model preparation is not complete.";
        return false;
    }
    return PublishRuntimeAsset(preparation, error);
}

own::shared_owner<DataSystem::SceneAssetPreparation> DataSystem::PrepareSceneAssets(
    const Authoring::ReadNode& root, const AssetBundle& bundle, const file::path& scene)
{
    auto preparation = own::make_shared<SceneAssetPreparation>();
    preparation->projectRoot = PathFinder::Relative();
    preparation->name = scene.filename().string();
    preparation->resources = bundle;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            throw std::runtime_error("Scene asset preparation is shutting down or invalidated.");
        }
        preparation->epoch = m_assetPreparationEpoch;
        std::erase_if(m_sceneAssetPreparations, [](const auto& weak) { return weak.expired(); });
        m_sceneAssetPreparations.emplace_back(preparation);
    }
    // Follow current scene/material/prefab references, including the first open.
    // Historical ScenePreload lists are not a correctness dependency.
    std::map<FileGuid, file::path> models;
    std::unordered_set<FileGuid> seen;
    std::vector<std::pair<FileGuid, file::path>> documents;
    const auto discover = [&](FileGuid guid)
    {
        if (guid == FileGuid{} || !seen.insert(guid).second)
        {
            return;
        }
        if (seen.size() > 65536)
        {
            throw std::runtime_error("Scene asset dependency limit exceeded.");
        }
        const auto path = GetFilePath(guid);
        if (path.empty())
        {
            return; // Embedded identities are resolved from their model below.
        }
        const auto type = ResolveRuntimeAssetType(path);
        if (type == RuntimeAssetType::Model)
        {
            models.emplace(guid, path);
        }
        else if (type == RuntimeAssetType::MaterialGraph)
        {
            preparation->graphPaths.emplace(guid, path);
        }
        else if (type == RuntimeAssetType::Material)
        {
            AssetEntry entry(ManagedAssetType::Material, path.stem());
            if (!preparation->resources.ContainsAsset(entry))
            {
                preparation->resources.AddAsset(entry);
            }
        }
        const auto extension = Lowercase(path.extension().string());
        if (extension == ".prefab" || (extension == ".asset" && type != RuntimeAssetType::Model) ||
            (extension == ".shadergraph" && PathFinder::IsAssetAuthoringEnabled()))
        {
            documents.emplace_back(guid, path);
        }
    };
    std::function<void(const Authoring::ReadNode&, std::size_t)> walk;
    walk = [&](const Authoring::ReadNode& node, std::size_t depth)
    {
        if (depth > 256)
        {
            throw std::runtime_error("Scene asset document nesting limit exceeded.");
        }
        if (node.IsScalar())
        {
            FileGuid guid;
            if (Uuid::TryParse(node.AsString(), guid.m_guid))
            {
                discover(guid);
            }
            return;
        }
        if (node.IsMap())
        {
            if (const auto material = node["m_Material"]; material && material.IsMap())
            {
                std::vector<std::byte> bytes;
                std::string error;
                if (!Authoring::EncodeCookedDocument(material, bytes, error))
                {
                    throw std::runtime_error(error);
                }
                preparation->materialDocuments.push_back(std::move(bytes));
            }
            for (const auto entry : node.Map())
            {
                const auto key = entry.key.Scalar();
                if (key == "m_valueYaml" && entry.value.IsScalar() && !entry.value.Scalar().empty())
                {
                    std::string error;
                    auto document = PathFinder::IsAssetAuthoringEnabled()
                        ? Authoring::WriteDocument::ParseText(entry.value.AsString(), &error)
                        : Authoring::DecodeCookedDocumentTextEnvelope(entry.value.Scalar(), error);
                    if (!document)
                    {
                        throw std::runtime_error("Prefab override dependency could not be read: " + error);
                    }
                    if (node["m_propertyName"].Scalar() == "m_Material")
                    {
                        std::vector<std::byte> bytes;
                        if (!Authoring::EncodeCookedDocument(document->Root().Read(), bytes, error))
                        {
                            throw std::runtime_error(error);
                        }
                        preparation->materialDocuments.push_back(std::move(bytes));
                    }
                    walk(document->Root().Read(), depth + 1);
                }
                else
                {
                    if (key == "texturePaths" && entry.value.IsSequence())
                    {
                        for (const auto texture : entry.value)
                        {
                            if (texture.IsScalar() && !texture.Scalar().empty())
                            {
                                AssetEntry asset(ManagedAssetType::UITexture, file::path(texture.AsString()));
                                if (!preparation->resources.ContainsAsset(asset))
                                {
                                    preparation->resources.AddAsset(asset);
                                }
                            }
                        }
                    }
                    if (key == "m_modelName" && entry.value.IsScalar() && !entry.value.Scalar().empty())
                    {
                        discover(GetStemToGuid(entry.value.AsString()));
                    }
                    if ((key == "m_SpritePath" || key == "m_spriteSheetPath") &&
                        entry.value.IsScalar() && !entry.value.Scalar().empty())
                    {
                        AssetEntry asset(key == "m_SpritePath" ? ManagedAssetType::Texture : ManagedAssetType::SpriteSheet,
                            file::path(entry.value.AsString()));
                        if (!preparation->resources.ContainsAsset(asset))
                        {
                            preparation->resources.AddAsset(asset);
                        }
                    }
                    if (key == "graphAssetId" && entry.value.IsScalar())
                    {
                        FileGuid guid;
                        if (Uuid::TryParse(entry.value.AsString(), guid.m_guid) && guid != FileGuid{})
                        {
                            preparation->graphPaths.emplace(guid, GetFilePath(guid));
                        }
                    }
                    walk(entry.value, depth + 1);
                }
            }
            return;
        }
        for (const auto child : node)
        {
            walk(child, depth + 1);
        }
    };
    walk(root, 0);
    for (const auto& entry : bundle.assets)
    {
        const auto type = static_cast<ManagedAssetType>(entry.assetTypeID);
        const auto path = type == ManagedAssetType::Material
            ? PathFinder::Relative("Materials\\") / (file::path(entry.assetName).stem().string() + ".asset")
            : ResolveRuntimeAssetPath(entry.assetName, type == ManagedAssetType::Model ? "Models\\" : "Textures\\");
        auto guid = GetFileGuid(path);
        if (guid == FileGuid{} && type == ManagedAssetType::Model)
        {
            guid = GetFilenameToGuid(file::path(entry.assetName).filename().string());
        }
        if (guid == FileGuid{} && type == ManagedAssetType::Model)
        {
            throw std::runtime_error("Required model identity could not be resolved: " + entry.assetName);
        }
        discover(guid);
    }
    std::erase_if(preparation->resources.assets, [](const AssetEntry& entry)
    {
        const auto type = static_cast<ManagedAssetType>(entry.assetTypeID);
        return type == ManagedAssetType::Model || type == ManagedAssetType::SpriteFont;
    });
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        const auto [guid, path] = documents[index];
        const auto resolved = Lowercase(path.extension().string()) == ".shadergraph"
            ? path : ResolveCatalogAssetPath(guid);
        std::string error;
        const auto document = Authoring::ParsedDocument::ParseFile(resolved.string(), error);
        if (!document)
        {
            if (PathFinder::IsAssetAuthoringEnabled() && Lowercase(path.extension().string()) == ".shadergraph")
            {
                Debug::PrintLog(spdlog::level::warn, "Graph dependency discovery failed; material construction will recover: "
                    + path.string() + " " + error);
                continue;
            }
            throw std::runtime_error("Asset dependency document could not be read: " + path.string() + " " + error);
        }
        walk(document.Root(), 0);
    }
    for (const auto& [guid, path] : models)
    {
        preparation->models.push_back(PrepareRuntimeAsset(guid, path, RuntimeAssetType::Model));
    }
    preparation->total.store(preparation->models.size(), std::memory_order_relaxed);
    preparation->phase.store(1, std::memory_order_release);
    return preparation;
}

bool DataSystem::PollSceneAssets(const own::shared_owner<SceneAssetPreparation>& preparation,
    bool wait, bool publish, std::string& error)
{
    error.clear();
    if (!preparation)
    {
        return true;
    }
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (!publish || m_assetPreparationStopping || m_assetInvalidationDepth != 0 ||
            preparation->epoch != m_assetPreparationEpoch ||
            preparation->projectRoot != PathFinder::Relative())
        {
            preparation->cancelled.store(true, std::memory_order_release);
        }
    }
    const auto finish = [&](std::string message)
    {
        preparation->error = std::move(message);
        preparation->finished.store(true, std::memory_order_release);
        error = preparation->error;
        return true;
    };
    const auto ready = [&](const auto& requests)
    {
        std::size_t completed = 0;
        for (const auto& asset : requests)
        {
            if (!wait && !asset->work.is_complete())
            {
                continue;
            }
            asset->work.wait(); // Never incomplete in the normal frame pump.
            ++completed;
        }
        preparation->completed.store(completed, std::memory_order_relaxed);
        return completed == requests.size();
    };
    if (preparation->finished.load(std::memory_order_acquire))
    {
        error = preparation->error;
        return true;
    }
    if (preparation->phase.load(std::memory_order_acquire) == 1)
    {
        if (!ready(preparation->models))
        {
            return false;
        }
        if (preparation->cancelled.load(std::memory_order_acquire))
        {
            return finish("Scene asset preparation was cancelled.");
        }
        for (const auto& asset : preparation->models)
        {
            if (!PublishRuntimeAsset(asset, error))
            {
                return finish(error);
            }
            for (const auto& material : asset->model->Materials())
            {
                const FileGuid graph(assets::ModelMaterialGraphId(material.materialId));
                preparation->graphPaths.insert_or_assign(graph, PathFinder::IsAssetAuthoringEnabled()
                    ? assets::ModelMaterialGraphPath(preparation->projectRoot, asset->model->Identity().modelId,
                        material.materialId) : file::path{});
            }
        }
        for (const auto& [guid, path] : preparation->graphPaths)
        {
            preparation->graphs.push_back(PrepareRuntimeAsset(guid, path, RuntimeAssetType::MaterialGraph));
        }
        preparation->completed.store(0, std::memory_order_relaxed);
        preparation->total.store(preparation->graphs.size(), std::memory_order_relaxed);
        preparation->phase.store(2, std::memory_order_release);
    }
    if (preparation->phase.load(std::memory_order_acquire) == 2)
    {
        if (!ready(preparation->graphs))
        {
            return false;
        }
        if (preparation->cancelled.load(std::memory_order_acquire))
        {
            return finish("Scene asset preparation was cancelled.");
        }
        for (const auto& asset : preparation->graphs)
        {
            if (!PublishRuntimeAsset(asset, error))
            {
                if (!PathFinder::IsAssetAuthoringEnabled())
                {
                    return finish(error);
                }
                Debug::PrintLog(spdlog::level::warn, "Scene graph will use an editable default material: " + error);
                error.clear();
            }
        }
        // Registration and handle assignment share the shutdown admission lock.
        // Drain must see this job, or reject it before the scheduler accepts it.
        std::lock_guard lock(m_assetPreparationMutex);
        if (preparation->cancelled.load(std::memory_order_acquire) ||
            m_assetPreparationStopping || m_assetInvalidationDepth != 0 ||
            preparation->epoch != m_assetPreparationEpoch)
        {
            return finish("Scene asset preparation was cancelled or invalidated.");
        }
        preparation->completed.store(0, std::memory_order_relaxed);
        preparation->total.store(preparation->resources.assets.size() + preparation->graphs.size() +
            preparation->materialDocuments.size(), std::memory_order_relaxed);
        job_group work;
        work.add([this, preparation]
        {
            const AssetDecoderApartment apartment;
            // Only CPU texture owners/material instances. RHI upload/fences and
            // Scene/Entity construction stay on their existing owning threads.
            for (const auto& bytes : preparation->materialDocuments)
            {
                if (preparation->cancelled.load(std::memory_order_acquire))
                {
                    return;
                }
                std::string error;
                const auto document = Authoring::ParsedDocument::ParseCooked(bytes, error);
                if (!document)
                {
                    throw std::runtime_error(error);
                }
                const auto node = document.Root();
                if (const auto reference = node["ref"]; reference && reference.IsScalar())
                {
                    const auto path = GetFilePath(FileGuid(reference.AsString()));
                    const auto base = LoadMaterialShared(path.stem().string());
                    if (!base)
                    {
                        throw std::runtime_error("Required base material could not be prepared.");
                    }
                    preparation->materialPins.push_back(base);
                    if (auto authored = LoadAuthoredMaterialShared(base->m_fileGuid))
                    {
                        preparation->authoredMaterialPins.push_back(std::move(authored));
                    }
                    Material material(*base);
                    for (const auto override : node["overrides"])
                    {
                        experiment::MaterialProperty property;
                        property.name = override["name"].AsString();
                        if (!experiment::DeserializeMaterialPropertyValue(override, property.name, property.value, error) ||
                            !ExperimentMaterialMigration::ApplyPropertyToLegacy(material, property, error))
                        {
                            throw std::runtime_error(error);
                        }
                    }
                    FinalizeMaterialRuntime(material);
                    if (!ValidatePreparedMaterialTextures(material, error))
                    {
                        throw std::runtime_error(error);
                    }
                    preparation->materialPins.push_back(own::make_shared<const Material>(std::move(material)));
                }
                else
                {
                    Material material;
                    if (!DeserializeMaterialPayload(material, Authoring::NodeViewAccess::Make(node), nullptr, false) ||
                        !ValidatePreparedMaterialTextures(material, error))
                    {
                        throw std::runtime_error("Required inline material could not be prepared: " + error);
                    }
                    // Preserve successfully prepared dependencies until owner construction.
                    // A deferred default graph remains the owner's recovery decision.
                    preparation->materialPins.push_back(own::make_shared<const Material>(std::move(material)));
                }
                ++preparation->completed;
            }
            for (const auto& graph : preparation->graphs)
            {
                if (preparation->cancelled.load(std::memory_order_acquire))
                {
                    return;
                }
                Material material;
                material_graph::InstanceDescription description;
                description.graphId.value = graph->guid.m_guid;
                std::string failure;
                if (!ConfigureMaterialGraph(material, description, failure))
                {
                    if (!PathFinder::IsAssetAuthoringEnabled())
                    {
                        throw std::runtime_error(failure);
                    }
                    // The model/inline owner constructs and persists its replacement.
                }
                else
                {
                    preparation->materialPins.push_back(own::make_shared<const Material>(std::move(material)));
                }
                ++preparation->completed;
            }
            for (const auto& entry : preparation->resources.assets)
            {
                if (preparation->cancelled.load(std::memory_order_acquire))
                {
                    return;
                }
                switch (static_cast<ManagedAssetType>(entry.assetTypeID))
                {
                case ManagedAssetType::Model:
                    break;
                case ManagedAssetType::Material:
                {
                    const auto loaded = LoadMaterialShared(file::path(entry.assetName).stem().string());
                    if (!loaded)
                    {
                        throw std::runtime_error("Required material could not be prepared: " + entry.assetName);
                    }
                    preparation->materialPins.push_back(loaded);
                    if (auto authored = LoadAuthoredMaterialShared(loaded->m_fileGuid))
                    {
                        preparation->authoredMaterialPins.push_back(std::move(authored));
                    }
                    Material material(*loaded);
                    std::string failure;
                    if (!ValidatePreparedMaterialTextures(material, failure))
                    {
                        throw std::runtime_error(failure);
                    }
                    preparation->materialPins.push_back(own::make_shared<const Material>(std::move(material)));
                    break;
                }
                case ManagedAssetType::Texture:
                {
                    auto texture = LoadTexture(entry.assetName);
                    if (!texture)
                    {
                        throw std::runtime_error("Required texture could not be prepared: " + entry.assetName);
                    }
                    preparation->texturePins.push_back(std::move(texture));
                    break;
                }
                case ManagedAssetType::UITexture:
                {
                    auto texture = LoadTexture(entry.assetName, TextureFileType::UITexture);
                    if (!texture)
                    {
                        throw std::runtime_error("Required texture could not be prepared: " + entry.assetName);
                    }
                    preparation->texturePins.push_back(std::move(texture));
                    break;
                }
                case ManagedAssetType::SpriteSheet:
                {
                    auto texture = LoadTexture(entry.assetName, TextureFileType::SpriteSheet);
                    if (!texture)
                    {
                        throw std::runtime_error("Required texture could not be prepared: " + entry.assetName);
                    }
                    preparation->texturePins.push_back(std::move(texture));
                    break;
                }
                default:
                    break;
                }
                ++preparation->completed;
            }
        });
        preparation->resourceWork = SubmitAssetWorkLocked(std::move(work));
        preparation->phase.store(3, std::memory_order_release);
    }
    if (!wait && !preparation->resourceWork.is_complete())
    {
        return false;
    }
    try
    {
        preparation->resourceWork.wait();
    }
    catch (const std::exception& failure)
    {
        return finish(failure.what());
    }
    preparation->phase.store(4, std::memory_order_release);
    return finish(preparation->cancelled.load(std::memory_order_acquire)
        ? "Scene asset preparation was cancelled." : std::string{});
}

bool DataSystem::ValidatePreparedMaterialTextures(Material& material, std::string& error)
{
    if (material.HasMaterialGraph())
    {
        return true; // BuildInstance already requires every declared texture owner.
    }
    for (const auto& model : SnapshotPreparedModelAssets())
    {
        BindModelGenerationTextures(material, *model);
    }
    for (const auto& property : material.m_propertyValues)
    {
        if (property.m_textureGuid != FileGuid{} && !material.GetTextureMapShared(property.m_name))
        {
            error = "Required material texture could not be prepared: " + property.m_name;
            return false;
        }
    }
    const std::array<std::pair<std::string_view, std::string_view>, 5> legacyNames{{
        {standard_material::property::BaseColorMap, material.m_baseColorTexName},
        {standard_material::property::NormalMap, material.m_normalTexName},
        {standard_material::property::OrmMap, material.m_ORM_TexName},
        {standard_material::property::AoMap, material.m_AO_TexName},
        {standard_material::property::EmissiveMap, material.m_EmissiveTexName}}};
    for (const auto& [property, name] : legacyNames)
    {
        if (!name.empty() && !material.GetTextureMapShared(property))
        {
            error = "Required material texture could not be prepared: " + std::string(name);
            return false;
        }
    }
    return true;
}

void DataSystem::CancelSceneAssets(const own::shared_owner<SceneAssetPreparation>& preparation)
{
    if (preparation)
    {
        preparation->cancelled.store(true, std::memory_order_release);
    }
}

DataSystem::AssetPreparationProgress DataSystem::SnapshotAssetPreparationProgress() const
{
    AssetPreparationProgress result;
    std::lock_guard lock(m_assetPreparationMutex);
    for (const auto& weak : m_sceneAssetPreparations)
    {
        const auto preparation = weak.lock();
        if (!preparation || preparation->finished.load(std::memory_order_acquire) ||
            preparation->cancelled.load(std::memory_order_acquire))
        {
            continue;
        }
        ++result.activeRequests;
        if (result.activeRequests == 1)
        {
            result.name = preparation->name;
            const auto phase = preparation->phase.load(std::memory_order_acquire);
            result.phase = phase == 0 ? "Discovering dependencies" : phase == 1 ? "Preparing models" :
                phase == 2 ? "Compiling materials" : "Preparing textures/materials";
            result.total = preparation->total.load(std::memory_order_relaxed);
            result.completed = (std::min)(result.total, preparation->completed.load(std::memory_order_relaxed));
        }
    }
    return result;
}

void DataSystem::DrainAssetPreparations()
{
    if (thread_pool::is_worker_thread())
    {
        // job_handle::wait rejects incomplete worker waits. Never mistake that
        // rejection for a completed job and tear down its captured service.
        throw std::logic_error("DataSystem shutdown requires the lifecycle owner thread.");
    }
    std::vector<own::shared_owner<PreparedRuntimeAsset>> assets;
    std::vector<own::shared_owner<SceneAssetPreparation>> scenes;
    std::vector<own::shared_owner<AssetBundlePreparation>> bundles;
    std::vector<job_handle> work;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        m_assetPreparationStopping = true;
        ++m_assetPreparationEpoch;
        assets = m_retiredAssetPreparations;
        for (const auto& [guid, weak] : m_assetPreparations)
        {
            if (auto asset = weak.lock())
            {
                assets.push_back(std::move(asset));
            }
        }
        for (const auto& asset : assets)
        {
            asset->cancelled.store(true, std::memory_order_release);
        }
        for (const auto& weak : m_sceneAssetPreparations)
        {
            if (auto scene = weak.lock())
            {
                scene->cancelled.store(true, std::memory_order_release);
                scenes.push_back(std::move(scene));
            }
        }
        for (const auto& weak : m_assetBundlePreparations)
        {
            if (auto bundle = weak.lock())
            {
                bundles.push_back(std::move(bundle));
            }
        }
        m_assetBundlePreparations.clear();
        for (const auto& bundle : bundles)
        {
            bundle->cancelled.store(true, std::memory_order_release);
        }
        work.swap(m_assetWork);
    }
    // Waiting also releases the scheduler callbacks/captures. Request owners
    // stay strong until every accepted preparation, prewarm and bundle ends.
    for (const auto& handle : work)
    {
        try
        {
            handle.wait();
        }
        catch (...)
        {
            // A task failure is terminal; its caller's handle still reports it.
        }
    }
    std::lock_guard lock(m_assetPreparationMutex);
    m_assetPreparations.clear();
    m_sceneAssetPreparations.clear();
    m_retiredAssetPreparations.clear();
    m_assetPreparationLanes.clear();
    m_nextAssetPreparationLane = 0;
}

DataSystem::~DataSystem()
{
	Finalize();
}

void DataSystem::Initialize()
{
    auto registry = own::make_shared<AssetMetaRegistry>();
    own::shared_owner<AssetMetaRegistry> retiredRegistry;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        retiredRegistry = std::move(m_assetMetaRegistry);
        m_assetMetaRegistry = std::move(registry);
        m_assetPreparationStopping = false;
    }
	const bool authoring = PathFinder::IsAssetAuthoringEnabled();
	if (authoring)
	{
		// Editor는 source catalog가 정본이다. efsw change publication도 이 표를
		// 갱신하므로 부팅 때 sidecar를 읽는 기존 계약을 유지한다.
		LoadAssetCatalog(PathFinder::Relative());
	}

	// D5 cutover — packaged Player는 CEMF source identity table을 정본으로 삼고
	// `.meta` tree를 전혀 열거하지 않는다. Editor의 optional cooked cache mount는
	// source registry를 건드리지 않는다.
	const auto cookedCatalogStart = std::chrono::steady_clock::now();
	std::string cookedCatalogError;
	const bool mounted = MountCookedCatalog(
		PathFinder::Relative(), cookedCatalogError);
	if (!mounted && !cookedCatalogError.empty())
	{
		if (!authoring)
			throw std::runtime_error("Packaged cooked catalog mount failed: "
				+ cookedCatalogError);
		Debug::PrintLog(spdlog::level::warn, "[cooked.catalog] 마운트 실패: " + cookedCatalogError);
	}
	if (!authoring)
	{
		if (!mounted)
			throw std::runtime_error(
				"Packaged cooked catalog is missing: Assets/Derived/asset-manifest.cemf");
		const std::size_t sourceAssets = CookedCatalogSourceAssetCount();
		if (sourceAssets == 0u)
			throw std::runtime_error(
				"Packaged cooked catalog has no source identity table");

		const auto elapsed = std::chrono::steady_clock::now() - cookedCatalogStart;
		SerializationProfile::RecordBootStage(
			SerializationProfile::Stage::AssetCatalog,
			static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				elapsed).count()),
			static_cast<uint64_t>(sourceAssets));
		std::printf("[asset.catalog] source=cemf identities=%zu metaParsed=0\n",
			sourceAssets);
	}
}

void DataSystem::Finalize()
{
    DrainAssetPreparations();
	{
		std::lock_guard retainedGuard(m_retainedAssetsMutex);
		m_retainedAssets.clear();
	}
	{
		std::lock_guard lock(m_pendingAssetChangeMutex);
		m_pendingAssetChanges.clear();
	}
    own::shared_owner<const experiment::cooked::CookedAssetCatalog> retiredCatalog;
    own::shared_owner<AssetMetaRegistry> retiredRegistry;
    AssetDepot::TextureAssetRetiredEntries retiredTextures;
    AssetDepot::ModelAssetRetiredEntries retiredModels;
    LegacyCacheRetirement retiredLegacy;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        StageLegacyCacheRetirementLocked(retiredLegacy);
        StageTextureAssetRetirementLocked(retiredTextures);
        StageModelAssetRetirementLocked(retiredModels);
        retiredCatalog = std::move(m_cookedCatalog);
        retiredRegistry = std::move(m_assetMetaRegistry);
        InvalidateTextureAssetsLocked(retiredTextures);
        InvalidateModelAssetsLocked(retiredModels);
        DetachLegacyCachesLocked(retiredLegacy);
        m_cookedStaleAssets.clear();
        if (m_assetDepotRevision != (std::numeric_limits<std::uint64_t>::max)())
        {
            ++m_assetDepotRevision;
        }
    }
    {
        std::lock_guard lock(m_sceneMaterialMutex);
        m_recordingSceneMaterials = false;
        m_sceneMaterials.clear();
    }

}

void DataSystem::LoadAssetCatalog(const file::path& root)
{
    const auto registry = SnapshotAssetMetaRegistry();
    if (!registry || !file::exists(root))
    {
        return;
    }

	// D0(SerializationPlan §1.7 ②): 부팅 시 `.meta` 전수 파싱 비용. CLI가 프로파일러를
	// 켜기 전에 이미 끝나는 구간이라 Scope가 아니라 부팅 슬롯에 직접 적재한다.
	// D5-c가 이 함수를 cooked catalog로 대체할 때 대조할 기준선이다.
	const auto catalogStart = std::chrono::steady_clock::now();
	uint64_t parsedMetaCount = 0;

	std::error_code error;
	file::recursive_directory_iterator iterator(
		root, file::directory_options::skip_permission_denied, error);
	const file::recursive_directory_iterator end;
	while (iterator != end)
	{
		if (error)
		{
			error.clear();
			iterator.increment(error);
			continue;
		}

		const file::directory_entry& entry = *iterator;
		if (entry.is_regular_file(error) && !error &&
			entry.path().extension() == ".meta")
		{
			file::path targetPath = entry.path();
			targetPath.replace_extension();
			if (file::exists(targetPath))
			{
				std::string parseError;
				const Authoring::ParsedDocument document =
					Authoring::ParsedDocument::ParseFile(
						entry.path().string(), parseError);
				if (!document)
				{
					Debug::PrintLog(spdlog::level::warn, "Asset catalog ignored invalid meta: " +
						entry.path().string() + " (" + parseError + ")");
				}
				else
				{
					++parsedMetaCount;
					const Authoring::ReadNode node = document.Root();
					// MBC5 — schema-v2 model sidecar에는 legacy `guid`가 없다.
					// startup catalog가 `assetId`를 읽지 않으면 MBC4가 rewrite한 scene
					// ModelId를 경로로 풀 수 없어 새 generation loader에 도달하지 못한다.
					const Authoring::ReadNode identity =
						(node["schemaVersion"].As(0u) == assets::kModelSidecarSchemaVersion)
						? node["assetId"] : node["guid"];
					if (identity && identity.IsScalar())
					{
						const FileGuid guid(identity.AsString());
						if (guid != FileGuid{})
							RegisterAssetMeta(*registry, guid, targetPath);
					}
				}
			}
		}

		error.clear();
		iterator.increment(error);
	}

	// calls는 호출 횟수가 아니라 실제로 파싱한 `.meta` 개수다 — 이 단계는 부팅 1회이므로
	// 그 값을 세는 편이 판정에 쓸모 있다(회귀 게이트가 "0개를 성공으로 읽는" 것을 막는다).
	const auto catalogElapsed = std::chrono::steady_clock::now() - catalogStart;
    SerializationProfile::RecordBootStage(
        SerializationProfile::Stage::AssetCatalog,
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(catalogElapsed).count()),
        parsedMetaCount);
}

assets::ModelAssetGeneration::Shared DataSystem::LoadModelAssetGeneration(FileGuid guid)
{
    if (FileGuid{} == guid || !assets::IsUuidV8(guid.m_guid))
    {
        return {};
    }
    std::uint64_t epoch{}, resolverRevision{};
    {
        std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        epoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
        if (auto current = m_modelAssetGenerations.ResolveCurrent(guid.m_guid))
        {
            // Missing authoring graphs recover at their material owner boundary,
            // never by forcing a geometry reimport on an otherwise valid hit.
            return current;
        }
    }
    return LoadAndPublishModelAssetGeneration(guid, true, true, epoch, resolverRevision);
}

assets::ModelAssetGeneration::Shared DataSystem::LoadAndPublishModelAssetGeneration(
	FileGuid guid, bool allowEditorRecovery, bool publish, std::optional<std::uint64_t> expectedEpoch,
    std::optional<std::uint64_t> expectedResolverRevision)
{
    std::uint64_t loadEpoch{}, resolverRevision{};
    own::shared_owner<AssetMetaRegistry> registry;
    own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog;
    bool cookedStale{};
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || (expectedEpoch && *expectedEpoch != m_assetPreparationEpoch)
            || (expectedResolverRevision && *expectedResolverRevision != m_assetDepotRevision))
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        registry = m_assetMetaRegistry;
        catalog = m_cookedCatalog;
        cookedStale = m_cookedStaleAssets.contains(guid);
    }
	ce::profile_scope profile{ ce::marker<"Asset.ModelGeneration">() };
	// MBC11 — cooked catalog가 마운트돼 있고 이 모델의 generation 레코드가 신선하면
	// 그 레코드(Derived/Models/xx/<id>/<gen>/generation.asset)를 읽는다. Player는 이
	// 경로뿐이고 Editor는 마운트 없이 Library(저작 정본)를 읽는다. 어느 쪽도 실패하면
	// 다른 쪽으로 우회하지 않는다(§0.1-4 — silent fallback 0).
	assets::ModelAssetGenerationLoadRequest request;
	request.identityHeaderPath = PathFinder::ProjectSettingPath("AssetIdentity.asset");
	request.expectedModelId = guid.m_guid;
    if (PathFinder::IsAssetAuthoringEnabled())
        request.decodedTextureCacheRoot = PathFinder::CachePath("ModelTextures");
	const file::path cookedRecord = catalog && !cookedStale
        ? catalog->ResolveArtifactPath(experiment::AssetId{ guid.m_guid }) : file::path{};
	const bool fromCatalog = !cookedRecord.empty()
		&& cookedRecord.filename() == file::path("generation.asset");
	std::string sourceLabel;
    const file::path sourcePath = registry ? registry->GetPath(guid) : file::path{};
	if (fromCatalog)
	{
		request.generationPath = cookedRecord.parent_path();
		request.canonicalSidecarPath = request.generationPath / "sidecar.meta";
		sourceLabel = cookedRecord.string();
	}
	else
	{
		if (sourcePath.empty()) return {};
		file::path sidecarPath = sourcePath;
		sidecarPath += ".meta";
		request.generationRoot = PathFinder::DynamicSolutionPath(
			"Library/ModelAssetGenerations");
		request.canonicalSidecarPath = sidecarPath;
		sourceLabel = sourcePath.string();
	}
	assets::ModelAssetGenerationLoadResult loaded =
		assets::LoadModelAssetGeneration(request);
	if (!loaded.Succeeded())
	{
        {
            std::lock_guard admissionLock(m_assetPreparationMutex);
            if (m_assetPreparationStopping || loadEpoch != m_assetPreparationEpoch
                || resolverRevision != m_assetDepotRevision)
            {
                return {};
            }
        }
		m_generationLoadFailed.fetch_add(1, std::memory_order_relaxed);
		// Uncooked loads/reloads may ask the Editor to repair derived data, then
		// retry this strict reader exactly once. Callers release authoring locks
		// before applying a reload; the repair itself never calls this loader.
		if (allowEditorRecovery && !fromCatalog && PathFinder::IsAssetAuthoringEnabled()
			&& AssetAuthoringPort::RecoverModel(sourcePath, guid))
        {
            return LoadAndPublishModelAssetGeneration(guid, false, publish, loadEpoch, resolverRevision);
        }
		const std::string detail = loaded.issues.empty()
			? "알 수 없는 generation load 실패"
			: loaded.issues.front().context + ": "
				+ loaded.issues.front().message;
        Debug::PrintLog(spdlog::level::err, std::string("[model.generation] 게시 전 검증 실패(") +
                                                (fromCatalog ? "catalog" : "library") + "): " + sourceLabel + " (" +
                                                detail + ")");
        return {};
    }
    for (const auto& warning : loaded.warnings)
    {
        Debug::PrintLog(spdlog::level::warn, "[model.generation] optional geometry fallback: "
            + sourceLabel + " (" + warning.context + ": " + warning.message + ")");
    }
    // Authoring graphs are required even when an Editor project mounts a
    // previously cooked geometry generation. Player needs only the cooked closure.
    if (PathFinder::IsAssetAuthoringEnabled() && !sourcePath.empty() &&
        !assets::ModelMaterialGraphsPresent(PathFinder::Relative(""), *loaded.generation))
    {
        Debug::PrintLog(spdlog::level::err,
                        "Model material graphs are missing; retain geometry and create editable default graphs.");
    }
    (fromCatalog ? m_generationFromCatalog : m_generationFromLibrary).fetch_add(1, std::memory_order_relaxed);
    const std::string sourcePathString = sourceLabel;

    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || loadEpoch != m_assetPreparationEpoch
        || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
    if (!publish)
    {
        return std::move(loaded.generation);
    }
    assets::ModelAssetPublishResult published =
		m_modelAssetGenerations.Publish(std::move(loaded.generation));
	if (!published.Succeeded())
	{
		m_generationLoadFailed.fetch_add(1, std::memory_order_relaxed);
		Debug::PrintLog(spdlog::level::err, "[model.generation] cache publish 거부: "
			+ sourcePathString + " (outcome "
			+ std::to_string(static_cast<unsigned>(published.outcome)) + ")");
		return {};
	}
	// MBC7 — 교체된 generation의 embedded texture owner는 새 generation과
	// 섞이지 않는다(§6.2 "이전 texture generation 재사용 금지").
    if (published.retiredHandle.IsValid())
    {
        RetireModelGenerationTextures(published.retiredHandle);
    }
	return published.current;
}

namespace
{
	// generation이 검증·디코드해 둔 RGBA8 subresource를 텍스처 캐시가 읽는
	// 중립 CPU 이미지로 옮긴다. 두 번째 디코드는 없다 — 바이트를 옮길 뿐이다.
	//
	// ★ 예전에는 이 자리가 왕복이었다(축 A 전). generation 의 텍스처는 이미
	//   RHIFormat + raw 픽셀로 백엔드 중립인데, 그것을 DXGI_FORMAT 으로
	//   되돌려 DirectX::ScratchImage 를 세웠고, Vulkan 캐시가 그 DXGI_FORMAT
	//   을 다시 RHIFormat 으로 환원했다. 포맷 왕복 두 번이 순수한 어댑터
	//   비용이었다 — 지금은 양쪽 어휘가 처음부터 같아 옮기기만 한다.
	[[nodiscard]] TextureImage BuildGenerationCpuImage(
		const assets::ModelTextureAsset& texture, std::string& outError)
	{
		if (RHIFormat::RGBA8Unorm != texture.format
			&& RHIFormat::RGBA8UnormSrgb != texture.format)
		{
			outError = "generation texture 포맷이 RGBA8 계열이 아니다";
			return {};
		}
		if (texture.isCube || 0 == texture.width || 0 == texture.height
			|| 0 == texture.mipLevels || 0 == texture.arraySize
			|| texture.subresources.size()
				!= static_cast<std::size_t>(texture.mipLevels) * texture.arraySize)
		{
			outError = "generation texture descriptor와 subresource 수가 맞지 않는다";
			return {};
		}

		TextureImage image = TextureImage::Allocate(texture.format, texture.width,
			texture.height, texture.arraySize, texture.mipLevels);
		if (!image.IsValid())
		{
			outError = "중립 CPU 이미지 초기화 실패";
			return {};
		}
		for (std::uint32_t item = 0; item < texture.arraySize; ++item)
		{
			for (std::uint32_t mip = 0; mip < texture.mipLevels; ++mip)
			{
				// CopyTexturePixels(ModelAssetGeneration.cpp)의 적재 순서와 같다:
				// item 바깥, mip 안쪽. TextureImage 도 같은 규약이다.
				const assets::ModelTextureSubresource& source =
					texture.subresources[static_cast<std::size_t>(item) * texture.mipLevels + mip];
				const TextureSubimage* destination = image.Find(mip, item);
				std::byte* destinationPixels = (nullptr != destination)
					? image.MutablePixelsAt(*destination) : nullptr;
				if (nullptr == destination || nullptr == destinationPixels
					|| 0 == source.rowPitch
					|| source.offset + source.slicePitch > texture.pixels.size()
					|| destination->width != source.width
					|| destination->height != source.height)
				{
					outError = "generation texture subresource가 이미지 기술과 어긋난다";
					return {};
				}
				const std::uint32_t sourceRows = static_cast<std::uint32_t>(
					source.slicePitch / source.rowPitch);
				CopyImageRows(destinationPixels, destination->rowPitch,
					texture.pixels.data() + source.offset,
					static_cast<std::size_t>(source.rowPitch),
					(std::min)(sourceRows, destination->height),
					destination->rowPitch);
			}
		}
		return image;
	}
}

own::shared_owner<const Texture> DataSystem::ResolveModelGenerationTexture(
	const assets::ModelAssetGeneration& generation, const Uuid::Uuid16& textureId)
{
    std::uint64_t loadEpoch;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
    }

	const assets::ModelTextureAsset* texture = generation.FindTexture(textureId);
	if (nullptr == texture) return nullptr;
	const assets::ModelTextureHandle key{ textureId, generation.Identity().generation };

	{
		std::lock_guard lock(m_modelGenerationTextureMutex);
		if (const auto found = m_modelGenerationTextures.find(key);
			found != m_modelGenerationTextures.end())
		{
            if (auto current = asset_cache_detail::Acquire(found->second))
            {
                ++m_modelGenerationTextureStats.hits;
                return current;
            }
		}
	}

	ce::profile_scope profile{ ce::marker<"Asset.GenerationTextureImage">() };
	std::string error;
	TextureImage image = BuildGenerationCpuImage(*texture, error);
	own::shared_owner<const Texture> owner = image.IsValid()
		? Texture::CreateSharedFromImage(texture->name, std::move(image))
		: nullptr;
	if (!owner)
	{
		std::lock_guard lock(m_modelGenerationTextureMutex);
		++m_modelGenerationTextureStats.rejected;
		Debug::PrintLog(spdlog::level::err, "[model.generation] embedded texture owner 생성 실패: "
			+ texture->name + " (" + error + ")");
		return nullptr;
	}
    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || loadEpoch != m_assetPreparationEpoch)
    {
        return {};
    }
	// 큰 픽셀 복사는 잠금 밖에서 끝낸다. 동시에 준비된 경우 먼저 게시된 owner를 쓴다.
	std::lock_guard lock(m_modelGenerationTextureMutex);
    {
        const auto existing = m_modelGenerationTextures.find(key);
        if (existing != m_modelGenerationTextures.end())
        {
            if (auto current = asset_cache_detail::Acquire(existing->second))
            {
                ++m_modelGenerationTextureStats.hits;
                return current;
            }
        }
    }
    const auto charge = LegacyTextureRetainedBytes(*owner);
    auto published = asset_cache_detail::Publish(m_modelGenerationTextures, key, std::move(owner),
        charge, kLegacyTextureBudgetBytes, kLegacyCacheBudgetEntries);
    for (auto reverse = m_modelGenerationTextureOwners.begin(); reverse != m_modelGenerationTextureOwners.end();)
    {
        std::erase_if(reverse->second, [this](const auto& handle)
        {
            return !m_modelGenerationTextures.contains(handle);
        });
        if (reverse->second.empty())
        {
            reverse = m_modelGenerationTextureOwners.erase(reverse);
        }
        else
        {
            ++reverse;
        }
    }
    auto& handles = m_modelGenerationTextureOwners[generation.Handle()];
    if (std::find(handles.begin(), handles.end(), key) == handles.end())
    {
        handles.push_back(key);
    }
    ++m_modelGenerationTextureStats.created;
    m_modelGenerationTextureStats.live = m_modelGenerationTextures.size();
    return published;
}

DataSystem::ModelGenerationTextureCacheSnapshot
DataSystem::SnapshotModelGenerationTextures() const
{
	std::lock_guard lock(m_modelGenerationTextureMutex);
	ModelGenerationTextureCacheSnapshot snapshot = m_modelGenerationTextureStats;
	snapshot.live = static_cast<std::size_t>(std::count_if(m_modelGenerationTextures.begin(),
        m_modelGenerationTextures.end(), [](const auto& entry) { return !entry.second.current.expired(); }));
	return snapshot;
}

void DataSystem::StageLegacyCacheRetirementLocked(LegacyCacheRetirement& retired)
{
    assert(retired.preparationPins.empty() && retired.scenePins.empty() && retired.bundlePins.empty());
    // Reserve everything before taking cancellation pins. On failure the caller's
    // staged storage unwinds only after its outer guards, leaving live state intact.
    retired.preparationPins.reserve(m_assetPreparations.size());
    retired.scenePins.reserve(m_sceneAssetPreparations.size());
    retired.bundlePins.reserve(m_assetBundlePreparations.size());
    for (const auto& [id, weak] : m_assetPreparations)
    {
        if (auto request = weak.lock())
        {
            retired.preparationPins.push_back(std::move(request));
        }
    }
    for (const auto& weak : m_sceneAssetPreparations)
    {
        if (auto scene = weak.lock())
        {
            retired.scenePins.push_back(std::move(scene));
        }
    }
    for (const auto& weak : m_assetBundlePreparations)
    {
        if (auto bundle = weak.lock())
        {
            retired.bundlePins.push_back(std::move(bundle));
        }
    }
}

void DataSystem::DetachLegacyCachesLocked(LegacyCacheRetirement& retired) noexcept
{
    // Every map shell and strong cancellation pin already exists. This phase
    // swaps only into empty caller-owned storage and never destroys retired owners.
    assert(retired.materials.empty() && retired.authoredMaterials.empty()
        && retired.textures.empty() && retired.uiTextures.empty() && retired.spriteSheets.empty()
        && retired.modelTextures.empty() && retired.modelTextureOwners.empty()
        && retired.shaderSlots.empty() && retired.shaderSlotsByGuid.empty() && retired.shaderFreeSlots.empty()
        && retired.preparations.empty() && retired.scenes.empty() && retired.bundles.empty());
    m_modelAssetGenerations.DetachAll(retired.models);
    m_materialGraphGenerations.DetachAll(retired.graphs);
    {
        std::lock_guard lock(m_materialMutex);
        static_assert(noexcept(Materials.swap(retired.materials)));
        Materials.swap(retired.materials);
    }
    {
        std::lock_guard lock(m_authoredMaterialMutex);
        static_assert(noexcept(m_authoredMaterials.swap(retired.authoredMaterials)));
        m_authoredMaterials.swap(retired.authoredMaterials);
    }
    {
        std::lock_guard lock(m_textureMutex);
        Textures.swap(retired.textures);
        UITextures.swap(retired.uiTextures);
        SpriteSheets.swap(retired.spriteSheets);
    }
    {
        std::lock_guard lock(m_modelGenerationTextureMutex);
        m_modelGenerationTextures.swap(retired.modelTextures);
        m_modelGenerationTextureOwners.swap(retired.modelTextureOwners);
        m_modelGenerationTextureStats = {};
    }
    {
        std::lock_guard lock(m_shaderMetaMutex);
        m_shaderMetaSlots.swap(retired.shaderSlots);
        m_shaderMetaSlotByGuid.swap(retired.shaderSlotsByGuid);
        m_shaderMetaFreeSlots.swap(retired.shaderFreeSlots);
        m_shaderMetaRetainedBytes = 0u;
        m_shaderMetaUseSerial = 0u;
        // The generation serial is global identity and deliberately never reset.
    }
    for (const auto& request : retired.preparationPins)
    {
        request->cancelled.store(true, std::memory_order_release);
    }
    for (const auto& scene : retired.scenePins)
    {
        scene->cancelled.store(true, std::memory_order_release);
    }
    for (const auto& bundle : retired.bundlePins)
    {
        bundle->cancelled.store(true, std::memory_order_release);
    }
    m_assetPreparations.swap(retired.preparations);
    m_sceneAssetPreparations.swap(retired.scenes);
    m_assetBundlePreparations.swap(retired.bundles);
}

void DataSystem::RetireModelGenerationTextures(
	assets::ModelAssetGenerationHandle handle)
{
	std::lock_guard lock(m_modelGenerationTextureMutex);
	const auto owners = m_modelGenerationTextureOwners.find(handle);
	if (owners == m_modelGenerationTextureOwners.end()) return;
	for (const assets::ModelTextureHandle& key : owners->second)
	{
		if (0 != m_modelGenerationTextures.erase(key))
			++m_modelGenerationTextureStats.retired;
	}
	m_modelGenerationTextureOwners.erase(owners);
	m_modelGenerationTextureStats.live = m_modelGenerationTextures.size();
}

std::size_t DataSystem::BindModelGenerationTextures(Material& material,
	const assets::ModelAssetGeneration& generation)
{
	std::size_t bound = 0;
	for (const MaterialPropertyValue& value : material.m_propertyValues)
	{
		if (value.m_name.empty() || FileGuid{} == value.m_textureGuid) continue;
		if (nullptr == generation.FindTexture(value.m_textureGuid.m_guid)) continue;
		own::shared_owner<const Texture> owner =
			ResolveModelGenerationTexture(generation, value.m_textureGuid.m_guid);
		if (!owner) continue;
		material.UseTextureMap(value.m_name, std::move(owner));
		++bound;
	}
	return bound;
}

assets::ModelAssetGeneration::Shared DataSystem::ResolveModelAssetGeneration(
	assets::ModelAssetGenerationHandle handle) const
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
	return m_modelAssetGenerations.Resolve(handle);
}

assets::ModelAssetGenerationCacheSnapshot
DataSystem::SnapshotModelAssetGenerations() const
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
	return m_modelAssetGenerations.Snapshot();
}

std::vector<assets::ModelAssetGeneration::Shared>
DataSystem::SnapshotCurrentModelAssetGenerations() const
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
	return m_modelAssetGenerations.SnapshotCurrent();
}

DataSystem::ModelGenerationSourceSnapshot
DataSystem::SnapshotModelGenerationSources() const noexcept
{
	ModelGenerationSourceSnapshot snapshot;
	snapshot.fromCatalog = m_generationFromCatalog.load(std::memory_order_relaxed);
	snapshot.fromLibrary = m_generationFromLibrary.load(std::memory_order_relaxed);
	snapshot.failed = m_generationLoadFailed.load(std::memory_order_relaxed);
	return snapshot;
}

void DataSystem::InsertMaterial(own::shared_owner<const Material> material)
{
    if (material)
    {
        (void)RegisterImportedMaterial(material, material->m_name);
    }
}

std::vector<std::pair<std::string, own::shared_owner<const Texture>>>
DataSystem::SnapshotTextures(TextureFileType type)
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
    std::lock_guard guard(m_textureMutex);
    auto& cache = TextureCacheFor(type);
    std::erase_if(cache, [this](const auto& value)
    {
        return value.second.resolverRevision != m_assetDepotRevision;
    });
    asset_cache_detail::PruneExpired(cache);
    std::vector<std::pair<std::string, own::shared_owner<const Texture>>> result;
    for (auto& [key, entry] : cache)
    {
        if (auto owner = asset_cache_detail::Acquire(entry, m_assetDepotRevision))
        {
            result.emplace_back(key, std::move(owner));
        }
    }
    return result;
}

own::shared_owner<const Material> DataSystem::FindCachedMaterial(std::string_view name)
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
    std::lock_guard guard(m_materialMutex);
    const auto found = Materials.find(std::string(name));
    if (found == Materials.end())
    {
        return {};
    }
    auto owner = asset_cache_detail::Acquire(found->second, m_assetDepotRevision);
    if (!owner)
    {
        Materials.erase(found);
    }
    return owner;
}

std::vector<std::pair<std::string, own::shared_owner<const Material>>> DataSystem::SnapshotMaterials()
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
    std::lock_guard guard(m_materialMutex);
    std::erase_if(Materials, [this](const auto& value)
    {
        return value.second.resolverRevision != m_assetDepotRevision;
    });
    asset_cache_detail::PruneExpired(Materials);
    std::vector<std::pair<std::string, own::shared_owner<const Material>>> result;
    for (auto& [key, entry] : Materials)
    {
        if (auto owner = asset_cache_detail::Acquire(entry, m_assetDepotRevision))
        {
            result.emplace_back(key, std::move(owner));
        }
    }
    return result;
}

own::shared_owner<const Material> DataSystem::RegisterImportedMaterial(
    own::shared_owner<const Material> material, std::string_view baseName)
{
    if (!material)
    {
        return {};
    }
    // A const-qualified incoming handle can still share mutable aliases. Build
    // an independent completed value before publication, never cache that handle.
    auto candidate = own::make_shared<Material>(*material);
    const std::string base = baseName.empty() ? candidate->m_name : std::string(baseName);
    std::string name = candidate->m_name.empty() ? base : candidate->m_name;
    int suffix = 1;
    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
    std::lock_guard guard(m_materialMutex);
    while (true)
    {
        const auto found = Materials.find(name);
        auto current = found == Materials.end() ? own::shared_owner<const Material>{}
            : asset_cache_detail::Acquire(found->second, m_assetDepotRevision);
        if (!current)
        {
            candidate->m_name = name;
            const auto charge = LegacyMaterialRetainedBytes(*candidate);
            own::shared_owner<const Material> published = std::move(candidate);
            return asset_cache_detail::Publish(Materials, name, std::move(published),
                charge, kLegacyMaterialBudgetBytes, kLegacyCacheBudgetEntries, m_assetDepotRevision);
        }
        if (current->m_fileGuid == candidate->m_fileGuid)
        {
            return current;
        }
        name = base + "(" + std::to_string(suffix++) + ")";
    }
}

void DataSystem::SynchronizeLegacyMaterialProperties(Material& material) const
{
    const auto registry = SnapshotAssetMetaRegistry();
	auto resolveGuid = [&registry](std::string_view textureName)
	{
        if (textureName.empty() || !registry)
        {
            return FileGuid{};
        }

		const file::path filename = file::path(textureName).filename();
		const file::path materialPath = PathFinder::Relative("Materials\\") / filename;
		if (const FileGuid exact = registry->GetGuid(materialPath);
			exact != FileGuid{})
		{
			return exact;
		}
		if (const FileGuid byFilename =
			registry->GetFilenameToGuid(filename.string());
			byFilename != FileGuid{})
		{
			return byFilename;
		}
		return registry->GetStemToGuid(filename.stem().string());
	};

	auto synchronize = [&registry, &material, &resolveGuid](std::string_view property,
		std::string& legacyName, const Texture* runtimeTexture)
	{
		auto value = std::find_if(material.m_propertyValues.begin(),
			material.m_propertyValues.end(), [property](const MaterialPropertyValue& candidate)
			{
				return candidate.m_name == property;
			});

		FileGuid guid = value == material.m_propertyValues.end()
			? FileGuid{} : value->m_textureGuid;
		// PHASE 3.75 MBC7 — UUIDv8 texture GUID는 모델 generation closure의 subasset
		// 신원이다. 파일이 없으니 아래 이름 역해석(Materials\<이름>.png)은 legacy
		// Assimp 추출물의 **다른 자산** v4 GUID를 되살려 신원을 덮어쓰던 자리였다
		// (실측: Gunner 저장 씬의 texture GUID가 v8 → v4로 바뀌어 콜드 로드가 closure
		// 를 못 찾았다). closure 신원은 이름에 지지 않고, legacy 이름 필드도 채우지
		// 않는다 — 이름 폴백 자체가 §6.2가 없애는 축이다.
		if (assets::IsUuidV8(guid.m_guid)) return;
		bool runtimeTextureSelected = false;
		if (runtimeTexture && !runtimeTexture->m_name.empty())
		{
			runtimeTextureSelected = true;
			file::path runtimeName(runtimeTexture->m_name);
			if (!runtimeName.has_extension() && !runtimeTexture->m_extension.empty())
				runtimeName += runtimeTexture->m_extension;
			legacyName = runtimeName.filename().string();
			guid = resolveGuid(legacyName);
		}
		else if (guid != FileGuid{} && registry)
		{
			const file::path path = registry->GetPath(guid);
			if (!path.empty()) legacyName = path.filename().string();
		}
		else
		{
			guid = resolveGuid(legacyName);
		}

		if (guid == FileGuid{})
		{
			// legacy pointer API가 catalog 밖 texture로 바뀌었다면 예전 GUID를
			// 남겨 두지 않는다. 이름 fallback은 보존되어 다음 load가 같은 파일을 찾는다.
			if (runtimeTextureSelected && value != material.m_propertyValues.end())
				value->m_textureGuid = {};
			return;
		}
		if (value == material.m_propertyValues.end())
		{
			MaterialPropertyValue inserted;
			inserted.m_name = std::string(property);
			inserted.m_textureGuid = guid;
			material.m_propertyValues.push_back(std::move(inserted));
		}
		else
		{
			value->m_textureGuid = guid;
		}
	};

	synchronize(standard_material::property::BaseColorMap,
		material.m_baseColorTexName, material.GetBaseColorMapShared().borrow().unsafe_get());
	synchronize(standard_material::property::NormalMap,
		material.m_normalTexName, material.GetNormalMapShared().borrow().unsafe_get());
	synchronize(standard_material::property::OrmMap,
		material.m_ORM_TexName, material.GetOccRoughMetalMapShared().borrow().unsafe_get());
	synchronize(standard_material::property::AoMap,
		material.m_AO_TexName, material.GetAOMapShared().borrow().unsafe_get());
	synchronize(standard_material::property::EmissiveMap,
		material.m_EmissiveTexName, material.GetEmissiveMapShared().borrow().unsafe_get());
}

bool DataSystem::SerializeMaterialPayload(Material& material,
	Authoring::WriteNode outNode) const
{
    if (const auto& instance = material.GetMaterialGraphInstance())
    {
        material_graph::InstanceDocument document;
        document.name = material.m_name;
        document.materialId.value = material.m_fileGuid.m_guid;
        document.doubleSided = material.m_doubleSided;
        document.blendMode = material.m_renderingMode == MaterialRenderingMode::Transparent ? "transparent" :
            material.m_renderingMode == MaterialRenderingMode::Masked ? "masked" : "opaque";
        document.description = instance->description;
        std::string error;
        if (material_graph::WriteInstanceDocument(document, outNode, error))
            return true;
        Debug::PrintLog(spdlog::level::err, "LX material save failed: " + error);
        return false;
    }
    SynchronizeLegacyMaterialProperties(material);
	Authoring::WriteDocument staging;
	const Authoring::WriteNode node = staging.Root();

	// I5-M5 S2b — writer 전환. ShaderMeta를 아는 재질은 새 정본(schema+
	// shaderAssetId)으로 적는다. meta 부재 재질, legacy 전용 잔여
	// (m_cbufferValues — 코퍼스 실저작 0), 변환·인코딩 실패는 legacy 표기로
	// 폴백한다(조용한 소실 금지 — 폴백은 로그를 남긴다).
	if (FileGuid{} != material.m_shaderMetaGuid && material.m_cbufferValues.empty())
	{
		std::string error;
        // Logical-const cache load returns the pin with its numeric identity.
        ShaderMetaHandle handle;
        if (const auto meta = const_cast<DataSystem*>(this)->LoadShaderMetaOwner(
            material.m_shaderMetaGuid, handle, error))
		{
			experiment::Material authored;
			if (ExperimentMaterialMigration::ConvertLegacyMaterial(material,
					*meta, authored, error)
				&& experiment::SerializeMaterialAuthoring(authored, node,
					error))
			{
				outNode.Assign(node);
				return true;
			}
		}
		Debug::PrintLog(spdlog::level::warn, "Material 새 정본 writer 실패 — legacy 표기로 폴백"
			" (" + material.m_name + "): " + error);
	}

	if (!Meta::SerializeInto(&material, node)) return false;
	if (material.m_cbufferValues.empty())
	{
		outNode.Assign(node);
		return true;
	}

	// unordered_map 순회 순서를 디스크 형상으로 새지 않는다. legacy CB payload도
	// 이름순으로 고정해야 save-load-resave diff 0을 안정적으로 판정할 수 있다.
	std::vector<std::string_view> names;
	names.reserve(material.m_cbufferValues.size());
	for (const auto& [name, data] : material.m_cbufferValues)
	{
		(void)data;
		names.push_back(name);
	}
	std::ranges::sort(names);

	const Authoring::WriteNode buffers = node.Child("constant_buffers");
	buffers.SetSequence();
	for (const std::string_view name : names)
	{
		const auto& data = material.m_cbufferValues.at(std::string(name));
		const Authoring::WriteNode entry = buffers.Append();
		entry.SetMap();
		entry.Child("name").SetScalar(name);
		entry.Child("data").SetScalar(
			Authoring::Base64::Encode(data.data(), data.size()));
	}
	outNode.Assign(node);
	return true;
}

bool DataSystem::DeserializeMaterialPayload(Material& material,
	const Authoring::NodeView& view)
{
	ce::profile_scope profile{ ce::marker<"Asset.MaterialPayload">() };
	return DeserializeMaterialPayload(material, view, nullptr);
}

namespace
{
// 재질 캐시는 Slang 이 이 재질을 컴파일하며 **실제로 읽은 파일**만으로 판정한다.
// 예전 키는 DefaultPassShader 아래 .slang 전부였다. 그래서 그림자 셰이더 하나만 고쳐도
// 모든 재질이 다시 컴파일됐고(재질당 ~5.5초, 게임 스레드 직렬), 재질마다 셰이더 폴더
// 전체를 다시 읽었다(7개에 ~110 ms). 의존 목록은 컴파일러가 보고한 것을 그대로 쓴다 —
// #include 를 따로 따라가 짐작하지 않는다.
//
// 놓치는 경우 하나: 이미 해석된 포함 파일보다 검색 경로 앞쪽에 같은 이름의 새 파일이
// 생기면(가림) 기록된 파일은 그대로라 맞힌다. 생성 소스가 놓이는 캐시 폴더에는 생성
// 파일만 있어 이 일이 생기지 않는다.
constexpr std::array<char, 8> kMaterialGraphCacheMagic{'L','X','S','C','A','C','H','2'};
constexpr std::uint32_t kMaterialGraphCacheMaxDependencies = 4096;

// 생성 소스 자체를 정하는 것: 프로그램 본문·메타·호스트 셰이더. 호스트는 #include 가
// 아니라 생성 소스 뒤에 이어 붙으므로 Slang 의존 목록에 나오지 않는다 — 직접 넣는다.
std::string MaterialGraphCacheHeader(const LX::LXMaterialProgram& program,
                                     const file::path& shaderDirectory)
{
    std::ifstream host(shaderDirectory / "Includes/MaterialGraphSceneHost.slang", std::ios::binary);
    const std::string hostText{std::istreambuf_iterator<char>(host), {}};
    if (hostText.empty()) return {};
    std::string header = "lx-scene-authoring-cache-v3-graph-shadermeta-" +
        std::to_string(ShaderGeneratedMaterial::kAdapterVersion);
    const auto append = [&](std::string_view value) {
        const std::uint64_t size = value.size();
        header.append(reinterpret_cast<const char*>(&size), sizeof(size));
        header.append(value);
    };
    append(program.slang);
    append(LX::WriteMaterialProgramMetadata(program));
    append(hostText);
    return header;
}

bool HashMaterialGraphDependency(const file::path& path, Hash::Sha256Digest& result)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    Hash::Sha256 hash;
    std::array<char, 65536> chunk;
    while (stream)
    {
        stream.read(chunk.data(), chunk.size());
        hash.Update(chunk.data(), static_cast<std::size_t>(stream.gcount()));
    }
    if (!stream.eof()) return false;
    result = hash.Finish();
    return true;
}

// 셰이더 폴더 안의 파일은 상대 경로로 적어 프로젝트를 옮겨도 캐시가 산다.
// 폴더 밖이면 절대 경로로 남긴다(그 경우만 위치에 묶인다).
std::string MaterialGraphDependencyKey(const file::path& dependency, const file::path& shaderDirectory)
{
    std::error_code ignored;
    const file::path root = std::filesystem::weakly_canonical(shaderDirectory, ignored);
    const file::path relative = dependency.lexically_relative(root);
    if (!relative.empty() && *relative.begin() != "..") return relative.generic_string();
    return dependency.generic_string();
}

bool ReadMaterialGraphCache(const file::path& path, std::string_view header,
                            const file::path& shaderDirectory, material_graph::CookedProgram& result)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::array<char, 8> magic{};
    std::uint64_t headerSize{};
    file.read(magic.data(), magic.size());
    file.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize));
    if (!file || magic != kMaterialGraphCacheMagic || headerSize != header.size()) return false;
    std::string recorded(header.size(), '\0');
    file.read(recorded.data(), static_cast<std::streamsize>(recorded.size()));
    if (!file || recorded != header) return false;

    std::uint32_t dependencyCount{};
    file.read(reinterpret_cast<char*>(&dependencyCount), sizeof(dependencyCount));
    if (!file || dependencyCount == 0 || dependencyCount > kMaterialGraphCacheMaxDependencies) return false;
    for (std::uint32_t index = 0; index < dependencyCount; ++index)
    {
        std::uint32_t keySize{};
        file.read(reinterpret_cast<char*>(&keySize), sizeof(keySize));
        if (!file || keySize == 0 || keySize > 4096) return false;
        std::string key(keySize, '\0');
        Hash::Sha256Digest expected{};
        file.read(key.data(), static_cast<std::streamsize>(key.size()));
        file.read(reinterpret_cast<char*>(expected.data()), static_cast<std::streamsize>(expected.size()));
        if (!file) return false;
        const file::path stored(key);
        Hash::Sha256Digest actual{};
        if (!HashMaterialGraphDependency(stored.is_absolute() ? stored : shaderDirectory / stored, actual) ||
            actual != expected)
            return false;
    }

    std::uint64_t productSize{};
    file.read(reinterpret_cast<char*>(&productSize), sizeof(productSize));
    if (!file || productSize == 0 || productSize > (128ull << 20)) return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(productSize));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof()) return false;
    std::string validation;
    return material_graph::ReadCookedProgram(bytes, {}, result, validation);
}

void WriteMaterialGraphCache(const file::path& path, std::string_view header,
                             const file::path& shaderDirectory, const material_graph::VerifiedProduct& product)
{
    // 의존 목록이 비면 무엇이 바뀌어도 맞히는 캐시가 된다. 그런 캐시는 쓰지 않는다.
    if (product.dependencies.empty() || product.dependencies.size() > kMaterialGraphCacheMaxDependencies) return;
    std::vector<std::pair<std::string, Hash::Sha256Digest>> dependencies;
    dependencies.reserve(product.dependencies.size());
    for (const auto& dependency : product.dependencies)
    {
        Hash::Sha256Digest digest{};
        if (!HashMaterialGraphDependency(dependency, digest)) return;
        dependencies.emplace_back(MaterialGraphDependencyKey(dependency, shaderDirectory), digest);
    }
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!material_graph::WriteCookedProgram(product, {}, bytes, error)) return;
    static std::atomic<std::uint64_t> serial{};
    const file::path staging = path.string() + ".stage-" + std::to_string(GetCurrentProcessId()) +
                               "-" + std::to_string(++serial);
    {
        std::ofstream file(staging, std::ios::binary | std::ios::trunc);
        const std::uint64_t headerSize = header.size(), productSize = bytes.size();
        const auto dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        file.write(kMaterialGraphCacheMagic.data(), kMaterialGraphCacheMagic.size());
        file.write(reinterpret_cast<const char*>(&headerSize), sizeof(headerSize));
        file.write(header.data(), static_cast<std::streamsize>(header.size()));
        file.write(reinterpret_cast<const char*>(&dependencyCount), sizeof(dependencyCount));
        for (const auto& [key, digest] : dependencies)
        {
            const auto keySize = static_cast<std::uint32_t>(key.size());
            file.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
            file.write(key.data(), static_cast<std::streamsize>(key.size()));
            file.write(reinterpret_cast<const char*>(digest.data()), static_cast<std::streamsize>(digest.size()));
        }
        file.write(reinterpret_cast<const char*>(&productSize), sizeof(productSize));
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file)
        {
            std::error_code ignored;
            std::filesystem::remove(staging, ignored);
            return;
        }
    }
    if (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
    }
}

bool CompileAuthoringMaterial(const LX::LXMaterialAsset& asset, FileGuid guid, material_graph::CookedProgram& result,
                              std::string& error)
{
    ce::profile_scope profile{ ce::marker<"Material.CompileAuthoring">() };
    error.clear();
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    std::optional<ce::profile_scope> step{ std::in_place, ce::marker<"Material.GenerateSlang">() };
    const auto program = LX::GenerateMaterialSlang(asset, &diagnostics);
    step.reset();
    if (!program)
    {
        for (const auto& diagnostic : diagnostics)
        {
            error += diagnostic.code + ": " + diagnostic.message + "\n";
        }
        return false;
    }
    const auto shaderDirectory = PathFinder::RelativeToShader("DefaultPassShader");
    const std::string cacheHeader = MaterialGraphCacheHeader(*program, shaderDirectory);
    const std::string sourceIdentity = cacheHeader.empty()
        ? material_graph::BuildBoundSource(*program) + LX::WriteMaterialProgramMetadata(*program) : cacheHeader;
    const auto sourceRevision = Hash::ToHex(Hash::Sha256::Compute(sourceIdentity.data(), sourceIdentity.size()));
    const auto source = PathFinder::CachePath("Lattice") / (guid.ToString() + "-" + sourceRevision + ".slang");
    std::error_code filesystemError;
    std::filesystem::create_directories(source.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = filesystemError.message();
        return false;
    }
    // 렌더러가 시작됐으면 그 백엔드만 컴파일한다. 다른 쪽은 이 프로세스에서 쓰이지 않는다.
    const auto backend = material_graph::AuthoringSceneBackend();
    const auto carriesWanted = [&](const material_graph::VerifiedProduct& product) {
        return backend ? material_graph::HasSceneBackend(product, *backend)
                       : material_graph::HasSceneBackend(product, RHIShaderBinary::Dxil) &&
                             material_graph::HasSceneBackend(product, RHIShaderBinary::SpirV);
    };
    const auto cachePath = PathFinder::CachePath("Lattice") / (guid.ToString() + ".slang.scene-cache");
    step.emplace(ce::marker<"Material.CacheRead">());
    if (!cacheHeader.empty() && ReadMaterialGraphCache(cachePath, cacheHeader, shaderDirectory, result) &&
        result.product.program.slang == program->slang &&
        result.metadata == LX::WriteMaterialProgramMetadata(*program) &&
        result.product.materialShader &&
        result.product.program.semanticKey.ends_with(material_graph::SceneHostIdentity) &&
        carriesWanted(result.product))
    {
        return true;
    }
    step.reset();
    step.emplace(ce::marker<"Material.CompileSceneProduct">());
    if (!material_graph::CompileSceneProduct(*program, shaderDirectory, source, {},
                                             result.product, error, guid, backend))
    {
        return false;
    }
    result.metadata = LX::WriteMaterialProgramMetadata(result.product.program);
    result.boundSource = material_graph::BuildBoundSource(result.product.program);
    step.reset();
    step.emplace(ce::marker<"Material.CacheWrite">());
    if (!cacheHeader.empty()) WriteMaterialGraphCache(cachePath, cacheHeader, shaderDirectory, result.product);
    return true;
}
}

own::shared_owner<const material_graph::Generation> DataSystem::LoadMaterialGraphGeneration(FileGuid guid,
                                                                                          std::string& error,
                                                                                          bool reload)
{
    ce::profile_scope profile{ ce::marker<"Material.GraphGeneration">() };
    const experiment::AssetId id{guid.m_guid};
    std::uint64_t loadEpoch;
    material_graph::GenerationPreparationRequest request;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping)
        {
            error = "Material graph loading is shutting down.";
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        request = m_materialGraphGenerations.BeginPreparation(id, reload, error);
    }
    if (!request)
    {
        return {};
    }
    // 이미 세대가 있는 그래프도 남긴다. 그래야 다음 열기에 세대가 비어 있을 때 선적재된다.
    // 경로 해석은 다른 잠금을 잡으므로 기록 잠금 밖에서 한다.
    bool recording = false;
    {
        std::lock_guard lock(m_sceneMaterialMutex);
        recording = m_recordingSceneMaterials;
    }
    if (recording)
    {
        if (file::path sourcePath = GetMaterialGraphSourcePath(guid); !sourcePath.empty())
        {
            std::lock_guard lock(m_sceneMaterialMutex);
            if (m_recordingSceneMaterials) m_sceneMaterials.emplace_back(guid, std::move(sourcePath));
        }
    }
    const auto prepared = material_graph::GenerationStore::Prepare(
        request,
        [this, guid](material_graph::CookedProgram& result, std::string& failure) {
            const file::path sourcePath =
                PathFinder::IsAssetAuthoringEnabled() ? GetMaterialGraphSourcePath(guid) : file::path{};
            return LoadMaterialGraphProgram(guid, sourcePath, result, failure);
        },
        error);
    if (!prepared)
    {
        return {};
    }
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || loadEpoch != m_assetPreparationEpoch)
    {
        error = "Material graph loading was cancelled or invalidated.";
        return {};
    }
    return m_materialGraphGenerations.Publish(*prepared, error);
}

bool DataSystem::LoadMaterialGraphProgram(FileGuid guid, const file::path& sourcePath,
                                          material_graph::CookedProgram& result, std::string& failure) const
{
    const experiment::AssetId id{guid.m_guid};
    const auto catalog = GetCookedCatalog();
    if (!catalog && !PathFinder::IsAssetAuthoringEnabled())
    {
        failure = "LX graph requires a mounted, verified cooked material catalog.";
        return false;
    }
    std::optional<LX::LXMaterialAsset> source;
    if (PathFinder::IsAssetAuthoringEnabled())
    {
        if (Lowercase(sourcePath.extension().string()) != ".shadergraph")
        {
            failure = "LX graph GUID does not resolve to its authoring source.";
            return false;
        }
        ce::profile_scope sourceProfile{ ce::marker<"Material.GraphSourceLoad">() };
        source = LX::LXMaterialAsset::Load(sourcePath, LX::CreateMaterialDefinitions(), &failure);
        if (!source)
            return false;
    }
    if (catalog)
    {
        const experiment::cooked::LooseArtifactByteSource bytes(catalog->DerivedRoot());
        if (material_graph::LoadCookedGeneration(*catalog, bytes, id, source ? &*source : nullptr, result, failure) &&
            result.product.materialShader)
        {
            return true;
        }
        if (failure.empty()) failure = "Cooked graph has no generated ShaderMeta contract; regenerate the cook.";
    }
    // An Editor may reopen an edited or newly authored graph before the
    // next package cook. Player keeps the strict bytecode-only boundary.
    return source && CompileAuthoringMaterial(*source, guid, result, failure);
}

namespace
{
// 프로젝트 안의 경로는 프로젝트 기준 상대 경로로 바꾼다. 프로젝트 폴더를 옮기거나 복사해도
// 목록 이름과 목록 안의 경로가 그대로 맞는다. 프로젝트 밖이면 절대 경로를 그대로 둔다.
file::path ProjectRelative(const file::path& path)
{
    std::error_code ignored;
    const auto root = std::filesystem::weakly_canonical(PathFinder::Relative(), ignored);
    const auto canonical = std::filesystem::weakly_canonical(
        path.is_absolute() ? path : PathFinder::Relative() / path, ignored);
    auto relative = canonical.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") return canonical;
    return relative;
}

// 장면 경로마다 목록 하나. 이름이 같은 장면이 다른 폴더에 있어도 섞이지 않게 경로 해시로 이름 짓는다.
file::path SceneMaterialListPath(const file::path& scene)
{
    // 한글 경로도 코드 페이지 변환 없이 다루도록 UTF-8 로 키를 만든다.
    const auto utf8 = ProjectRelative(scene).generic_u8string();
    const auto key = Lowercase(std::string(utf8.begin(), utf8.end()));
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : key) hash = (hash ^ c) * 1099511628211ull;
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << hash << ".txt";
    return PathFinder::CachePath("Lattice") / "ScenePreload" / name.str();
}

constexpr std::size_t kMaxSceneMaterials = 4096;
} // namespace

void DataSystem::PrewarmSceneMaterials(const file::path& scene)
{
    if (thread_pool::is_worker_thread())
    {
        throw std::logic_error("Scene material prewarm requires the scene owner thread.");
    }
    if (!PathFinder::IsAssetAuthoringEnabled())
    {
        return;
    }
    std::uint64_t loadEpoch;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return;
        }
        loadEpoch = m_assetPreparationEpoch;
        std::lock_guard lock(m_sceneMaterialMutex);
        m_recordingSceneMaterials = true;
        m_sceneMaterials.clear();
    }
    ce::profile_scope profile{ ce::marker<"Material.Prewarm">() };
    std::vector<std::pair<FileGuid, file::path>> graphs;
    {
        std::ifstream list(SceneMaterialListPath(scene), std::ios::binary);
        std::string line;
        while (graphs.size() < kMaxSceneMaterials && std::getline(list, line))
        {
            const auto tab = line.find('\t');
            FileGuid guid;
            if (tab == std::string::npos || !Uuid::TryParse(line.substr(0, tab), guid.m_guid)) continue;
            // 이미 세대가 있으면 엔티티 적재가 컴파일하지 않는다.
            if (m_materialGraphGenerations.Current(experiment::AssetId{guid.m_guid})) continue;
            file::path source(std::u8string(line.begin() + tab + 1, line.end()));
            if (source.is_relative()) source = PathFinder::Relative() / source;
            graphs.emplace_back(guid, std::move(source));
        }
    }
    if (graphs.empty()) return;

    // Explicit synchronous callers retain verified CPU generations as well as the disk cache.
    std::atomic<std::size_t> next{};
    std::atomic<std::size_t> failures{};
    const auto work = [&]() {
        for (std::size_t index = next++; index < graphs.size(); index = next++)
        {
            std::string failure;
            try
            {
                material_graph::GenerationPreparationRequest request;
                {
                    std::lock_guard lock(m_assetPreparationMutex);
                    if (m_assetPreparationStopping || loadEpoch != m_assetPreparationEpoch)
                    {
                        return;
                    }
                    request = m_materialGraphGenerations.BeginPreparation(
                        experiment::AssetId{graphs[index].first.m_guid}, false, failure);
                }
                const auto prepared = material_graph::GenerationStore::Prepare(request,
                    [&](material_graph::CookedProgram& result, std::string& error)
                    {
                        return LoadMaterialGraphProgram(graphs[index].first, graphs[index].second, result, error);
                    }, failure);
                std::lock_guard lock(m_assetPreparationMutex);
                if (m_assetPreparationStopping || loadEpoch != m_assetPreparationEpoch)
                {
                    return;
                }
                if (!prepared || !m_materialGraphGenerations.Publish(*prepared, failure))
                {
                    ++failures;
                }
            }
            catch (const std::exception&)
            {
                ++failures;
            }
        }
    };
    // 컴파일 칸 수만큼만 작업 스레드를 쓴다. 더 띄우면 칸을 기다리며 작업 스레드를 붙든다.
    const std::size_t workers = std::min(graphs.size(), RHIShaderCompiler::MaxParallelCompiles());
    job_group jobs;
    for (std::size_t index = 0; index < workers; ++index)
    {
        jobs.add([&work] { work(); });
    }
    job_handle completion;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        completion = SubmitAssetWorkLocked(std::move(jobs));
    }
    completion.wait();
    if (failures)
    {
        // 실패한 그래프는 엔티티 적재가 다시 시도하고 그 오류를 보고한다.
        Debug::PrintLog(spdlog::level::warn, "Scene material prewarm skipped " + std::to_string(failures.load()) +
                                                 " of " + std::to_string(graphs.size()) + " graphs.");
    }
}

void DataSystem::CommitSceneMaterials(const file::path& scene)
{
    std::vector<std::pair<FileGuid, file::path>> graphs;
    {
        std::lock_guard lock(m_sceneMaterialMutex);
        if (!m_recordingSceneMaterials) return;
        m_recordingSceneMaterials = false;
        graphs.swap(m_sceneMaterials);
    }
    std::ranges::sort(graphs);
    graphs.erase(std::unique(graphs.begin(), graphs.end()), graphs.end());
    if (graphs.size() > kMaxSceneMaterials) graphs.resize(kMaxSceneMaterials);
    const auto path = SceneMaterialListPath(scene);
    std::error_code ioError;
    std::filesystem::create_directories(path.parent_path(), ioError);
    std::ofstream list(path, std::ios::binary | std::ios::trunc);
    for (const auto& [guid, source] : graphs)
    {
        const auto text = ProjectRelative(source).generic_u8string();
        list << guid.ToString() << '\t' << std::string(text.begin(), text.end()) << '\n';
    }
}

own::shared_owner<const material_graph::Generation> DataSystem::ResolveMaterialGraphGeneration(FileGuid guid) const
{
    return m_materialGraphGenerations.Current(experiment::AssetId{guid.m_guid});
}

std::vector<assets::ModelAssetGeneration::Shared> DataSystem::SnapshotPreparedModelAssets() const
{
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
    auto current = m_modelAssetGenerations.SnapshotCurrent();
    std::map<Uuid::Uuid16, assets::ModelAssetGeneration::Shared> byModel;
    for (auto& model : current)
    {
        byModel.emplace(model->Identity().modelId, std::move(model));
    }
    {
        for (const auto& [id, weak] : m_assetPreparations)
        {
            const auto preparation = weak.lock();
            if (preparation && preparation->type == RuntimeAssetType::Model
                && preparation->epoch == m_assetPreparationEpoch && preparation->resolverRevision == m_assetDepotRevision
                && !preparation->cancelled.load(std::memory_order_acquire)
                && preparation->work.is_complete() && preparation->error.empty() && preparation->model)
            {
                auto& candidate = byModel[id.m_guid];
                if (!candidate || candidate->Identity().generation <= preparation->model->Identity().generation)
                {
                    candidate = preparation->model;
                }
            }
        }
    }
    std::vector<assets::ModelAssetGeneration::Shared> result;
    result.reserve(byModel.size());
    for (auto& [id, model] : byModel)
    {
        result.push_back(std::move(model));
    }
    return result;
}

file::path DataSystem::GetMaterialGraphSourcePath(FileGuid guid) const
{
    const auto registered = GetFilePath(guid);
    if (!registered.empty())
        return registered;
    for (const auto& model : SnapshotPreparedModelAssets())
    {
        for (const auto& material : model->Materials())
        {
            if (assets::ModelMaterialGraphId(material.materialId) == guid.m_guid)
            {
                const auto path = assets::ModelMaterialGraphPath(PathFinder::Relative(""), model->Identity().modelId,
                                                                 material.materialId);
                return assets::ModelMaterialGraphIdentityMatches(path, guid.m_guid) ? path : file::path{};
            }
        }
    }
    return {};
}

bool DataSystem::ConfigureModelMaterialGraph(Material& material, const assets::ModelAssetGeneration& model,
                                             const assets::ModelMaterialAsset& source, std::string& error)
{
    if (material.HasMaterialGraph())
        return true;
    material_graph::InstanceDescription description;
    description.graphId.value = assets::ModelMaterialGraphId(source.materialId);
    const auto generation = LoadMaterialGraphGeneration(FileGuid(description.graphId.value), error);
    if (!generation)
    {
        const std::string cause = error;
        return ConfigureEditableDefaultMaterialGraph(material, cause, error);
    }
    experiment::Material imported;
    ExperimentMaterialMigration::ConvertModelMaterialAsset(source, model, imported);
    SynchronizeLegacyMaterialProperties(material);
    // Existing Scene edits become instance overrides only when they differ
    // from the imported PBR seed. An edited source graph remains authoritative.
    for (const auto& parameter : generation->cooked.product.program.parameters)
    {
        const std::string property = parameter.identifier == "alpha" ? "baseColor" : parameter.identifier;
        const auto original = std::ranges::find(imported.properties, property, &experiment::MaterialProperty::name);
        const auto current = std::ranges::find(material.m_propertyValues, property, &MaterialPropertyValue::m_name);
        if (original == imported.properties.end() || current == material.m_propertyValues.end())
            continue;
        if (parameter.type == LX::PinType::Texture)
        {
            const auto* texture = std::get_if<experiment::TextureReference>(&original->value);
            if (texture && current->m_textureGuid != FileGuid{} &&
                current->m_textureGuid.m_guid != texture->assetId.value)
                description.textures.push_back({parameter.id, experiment::AssetId{current->m_textureGuid.m_guid}});
            continue;
        }
        std::vector<float> before;
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, float>)
                    before = {value};
                else if constexpr (std::is_same_v<T, math::vector3>)
                    before = {value.x, value.y, value.z};
                else if constexpr (std::is_same_v<T, math::vector4>)
                    before = {value.x, value.y, value.z, value.w};
            },
            original->value);
        const auto& after = current->m_numericValue;
        if (parameter.identifier == "alpha")
        {
            if (before.size() == 4 && after.size() == 4 && before[3] != after[3])
                description.parameters.push_back({parameter.id, double(after[3])});
        }
        else if (parameter.type == LX::PinType::Float && before.size() == 1 && after.size() == 1 && before != after)
            description.parameters.push_back({parameter.id, double(after.front())});
        else if (parameter.type == LX::PinType::Color && before.size() >= 3 && after.size() >= 3 &&
                 !std::equal(before.begin(), before.begin() + 3, after.begin()))
            description.parameters.push_back({parameter.id, std::array<double, 4>{after[0], after[1], after[2], 1}});
    }
    if (!ConfigureMaterialGraph(material, description, error, false, &model))
    {
        const std::string cause = error;
        return ConfigureEditableDefaultMaterialGraph(material, cause, error);
    }
    material.m_fileGuid = FileGuid(source.materialId);
    return true;
}

bool DataSystem::ConfigureEditableDefaultMaterialGraph(Material& material, std::string_view cause, std::string& error)
{
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping)
        {
            error = "Material graph recovery is shutting down.";
            return false;
        }
    }
    if (!PathFinder::IsAssetAuthoringEnabled())
    {
        error = std::string(cause);
        return false;
    }
    const auto graph = assets::BuildDefaultMaterialGraph(error);
    if (!graph)
    {
        return false;
    }
    const FileGuid guid = FileGuid::CreateRandomV4();
    const auto path = PathFinder::RelativeToMaterial("") / ("Recovered_" + guid.ToString() + ".shadergraph");
    if (AssetAuthoringPort::WriteTextAssetWithMeta(path, LX::LXMaterialArchive::Write(*graph), guid) != guid)
    {
        error = "Cannot publish editable default graph: " + path.string();
        return false;
    }
    if (!ApplyAssetChange({RuntimeAssetChangeKind::CatalogUpsert, RuntimeAssetType::MaterialGraph, guid, path}))
    {
        error = "Cannot register editable default graph: " + path.string();
        return false;
    }
    material_graph::InstanceDescription description;
    description.graphId.value = guid.m_guid;
    if (!ConfigureMaterialGraphAuthoring(material, *graph, description, error))
    {
        return false;
    }
    material.m_name = "Recovered Material";
    if (material.m_fileGuid == FileGuid{})
    {
        material.m_fileGuid = guid;
    }
    material.m_renderingMode = MaterialRenderingMode::Opaque;
    material.m_doubleSided = false;
    Debug::PrintLog(spdlog::level::warn, "Material graph failed: " + std::string(cause) +
        ". Mesh retained with editable graph: " + path.string());
    error.clear();
    return true;
}

bool DataSystem::ConfigureMaterialGraphAuthoring(Material& material, const LX::LXMaterialAsset& asset,
                                                 const material_graph::InstanceDescription& description,
                                                 std::string& error)
{
    if (!PathFinder::IsAssetAuthoringEnabled())
    {
        error = "Material graph authoring requires an Editor project.";
        return false;
    }
    std::uint64_t loadEpoch;
    material_graph::GenerationPreparationRequest request;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping)
        {
            error = "Material graph authoring is shutting down.";
            return false;
        }
        loadEpoch = m_assetPreparationEpoch;
        request = m_materialGraphGenerations.BeginPreparation(description.graphId, true, error);
    }
    const auto prepared = material_graph::GenerationStore::Prepare(
        request,
        [&](material_graph::CookedProgram& result, std::string& failure) {
            return CompileAuthoringMaterial(asset, FileGuid(description.graphId.value), result, failure);
        },
        error);
    if (!prepared)
    {
        return false;
    }
    own::shared_owner<const material_graph::Generation> generation;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || loadEpoch != m_assetPreparationEpoch)
        {
            error = "Material graph authoring was cancelled or invalidated.";
            return false;
        }
        generation = m_materialGraphGenerations.Publish(*prepared, error);
    }
    return generation && ConfigureMaterialGraph(material, description, error);
}

bool DataSystem::ConfigureMaterialGraph(Material& material, const material_graph::InstanceDescription& description,
                                        std::string& error, bool reload, const assets::ModelAssetGeneration* model)
{
    FileGuid guid;
    guid.m_guid = description.graphId.value;
    auto generation = LoadMaterialGraphGeneration(guid, error, reload);
    if (!generation)
        return false;
    if (!generation->cooked.product.materialShader)
    {
        error = "Graph material requires a cooked/generated ShaderMeta contract; regenerate the material cook.";
        return false;
    }
    const auto textureLoader = [this, model](const experiment::AssetId& id, LX::LXColorSpace colorSpace,
                                      std::string& failure) -> own::shared_owner<const Texture> {
        if (model && model->FindTexture(id.value))
        {
            return ResolveModelGenerationTexture(*model, id.value);
        }
        for (const auto& model : SnapshotPreparedModelAssets())
        {
            if (model->FindTexture(id.value))
                return ResolveModelGenerationTexture(*model, id.value);
        }
        FileGuid textureGuid;
        textureGuid.m_guid = id.value;
        const auto catalog = GetCookedCatalog();
        const auto* entry = catalog ? catalog->Find(id) : nullptr;
        if ((entry && entry->kind != experiment::cooked::CookedAssetKind::Texture) ||
            (!entry && !PathFinder::IsAssetAuthoringEnabled()))
        {
            failure = "LX texture override is not a cooked Texture asset: " + textureGuid.ToString();
            return {};
        }
        const file::path path = ResolveCatalogAssetPath(textureGuid);
        if (path.empty())
        {
            failure = "LX texture GUID has no registered asset path: " + textureGuid.ToString();
            return {};
        }
        auto owner = LoadSharedMaterialTexture(path.string(), false, colorSpace == LX::LXColorSpace::SRGB);
        if (!owner)
            failure = "LX texture load failed: " + textureGuid.ToString();
        return owner;
    };
    own::shared_owner<const material_graph::Instance> candidate;
    if (!material_graph::BuildInstance(std::move(generation), description, textureLoader, candidate, error))
        return false;

    // Publish the generated common schema, packed values and texture owners as
    // one immutable instance. Authored ShaderMeta cache slots remain separate.
    material.ResetShaderRuntime();
    material.ResetTextureRuntime();
    material.m_shaderMetaGuid = {};
    material.m_propertyValues.clear();
    material.m_keywordSelections.clear();
    material.m_cbufferValues.clear();
    material.m_baseColorTexName.clear();
    material.m_normalTexName.clear();
    material.m_ORM_TexName.clear();
    material.m_AO_TexName.clear();
    material.m_EmissiveTexName.clear();
    material.m_materialGraphInstance = std::move(candidate);
    return true;
}

bool DataSystem::DeserializeMaterialPayload(Material& material,
	const Authoring::NodeView& view, experiment::Material* outAuthored, bool persistRecovery)
{
	const Authoring::ReadNode readNode = Authoring::NodeViewAccess::Node(view);
	if (!readNode || !readNode.IsMap()) return false;

    if (readNode["lattice_material"])
    {
        material_graph::InstanceDocument document;
        std::string error;
        if (!material_graph::ReadInstanceDocument(readNode, document, error))
        {
            Debug::PrintLog(spdlog::level::err, "LX material load failed: " + error);
            return false;
        }
        if (!ConfigureMaterialGraph(material, document.description, error))
        {
            if (!persistRecovery && PathFinder::IsAssetAuthoringEnabled())
            {
                Debug::PrintLog(spdlog::level::warn, "Inline material prewarm deferred recovery to its owner: " + error);
                return true;
            }
            const std::string cause = error;
            if (!ConfigureEditableDefaultMaterialGraph(material, cause, error))
            {
                Debug::PrintLog(spdlog::level::err, "LX default graph recovery failed: " + error);
                return false;
            }
            if (outAuthored)
            {
                *outAuthored = {};
            }
            return true;
        }
        material.m_name = std::move(document.name);
        material.m_fileGuid.m_guid = document.materialId.value;
        material.m_doubleSided = document.doubleSided;
        material.m_renderingMode = document.blendMode == "transparent" ? MaterialRenderingMode::Transparent :
            document.blendMode == "masked" ? MaterialRenderingMode::Masked : MaterialRenderingMode::Opaque;
        if (outAuthored)
            *outAuthored = {};
        return true;
    }

    // I5-M5 S1 — 읽기 이중화. 새 정본(schema + shaderAssetId)을 만나면
	// experiment 코덱으로 읽고 legacy 런타임 재질로 변환한다. 런타임 소유가
	// 아직 legacy인 동안(S2 이전)의 전환기 경로이며, 이름 기반 keywords는
	// 실제 ShaderMeta를 로드해 인덱스로 정규화한다 — 짐작하지 않는다.
	if (readNode["schema"] && readNode["shaderAssetId"])
	{
		experiment::Material authored;
		std::string error;
		if (!experiment::DeserializeMaterialAuthoring(readNode, authored, error))
		{
			Debug::PrintLog(spdlog::level::err, "Material 새 정본 decode 실패: " + error);
			return false;
		}
		const ShaderMeta* metaForKeywords = nullptr;
		own::shared_owner<const ShaderMeta> metaOwner;
		if (!authored.keywords.empty())
		{
			FileGuid shaderGuid{};
			shaderGuid.m_guid = authored.shaderAssetId.value;
            ShaderMetaHandle handle;
            metaOwner = LoadShaderMetaOwner(shaderGuid, handle, error);
			if (!metaOwner)
			{
				Debug::PrintLog(spdlog::level::err, "Material 새 정본 keywords 정규화용 ShaderMeta"
					" 로드 실패: " + error);
				return false;
			}
			metaForKeywords = metaOwner ? &*metaOwner.borrow() : nullptr;
		}
		if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(authored,
			metaForKeywords, material, error))
		{
			Debug::PrintLog(spdlog::level::err, "Material 새 정본 변환 실패: " + error);
			return false;
		}
        material.m_materialGraphInstance.reset();
        FinalizeMaterialRuntime(material);
		// I5-D5c1 — 저작 원본을 버리지 않는다. 여기서 놓치면 소비자는 legacy를
		// 다시 experiment로 되돌리는 수밖에 없고, 그 왕복이 colorSpace·string
		// property를 깎는다(변환기 헤더가 명시한 손실).
		if (nullptr != outAuthored) *outAuthored = std::move(authored);
		return true;
	}

	try
	{
		Meta::Deserialize(&material, readNode);
		material.m_cbufferValues.clear();
		if (const Authoring::ReadNode buffers = readNode["constant_buffers"])
		{
			if (!buffers.IsSequence()) return false;
			for (const Authoring::ReadNode entry : buffers)
			{
				if (!entry.IsMap() || !entry["name"] || !entry["data"])
					return false;
				std::string name = entry["name"].AsString();
				if (name.empty() || material.m_cbufferValues.contains(name))
					return false;
				const std::string encoded = entry["data"].AsString();
				std::vector<std::uint8_t> binary;
				if (!Authoring::Base64::Decode(encoded, binary)) return false;
				material.m_cbufferValues.emplace(std::move(name), std::move(binary));
			}
		}
	}
	catch (const std::exception& exception)
	{
		Debug::PrintLog(spdlog::level::err, "Material payload deserialize failed: "
			+ std::string(exception.what()));
		return false;
	}

    material.m_materialGraphInstance.reset();
    FinalizeMaterialRuntime(material);
	return true;
}

bool DataSystem::HasVersionedMaterialBinaryPayload(std::istream& input) const
{
	const std::istream::pos_type position = input.tellg();
	if (position == std::istream::pos_type(-1)) return false;

	std::array<char, kMaterialPayloadMagic.size()> magic{};
	input.read(magic.data(), magic.size());
	const bool matches = input.gcount() == static_cast<std::streamsize>(magic.size())
		&& magic == kMaterialPayloadMagic;
	input.clear();
	input.seekg(position);
	return matches && static_cast<bool>(input);
}

bool DataSystem::SerializeMaterialBinaryPayload(Material& material,
	std::ostream& output) const
{
	Authoring::WriteDocument document;
	if (!SerializeMaterialPayload(material, document.Root())) return false;
	std::vector<std::byte> payload;
	std::string encodeError;
	if (!Authoring::EncodeCookedDocument(document.Root().Read(), payload, encodeError))
	{
		Debug::PrintLog(spdlog::level::err, "Material binary payload encode failed: " + encodeError);
		return false;
	}
	if (payload.size() > kMaxMaterialPayloadBytes
		|| payload.size() > std::numeric_limits<std::uint32_t>::max())
	{
		return false;
	}

	output.write(kMaterialPayloadMagic.data(), kMaterialPayloadMagic.size());
	WriteU16(output, kMaterialPayloadVersion);
	WriteU16(output, kMaterialPayloadCookedDocumentEncoding);
	WriteU32(output, static_cast<std::uint32_t>(payload.size()));
	output.write(reinterpret_cast<const char*>(payload.data()),
		static_cast<std::streamsize>(payload.size()));
	return output.good();
}

bool DataSystem::DeserializeMaterialBinaryPayload(Material& material,
	std::istream& input)
{
	std::array<char, kMaterialPayloadMagic.size()> magic{};
	input.read(magic.data(), magic.size());
	std::uint16_t version{};
	std::uint16_t encoding{};
	std::uint32_t payloadSize{};
	if (!input || magic != kMaterialPayloadMagic
		|| !ReadU16(input, version) || !ReadU16(input, encoding)
		|| !ReadU32(input, payloadSize))
	{
		return false;
	}
	if (version != kMaterialPayloadVersion
		|| encoding != kMaterialPayloadCookedDocumentEncoding
		|| payloadSize > kMaxMaterialPayloadBytes)
	{
		return false;
	}

	std::vector<std::byte> payload(payloadSize);
	if (payloadSize != 0)
		input.read(reinterpret_cast<char*>(payload.data()),
			static_cast<std::streamsize>(payload.size()));
	if (!input) return false;

	std::string parseError;
	const Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseCooked(payload, parseError);
	if (!document)
	{
		Debug::PrintLog(spdlog::level::err, "Material binary payload decode failed: " + parseError);
		return false;
	}
	const Authoring::ReadNode payloadNode = document.Root();
	return DeserializeMaterialPayload(material,
		Authoring::NodeViewAccess::Make(payloadNode));
}

void DataSystem::FinalizeMaterialRuntime(Material& material)
{
	ce::profile_scope profile{ ce::marker<"Asset.MaterialFinalize">() };
    // Scene binding finalizes cloned materials too. An LX snapshot is already
    // complete and must not be erased by the legacy ShaderMeta finalizer.
    if (material.HasMaterialGraph())
        return;
    // 디스크/scene 논리 값이 바뀌면 기존 schema가 가리키는 applied generation도
	// 더는 유효한 runtime 상태가 아니다. legacy CB bytes는 Configure에서 새 layout에
	// repack할 입력이므로 ResetShaderRuntime은 그것을 보존한다.
	material.ResetShaderRuntime();
	if (0.04f > material.m_materialInfo.m_IOR || 4.f < material.m_materialInfo.m_IOR)
		material.m_materialInfo.m_IOR = 1.5f;

	// property GUID가 저장 정본이다. decode 대상에 남아 있을 수 있는 generic/
	// Standard runtime owner를 함께 버려 낡은 generation과 이름이 새 GUID를
	// 역으로 덮지 못하게 한다.
	material.ResetTextureRuntime();
	SynchronizeLegacyMaterialProperties(material);
	auto loadTexture = [this, &material](std::string_view property,
		const std::string& name, bool compress)
	{
		const bool srgb = property == standard_material::property::BaseColorMap
            || property == standard_material::property::EmissiveMap;
		own::shared_owner<const Texture> texture;
		const auto value = std::find_if(material.m_propertyValues.begin(),
			material.m_propertyValues.end(), [property](const MaterialPropertyValue& candidate)
			{
				return candidate.m_name == property;
			});
		if (value != material.m_propertyValues.end()
			&& value->m_textureGuid != FileGuid{})
		{
			const file::path path = GetFilePath(value->m_textureGuid);
			if (!path.empty()) texture = LoadSharedMaterialTexture(path.string(), compress, srgb);
		}
		if (!texture && !name.empty())
			texture = LoadSharedMaterialTexture(name, compress, srgb);
		return texture;
	};

	material.UseBaseColorMap(loadTexture(standard_material::property::BaseColorMap,
		material.m_baseColorTexName, true));
	material.UseNormalMap(loadTexture(standard_material::property::NormalMap,
		material.m_normalTexName, false));
	material.UseOccRoughMetalMap(loadTexture(standard_material::property::OrmMap,
		material.m_ORM_TexName, false));
	material.UseAOMap(loadTexture(standard_material::property::AoMap,
		material.m_AO_TexName, false));
	material.UseEmissiveMap(loadTexture(standard_material::property::EmissiveMap,
		material.m_EmissiveTexName, false));

	// P2d-c: Standard 다섯 이름 밖의 texture property도 같은 GUID 경로로 owner를
	// 복원한다. MaterialPropertyValue는 type tag를 중복 저장하지 않으므로 nil이
	// 아닌 texture GUID만 후보로 삼고, 실제 ShaderMeta type/register 대조는 frame
	// sealing과 reflection gate가 담당한다.
	const auto isLegacyTextureProperty = [](std::string_view property)
	{
		return property == standard_material::property::BaseColorMap
			|| property == standard_material::property::NormalMap
			|| property == standard_material::property::OrmMap
			|| property == standard_material::property::AoMap
			|| property == standard_material::property::EmissiveMap;
	};
	for (const MaterialPropertyValue& value : material.m_propertyValues)
	{
		if (value.m_name.empty() || value.m_textureGuid == FileGuid{}
			|| isLegacyTextureProperty(value.m_name))
		{
			continue;
		}

		// MBC9 — 임베디드(subasset) GUID는 registry에 경로가 없다. 그 owner는
		// MeshRenderer::BindModelGeneration이 generation closure에서 묶는다.
		const file::path path = GetFilePath(value.m_textureGuid);
		if (path.empty()) continue;
		own::shared_owner<const Texture> texture = LoadSharedMaterialTexture(path.string(), false);
		if (!texture) continue;
		material.UseTextureMap(value.m_name, std::move(texture));
	}
}

own::shared_owner<const Material> DataSystem::LoadMaterial(std::string_view name)
{
    std::uint64_t loadEpoch;
    std::uint64_t resolverRevision;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
    }

    std::string materialName(name);

    // 조회와 삽입만 락으로 감싼다. 중간의 파일 로딩은 LoadMaterialTexture를 호출하는데
    // 그쪽이 m_textureMutex를 잡으므로, 여기서 락을 유지하면 material→texture 순서의
    // 락 중첩이 생긴다. 락을 겹치지 않게 두어 데드락 여지를 없앤다.
    {
        std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
            || loadEpoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
        {
            return {};
        }
        std::lock_guard<std::mutex> guard(m_materialMutex);
        const auto found = Materials.find(materialName);
        if (found != Materials.end())
        {
            if (auto current = asset_cache_detail::Acquire(found->second, resolverRevision))
            {
                return current;
            }
        }
    }
    const file::path sourcePath =
		PathFinder::Relative("Materials\\") / (materialName + ".asset");
    if (!file::exists(sourcePath))
    {
		return nullptr;
    }
	file::path loadPath = sourcePath;
	FileGuid assetGuid = GetFileGuid(sourcePath);
	if (assetGuid != FileGuid{})
	{
		loadPath = ResolveCatalogAssetPath(assetGuid);
		if (loadPath.empty()) return nullptr;
	}

	std::string parseError;
	const Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseFile(loadPath.string(), parseError);
	if (!document)
		throw std::runtime_error("Material parse failed: " + parseError);
	const Authoring::ReadNode node = document.Root();
    auto material = own::make_shared<Material>();
    if (!DeserializeMaterialPayload(*material, Authoring::NodeViewAccess::Make(node))) return nullptr;
    // 파일 stem이 cache key의 정본이다. 내부 m_name이 낡았거나 비어 있어도
    // LoadMaterialShared(name)가 같은 세대를 찾도록 게시 직전에 맞춘다.
    material->m_name = materialName;
	if (assetGuid != FileGuid{})
	{
		std::printf("[material.document] source=%s guid=%s\n",
			loadPath.lexically_normal() != sourcePath.lexically_normal()
				? "cooked" : "authoring",
			assetGuid.ToString().c_str());
	}

    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || loadEpoch != m_assetPreparationEpoch
        || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
    {
        std::lock_guard<std::mutex> guard(m_materialMutex);
        // 로딩 중 다른 스레드가 같은 머티리얼을 먼저 넣었을 수 있다.
        // 그 경우 맵에 있는 쪽을 반환해 인스턴스가 갈라지지 않게 한다.
        const auto charge = LegacyMaterialRetainedBytes(*material);
        own::shared_owner<const Material> published = std::move(material);
        return asset_cache_detail::Publish(Materials, materialName, std::move(published),
            charge, kLegacyMaterialBudgetBytes, kLegacyCacheBudgetEntries, resolverRevision);
    }
}

// I5-D5c1 — base 재질 자산의 저작 원본. legacy 캐시(Materials)와 별개로
// 자산 GUID로 캐시한다 — 씬 로드가 renderer마다 같은 base를 다시 파싱하지
// 않게. 캐시 키는 GUID다(파일 stem이 아니라): ref 표기의 정본 주소가 GUID다.
own::shared_owner<const experiment::Material> DataSystem::LoadAuthoredMaterialShared(
	FileGuid assetGuid)
{
    std::uint64_t loadEpoch;
    std::uint64_t resolverRevision;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
    }

	ce::profile_scope profile{ ce::marker<"Asset.AuthoredMaterial">() };
	if (FileGuid{} == assetGuid) return nullptr;
	{
		std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
            || loadEpoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
        {
            return {};
        }
        std::lock_guard<std::mutex> guard(m_authoredMaterialMutex);
		const auto it = m_authoredMaterials.find(assetGuid);
		if (it != m_authoredMaterials.end())
        {
            if (auto current = asset_cache_detail::Acquire(it->second, resolverRevision))
            {
                return current;
            }
        }
	}

	const file::path sourcePath = GetFilePath(assetGuid);
	const file::path path = ResolveCatalogAssetPath(assetGuid);
	if (path.empty()) return nullptr;

	// I5-D5c1 — 문서 파싱은 **기존 payload 디코더 하나**만 쓴다. 여기서 YAML
	// backend 노드를 직접 열면 D3-b 래칫의 탈출구가 하나 더 생긴다(실제로
	// 그렇게 짰다가 게이트가 잡았다). legacy 산물은 버린다 — 이 창구가 원하는
	// 것은 authored 쪽이고, 캐시라 자산당 1회다.
	std::string parseError;
	const Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseFile(path.string(), parseError);
	if (!document)
		throw std::runtime_error("Authored material parse failed: " + parseError);
	const Authoring::ReadNode node = document.Root();
	Material discardedLegacy;
	auto authored = own::make_shared<experiment::Material>();
	if (!DeserializeMaterialPayload(discardedLegacy,
		Authoring::NodeViewAccess::Make(node), authored.borrow().unsafe_get()))
	{
		return nullptr;
	}
	// legacy 표기 자산에는 저작 원본이 없다 — 지어내지 않는다(디코더가
	// outAuthored를 건드리지 않으므로 shaderAssetId가 nil로 남는다).
	if (experiment::AssetId{} == authored->shaderAssetId) return nullptr;
	std::printf("[material.document] source=%s guid=%s\n",
		path.lexically_normal() != sourcePath.lexically_normal()
			? "cooked" : "authoring",
		assetGuid.ToString().c_str());

    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || loadEpoch != m_assetPreparationEpoch
        || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
	std::lock_guard<std::mutex> guard(m_authoredMaterialMutex);
    const auto charge = LegacyAuthoredMaterialRetainedBytes(*authored);
    own::shared_owner<const experiment::Material> published = std::move(authored);
    return asset_cache_detail::Publish(m_authoredMaterials, assetGuid, std::move(published),
        charge, kLegacyMaterialBudgetBytes, kLegacyCacheBudgetEntries, resolverRevision);
}

own::shared_owner<const Material> DataSystem::LoadMaterialShared(std::string_view name)
{
    // Transitional synchronous loader. New runtime loading uses AssetDepot requests.
    return LoadMaterial(name);
}

own::shared_owner<const Texture> DataSystem::LoadTextureGUID(FileGuid guid)
{
    std::uint64_t loadEpoch{};
    std::uint64_t resolverRevision{};
    {
        std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || !SnapshotAssetMetaRegistry())
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
    }
    // GUID resolution and the path-loader handoff are one current lookup. Do
    // not accept an old resolved path merely because the inner loader started
    // after a remount and therefore observed its newer resolver revision.
    const file::path texturePath = ResolveCatalogAssetPath(guid);
    if (texturePath.empty())
    {
        return {};
    }
    auto texture = LoadTexture(texturePath.string());
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
        || loadEpoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
    return texture;
}

own::shared_owner<const Texture> DataSystem::LoadTexture(std::string_view filePath, TextureFileType type)
{
	return LoadSharedTexture(filePath, type);
}

DataContainer<Texture>& DataSystem::TextureCacheFor(TextureFileType type)
{
	// 조회와 넣기가 **같은** 맵을 본다. 예전에는 조회는 늘 Textures, 넣기는 용도별이라
	// UI·SpriteSheet 는 한 번도 맞지 않았고 같은 stem 의 일반 텍스처가 대신 나왔다.
	// 재질·지형·HDR 용도는 같은 적재 정책이라 Textures 를 나눠 쓴다 — 키가 경로라
	// 다른 파일이 섞이지 않는다.
	switch (type)
	{
	case TextureFileType::UITexture:   return UITextures;
	case TextureFileType::SpriteSheet: return SpriteSheets;
	default:                           return Textures;
	}
}

own::shared_owner<const Texture> DataSystem::LoadSharedTexture(std::string_view filePath, TextureFileType type)
{
    std::uint64_t loadEpoch;
    std::uint64_t resolverRevision;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
    }

	const file::path assetPath = ResolveRuntimeAssetPath(filePath, TextureFallbackDirectory(type));
    const std::string sourceKey = TextureCacheKey(assetPath);
    const std::string key = TextureRoleCacheKey(sourceKey, type);
	DataContainer<Texture>& cache = TextureCacheFor(type);

	// 캐시 조회와 삽입만 락으로 감싼다.
	// 이 함수는 LoadAssetBundle이 스레드풀로 병렬 호출하는데 예전에는 무잠금이라
	// 동시 삽입 시 unordered_map 리해시와 겹쳐 힙이 손상될 수 있었다.
	// 디스크 로딩은 오래 걸리므로 락 밖에서 수행한다(같은 파일을 두 번 읽는
	// 낭비는 있을 수 있으나 삽입 시 먼저 들어온 것을 쓰므로 정확성에는 문제가 없다).
	{
		std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
            || loadEpoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
        {
            return {};
        }
        std::lock_guard<std::mutex> guard(m_textureMutex);
        if (const auto found = cache.find(key); found != cache.end())
        {
            if (auto current = asset_cache_detail::Acquire(found->second, resolverRevision))
            {
                return current;
            }
        }
	}

	own::shared_owner<const Texture> texture;
	{
		ce::profile_scope profile{ ce::marker<"Asset.Texture">() };
		texture = Texture::LoadSharedFromPath(assetPath, false, sourceKey);
	}
	if (!texture)
	{
		Debug::PrintLog(spdlog::level::err, "DataSystem::LoadSharedTexture : texture file not found: " + assetPath.string());
		return nullptr;
	}

    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || loadEpoch != m_assetPreparationEpoch
        || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
	std::lock_guard<std::mutex> guard(m_textureMutex);
	// 다른 스레드가 먼저 넣었으면 그것을 돌려준다 — 같은 키에 인스턴스가 둘이면
	// 먼저 받은 쪽과 나중에 받은 쪽이 서로 다른 GPU 이미지를 붙든다.
	const auto charge = LegacyTextureRetainedBytes(*texture);
    return asset_cache_detail::Publish(cache, key, std::move(texture),
        charge, kLegacyTextureBudgetBytes, kLegacyCacheBudgetEntries, resolverRevision);
}

own::shared_owner<const Texture> DataSystem::LoadSharedMaterialTexture(std::string_view filePath, bool isCompress,
    std::optional<bool> srgb)
{
    std::uint64_t loadEpoch;
    std::uint64_t resolverRevision;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
    }

	// I7-C1 — 호출자가 준 경로가 **실재하면 그대로 쓴다**. 예전에는 파일명만
	// 떼어 `Assets/Materials/` 아래로 다시 뿌리내렸는데, 그 규약이 cooked
	// artifact를 원리적으로 못 읽게 만들고 있었다: resolver가 catalog에서
	// `Derived/Textures/<ab>/<guid>.png`를 골라 줘도 여기서 이름만 남아
	// `Assets/Materials/<guid>.png`를 찾다 실패했다(실측 — cooked 소비의
	// 실장애물이 resolver가 아니라 이 한 줄이었다). 이름만 오는 legacy
	// 호출(재질의 텍스처 이름 필드)은 예전 규약 그대로 간다.
	file::path destination = file::path(filePath);
	std::error_code destinationError;
	if (!destination.is_absolute()
		|| !file::is_regular_file(destination, destinationError))
	{
		destination = PathFinder::Relative("Materials\\")
			/ destination.filename();
	}
	// G2 — 경로·압축·색공간이 모두 결과를 바꾼다. 색공간을 안 준 호출은 원본 그대로를
	// 받으므로 "source" 로 가른다. 예전 이 자리의 stem 키는 일반 텍스처 캐시와 같은
	// 맵에서 같은 stem 을 나눠 다른 파일·다른 압축 결과를 돌려줄 수 있었다.
	std::string key = MaterialTextureKeyPrefix(destination)
		+ (isCompress ? "bc:" : "raw:")
		+ (srgb.has_value() ? (*srgb ? "srgb" : "linear") : "source");

	// 1차 조회 (락 짧게)
	{
		std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
            || loadEpoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
        {
            return {};
        }
        std::unique_lock lock(m_textureMutex);
        if (auto it = Textures.find(key); it != Textures.end())
        {
            if (auto current = asset_cache_detail::Acquire(it->second, resolverRevision))
            {
                return current;
            }
        }
	}

	// 로드 (락 없이 I/O)
	// ★ 디코더는 HRESULT 실패를 예외로 던진다(Win32::ThrowIfFailed). 이 함수의 계약은
	//   "못 읽으면 nullptr + 로그" 인데, 파일이 **있지만 그림이 아닐** 때만 예외가
	//   새어 FinalizeMaterialRuntime → DeserializeMaterialPayload 를 뚫었다 — 텍스처
	//   GUID 하나가 엉뚱한 자산(.shadermeta)을 가리키면 재질 복원 전체가 예외로
	//   끊겼다. 없는 파일과 같은 자리에서 거절한다.
	ce::profile_scope profile{ ce::marker<"Asset.MaterialTexture">() };
	own::shared_owner<const Texture> loaded;
	try
	{
		loaded = Texture::LoadSharedFromPath(destination, isCompress, TextureCacheKey(destination));
	}
	catch (const std::exception& exception)
	{
		Debug::PrintLog(spdlog::level::err, "Material texture decode failed: "
			+ destination.string() + ": " + exception.what());
		return nullptr;
	}
    if (loaded && srgb.has_value())
    {
        loaded = Texture::WithColorSpace(loaded, *srgb);
        std::string mipFailure;
        loaded = Texture::WithMipChain(loaded, mipFailure);
        if (!loaded)
        {
            Debug::PrintLog(spdlog::level::err, "Material texture mip preparation failed: " + destination.string() + ": " + mipFailure);
            return nullptr;
        }
    }
	if (!loaded)
	{
		Debug::PrintLog(spdlog::level::err, "TextureLoader::LoadTexture : file not found");
		return nullptr;
	}

    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || loadEpoch != m_assetPreparationEpoch
        || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
	// 삽입 단계: 이미 다른 스레드가 넣었을 수 있으니 '덮어쓰지 말고' 기존 걸 사용
	{
		std::unique_lock lock(m_textureMutex);
        const auto charge = LegacyTextureRetainedBytes(*loaded);
        return asset_cache_detail::Publish(Textures, key, std::move(loaded),
            charge, kLegacyTextureBudgetBytes, kLegacyCacheBudgetEntries, resolverRevision);
	}

}

own::shared_owner<Material> DataSystem::CreateMaterial()
{
    // Creation is caller-owned editing state. Publication is explicit through
    // RegisterImportedMaterial, which copies to a distinct immutable generation.
    auto material = own::make_shared<Material>();
    std::lock_guard guard(m_materialMutex);
    std::string name = "NewMaterial";
    int index = 1;
    while (Materials.find(name) != Materials.end())
    {
        name = "NewMaterial" + std::to_string(index++);
    }
    material->m_name = std::move(name);
    material->m_fileGuid = FileGuid::CreateRandomV4();
    return material;
}

// ★ LoadSFont(DirectXTK SpriteFont)를 걷었다 (D4, 2026-08-09).
//   ID3D11Device로 폰트를 만들던 유일한 자리였고, 그 결과를 그리는 쪽은
//   T6에서 사라졌다. 게다가 Assets/Font/ 가 비어 있어 읽을 자산도 없었다.
//   폰트는 SDF 계통으로 새로 세운다.

// ★ 콘텐츠 브라우저 UI 전체가 여기 있었다 (PHASE 4-3 슬라이스 2).
//   창 등록과 그리기 함수 일곱, 그리고 그 상태(현재 폴더·검색 필터·
//   선택 메타)가 EngineGUIWindow/ContentsBrowserWindow로 옮겨 갔다.
//   자산 시스템은 캐시와 아이콘·폰트를 가질 뿐, 이제 그리지 않는다.

own::shared_owner<AssetMetaRegistry> DataSystem::SnapshotAssetMetaRegistry() const
{
    std::lock_guard catalogLock(m_cookedCatalogMutex);
    return m_assetMetaRegistry;
}

FileGuid DataSystem::GetFileGuid(const file::path& filepath) const
{
    const auto registry = SnapshotAssetMetaRegistry();
    return registry ? registry->GetGuid(filepath) : FileGuid{};
}

void DataSystem::QueueAssetChange(RuntimeAssetChange change)
{
	if (change.path.empty()) return;
	change.path = change.path.lexically_normal();

	std::lock_guard lock(m_pendingAssetChangeMutex);
	// Windows는 한 번의 저장에도 Modified를 여러 번 보낼 수 있다. 같은 종류와
	// 경로의 미처리 이벤트는 마지막 게시값 하나로 합쳐 프레임 경계 generation이
	// watcher 알림 조각 수만큼 뛰지 않게 한다.
	const auto duplicate = std::find_if(m_pendingAssetChanges.rbegin(),
		m_pendingAssetChanges.rend(), [&](const RuntimeAssetChange& queued)
		{
			return queued.kind == change.kind && queued.path == change.path;
		});
	if (duplicate != m_pendingAssetChanges.rend())
	{
		*duplicate = std::move(change);
		return;
	}

	m_pendingAssetChanges.emplace_back(std::move(change));
}

std::size_t DataSystem::DrainQueuedAssetChanges()
{
	std::vector<RuntimeAssetChange> pending;
	{
		std::lock_guard lock(m_pendingAssetChangeMutex);
		pending.swap(m_pendingAssetChanges);
	}

	for (const RuntimeAssetChange& change : pending) ApplyAssetChange(change);
	return pending.size();
}

bool DataSystem::ApplyAssetChange(const RuntimeAssetChange& change)
{
    if (change.path.empty())
    {
        return false;
    }

    struct InvalidationScope
    {
        std::mutex* mutex{};
        std::size_t* depth{};
        ~InvalidationScope()
        {
            if (mutex)
            {
                std::lock_guard lock(*mutex);
                --*depth;
            }
        }
    } invalidation;
    AssetDepot::TextureAssetRetiredEntries retiredTextures;
    AssetDepot::ModelAssetRetiredEntries retiredModels;
    own::shared_owner<AssetMetaRegistry> registry;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || !m_assetMetaRegistry)
        {
            return false;
        }
        registry = m_assetMetaRegistry;
        ++m_assetInvalidationDepth;
        invalidation.mutex = &m_assetPreparationMutex;
        invalidation.depth = &m_assetInvalidationDepth;
    }
    if (change.kind != RuntimeAssetChangeKind::CatalogUpsert)
    {
        // The current document closure may reference a changed model's generated
        // graphs or texture overrides. Invalidate the batch, keeping old accepted
        // owners alive; the next scene request rediscovers the revised closure.
        std::lock_guard lock(m_assetPreparationMutex);
        StageTextureAssetRetirementLocked(retiredTextures);
        StageModelAssetRetirementLocked(retiredModels);
        ++m_assetPreparationEpoch;
        InvalidateTextureAssetsLocked(retiredTextures);
        InvalidateModelAssetsLocked(retiredModels);
        std::erase_if(m_retiredAssetPreparations, [](const auto& asset)
        {
            return asset->work.is_complete();
        });
        for (const auto& [guid, weak] : m_assetPreparations)
        {
            if (const auto asset = weak.lock())
            {
                asset->cancelled.store(true, std::memory_order_release);
                if (asset->type == RuntimeAssetType::MaterialGraph)
                {
                    m_materialGraphGenerations.InvalidatePreparation(experiment::AssetId{guid.m_guid});
                }
                if (!asset->work.is_complete())
                {
                    m_retiredAssetPreparations.push_back(asset);
                }
            }
        }
        m_assetPreparations.clear();
        for (const auto& weak : m_assetBundlePreparations)
        {
            if (const auto bundle = weak.lock())
            {
                bundle->cancelled.store(true, std::memory_order_release);
            }
        }
        for (const auto& weak : m_sceneAssetPreparations)
        {
            if (const auto preparation = weak.lock())
            {
                preparation->cancelled.store(true, std::memory_order_release);
            }
        }
    }

	RuntimeAssetType assetType = change.assetType;
	if (RuntimeAssetType::Auto == assetType)
		assetType = ResolveRuntimeAssetType(change.path);

    if (change.kind != RuntimeAssetChangeKind::CatalogUpsert)
    {
        const auto guid = change.guid != FileGuid{} ? change.guid : GetFileGuid(change.path);
        if (assetType == RuntimeAssetType::MaterialGraph)
        {
            m_materialGraphGenerations.InvalidatePreparation(experiment::AssetId{guid.m_guid});
        }
        else if (assetType == RuntimeAssetType::Model)
        {
            assets::ModelAssetGeneration::Shared model;
            {
                std::lock_guard admissionLock(m_assetPreparationMutex);
                model = m_modelAssetGenerations.ResolveCurrent(guid.m_guid);
            }
            if (model)
            {
                for (const auto& material : model->Materials())
                {
                    m_materialGraphGenerations.InvalidatePreparation(
                        experiment::AssetId{assets::ModelMaterialGraphId(material.materialId)});
                }
            }
        }
    }

	switch (change.kind)
	{
	case RuntimeAssetChangeKind::CatalogUpsert:
		return RegisterAssetMeta(*registry, change.guid, change.path);
	case RuntimeAssetChangeKind::ContentReload:

        if (assetType == RuntimeAssetType::MaterialGraph)
        {
            const FileGuid guid = change.guid != FileGuid{} ? change.guid : registry->GetGuid(change.path);
            if (guid == FileGuid{} || !RegisterAssetMeta(*registry, guid, change.path))
                return false;
            if (!ResolveMaterialGraphGeneration(guid))
                return true;
            std::string error;
            if (LoadMaterialGraphGeneration(guid, error, true))
                return true;
            Debug::PrintLog(spdlog::level::err, "LX graph reload retained the accepted generation: " + error);
            return false;
        }
        if (assetType == RuntimeAssetType::Model)
		{
			const FileGuid guid = change.guid != FileGuid{} ? change.guid
				: registry->GetGuid(change.path);
			if (!assets::IsUuidV8(guid.m_guid)
				|| !RegisterAssetMeta(*registry, guid, change.path)) return false;
			// Keep unused assets lazy. For a resident model, validate a candidate
			// before Publish atomically replaces current. Only successful replacement
			// retires embedded textures; failed and duplicate reloads keep their owners.
            bool hasCurrent{};
            std::uint64_t epoch{}, resolverRevision{};
            {
                std::lock_guard admissionLock(m_assetPreparationMutex);
                hasCurrent = !!m_modelAssetGenerations.ResolveCurrent(guid.m_guid);
                epoch = m_assetPreparationEpoch;
                resolverRevision = m_assetDepotRevision;
            }
            if (hasCurrent)
            {
                return !!LoadAndPublishModelAssetGeneration(guid, true, true, epoch, resolverRevision);
            }
			return true;
		}
		// 파일 게시는 이미 끝났다. 먼저 이전 generation을 cache lookup에서
		// 분리한 뒤 catalog를 갱신해, 이 호출 이후의 load가 새 파일을 읽게 한다.
		RetireCachedAsset(assetType, change.path, change.guid, false);
		if (change.guid != FileGuid{})
			return RegisterAssetMeta(*registry, change.guid, change.path);
		return true;
	case RuntimeAssetChangeKind::Removed:
		RetireCachedAsset(assetType, change.path, change.guid, true);
		registry->Unregister(change.path);
		return true;
	}
	return false;
}

void DataSystem::RetireCachedAsset(RuntimeAssetType assetType,
	const file::path& path, FileGuid guid, bool remove)
{
    const auto registry = SnapshotAssetMetaRegistry();
	if (RuntimeAssetType::Auto == assetType)
		assetType = ResolveRuntimeAssetType(path);
	if (RuntimeAssetType::CatalogOnly == assetType) return;
    if (assetType == RuntimeAssetType::MaterialGraph)
    {
        if (guid == FileGuid{} && registry)
        {
            guid = registry->GetGuid(path);
        }
        if (remove)
            m_materialGraphGenerations.Remove(experiment::AssetId{guid.m_guid});
        return;
    }
    if (RuntimeAssetType::ShaderMeta == assetType)
	{
        if (FileGuid{} == guid && registry)
        {
            guid = registry->GetGuid(path);
        }
		InvalidateShaderMeta(guid, remove);
		return;
	}

	// G2 — 텍스처 캐시는 적재와 같은 신원으로 뗀다. stem 으로 떼면 다른 폴더의 같은
	// 이름 항목이 대신 떨어졌다. 재질 캐시는 이름 키 그대로다.
	const std::string key = RuntimeAssetType::Material == assetType
		? path.stem().string() : TextureCacheKey(path);
	auto detach = [&key](auto& cache, std::mutex& cacheMutex)
	{
		using Resource = typename std::decay_t<decltype(cache)>::mapped_type::Resource;
        own::shared_owner<const Resource> generation;
		{
			std::lock_guard lock(cacheMutex);
			const auto iterator = cache.find(key);
			if (iterator == cache.end()) return generation;
			generation = asset_cache_detail::Acquire(iterator->second);
			cache.erase(iterator);
		}
		return generation;
	};

	switch (assetType)
	{
	case RuntimeAssetType::Model:
        if (FileGuid{} == guid && registry)
        {
            guid = registry->GetGuid(path);
        }
		if (FileGuid{} != guid)
		{
			// MBC7 — generation과 그 embedded texture owner는 한 단위로 은퇴한다.
            std::lock_guard admissionLock(m_assetPreparationMutex);
            assets::ModelAssetGenerationHandle retiredHandle;
            const auto retired = m_modelAssetGenerations.Retire(guid.m_guid, &retiredHandle);
            if (retiredHandle.IsValid())
            {
                RetireModelGenerationTextures(retiredHandle);
            }
		}
		break;
	case RuntimeAssetType::Material:
		(void)detach(Materials, m_materialMutex);
        {
            const auto materialGuid = guid != FileGuid{} ? guid : GetFileGuid(path);
            std::lock_guard lock(m_authoredMaterialMutex);
            m_authoredMaterials.erase(materialGuid);
        }
		break;
    case RuntimeAssetType::Texture:
    case RuntimeAssetType::UITexture:
    case RuntimeAssetType::SpriteSheet:
    {
        // A source publication invalidates every role/representation, including
        // UI and sprite uses loaded from the same file in their separate maps.
        const auto materialPrefix = MaterialTextureKeyPrefix(path);
        const std::array roleKeys{
            key,
            TextureRoleCacheKey(key, TextureFileType::MaterialTexture),
            TextureRoleCacheKey(key, TextureFileType::TerrainTexture),
            TextureRoleCacheKey(key, TextureFileType::HDR)};
        std::vector<own::shared_owner<const Texture>> released;
        {
            std::lock_guard lock(m_textureMutex);
            for (auto* cache : { &Textures, &UITextures, &SpriteSheets })
            {
                for (auto iterator = cache->begin(); iterator != cache->end();)
                {
                    const bool matchesRole = std::find(roleKeys.begin(), roleKeys.end(), iterator->first)
                        != roleKeys.end();
                    if (!matchesRole && !iterator->first.starts_with(materialPrefix))
                    {
                        ++iterator;
                        continue;
                    }
                    released.push_back(asset_cache_detail::Acquire(iterator->second));
                    iterator = cache->erase(iterator);
                }
            }
        }
        // Release only the cache references, outside its mutex. Scene/proxy/
        // recording pins keep each exact old representation alive independently.
        break;
    }
	default:
		break;
	}
}

FileGuid DataSystem::GetFilenameToGuid(const std::string& filename) const
{
    const auto registry = SnapshotAssetMetaRegistry();
	return registry
		? registry->GetFilenameToGuid(filename) : FileGuid{};
}

FileGuid DataSystem::GetStemToGuid(const std::string& stem) const
{
    const auto registry = SnapshotAssetMetaRegistry();
	return registry ? registry->GetStemToGuid(stem) : FileGuid{};
}

file::path DataSystem::GetFilePath(FileGuid fileguid) const
{
    const auto registry = SnapshotAssetMetaRegistry();
	return registry ? registry->GetPath(fileguid) : file::path{};
}

own::shared_owner<DataSystem::AssetBundlePreparation> DataSystem::SubmitAssetBundle(const AssetBundle& bundle)
{
    auto request = own::make_shared<AssetBundlePreparation>();
    request->result.submitted = bundle.assets.size();
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        request->result.status = AssetDepot::AssetRequestStatus::Cancelled;
        request->result.error = AssetDepot::AssetRequestError::ShuttingDown;
        return request;
    }
    std::erase_if(m_assetBundlePreparations, [](const auto& request) { return request.expired(); });
    request->epoch = m_assetPreparationEpoch;
    job_group jobs;
    for (const auto& entry : bundle.assets)
    {
        const auto type = static_cast<ManagedAssetType>(entry.assetTypeID);
        const file::path name = entry.assetName;
        jobs.add([this, request, type, name]
        {
            ce::profile_scope profile{ ce::marker<"Asset.BundleJob">() };
            {
                std::lock_guard lock(m_assetPreparationMutex);
                if (request->cancelled.load(std::memory_order_acquire) ||
                    m_assetPreparationStopping || request->epoch != m_assetPreparationEpoch)
                {
                    request->cancelled.store(true, std::memory_order_release);
                    throw std::runtime_error("Asset bundle loading was cancelled or invalidated.");
                }
            }
            assets::ModelAssetGeneration::Shared model;
            own::shared_owner<const Material> material;
            own::shared_owner<const Texture> texture;
            switch (type)
            {
            case ManagedAssetType::Model:
                model = LoadModelAssetGenerationByPath(name.string());
                if (!model)
                {
                    throw std::runtime_error("Required bundle model could not be loaded.");
                }
                break;
            case ManagedAssetType::Material:
                material = LoadMaterial(name.string());
                if (!material)
                {
                    throw std::runtime_error("Required bundle material could not be loaded.");
                }
                break;
            case ManagedAssetType::Texture:
            case ManagedAssetType::UITexture:
            case ManagedAssetType::SpriteSheet:
                texture = LoadTexture(name.string(), type == ManagedAssetType::UITexture
                    ? TextureFileType::UITexture : (type == ManagedAssetType::SpriteSheet
                        ? TextureFileType::SpriteSheet : TextureFileType::Texture));
                if (!texture)
                {
                    throw std::runtime_error("Required bundle texture could not be loaded.");
                }
                break;
            case ManagedAssetType::SpriteFont:
                // Font loading retired with DX11 SpriteFont; old entries are ignored.
                break;
            default:
                break;
            }
            std::lock_guard preparationLock(m_assetPreparationMutex);
            if (request->cancelled.load(std::memory_order_acquire) ||
                m_assetPreparationStopping || request->epoch != m_assetPreparationEpoch)
            {
                request->cancelled.store(true, std::memory_order_release);
                throw std::runtime_error("Asset bundle loading was cancelled or invalidated.");
            }
            std::lock_guard resultLock(request->mutex);
            if (model)
            {
                request->result.models.push_back(std::move(model));
            }
            if (material)
            {
                request->result.materials.push_back(std::move(material));
            }
            if (texture)
            {
                request->result.textures.push_back(std::move(texture));
            }
            ++request->result.completed;
        });
    }
    // This observer also runs if dispatch/dependencies fail before a task body.
    // It uses only request synchronization, so inline submission failure cannot
    // recursively take the already-held DataSystem admission mutex.
    jobs.on_complete([request](std::exception_ptr error)
    {
        std::lock_guard resultLock(request->mutex);
        if (request->cancelled.load(std::memory_order_acquire))
        {
            request->result.status = AssetDepot::AssetRequestStatus::Cancelled;
            request->result.error = AssetDepot::AssetRequestError::None;
        }
        else if (error)
        {
            request->result.status = AssetDepot::AssetRequestStatus::Failed;
            request->result.error = AssetDepot::AssetRequestError::ReadFailed;
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& failure)
            {
                request->result.message = failure.what();
            }
            catch (...)
            {
                request->result.message = "Asset bundle work failed.";
            }
        }
        else
        {
            request->result.status = AssetDepot::AssetRequestStatus::Ready;
        }
        if (request->result.status != AssetDepot::AssetRequestStatus::Ready)
        {
            request->result.models.clear();
            request->result.materials.clear();
            request->result.textures.clear();
        }
    });
    m_assetBundlePreparations.emplace_back(request);
    try
    {
        auto completion = SubmitAssetWorkLocked(std::move(jobs));
        std::lock_guard resultLock(request->mutex);
        request->work = std::move(completion);
    }
    catch (...)
    {
        std::lock_guard resultLock(request->mutex);
        request->result.status = AssetDepot::AssetRequestStatus::Failed;
        request->result.error = AssetDepot::AssetRequestError::SubmissionFailed;
        request->result.models.clear();
        request->result.materials.clear();
        request->result.textures.clear();
    }
    return request;
}

AssetBundleLoadResult DataSystem::LoadAssetBundle(const AssetBundle& bundle)
{
    ce::profile_scope profile{ ce::marker<"Asset.BundleWait">() };
    const auto request = SubmitAssetBundle(bundle);
    request->Completion().wait();
    return request->Snapshot();
}

own::shared_owner<DataSystem::AssetBundlePreparation> DataSystem::LoadAssetBundleAsync(const AssetBundle& bundle)
{
    return SubmitAssetBundle(bundle);
}

void DataSystem::RetainAssets(const AssetBundle& bundle)
{
	std::lock_guard retainedGuard(m_retainedAssetsMutex);
	for (const auto& entry : bundle.assets)
	{
		file::path name = entry.assetName;
		// G2 — 텍스처 캐시는 경로 키다. 적재와 같은 해석으로 키를 만들지 않으면
		// UnloadUnusedAssets 가 번들이 붙든 텍스처를 전부 "안 쓰는 것" 으로 지운다.
		std::string key;
		switch (static_cast<ManagedAssetType>(entry.assetTypeID))
		{
		case ManagedAssetType::Texture:
			key = TextureCacheKey(ResolveRuntimeAssetPath(entry.assetName, TextureFallbackDirectory(TextureFileType::Texture)));
			break;
		case ManagedAssetType::UITexture:
			key = TextureCacheKey(ResolveRuntimeAssetPath(entry.assetName, TextureFallbackDirectory(TextureFileType::UITexture)));
			break;
		case ManagedAssetType::SpriteSheet:
			key = TextureCacheKey(ResolveRuntimeAssetPath(entry.assetName, TextureFallbackDirectory(TextureFileType::SpriteSheet)));
			break;
		default:
			key = name.stem().string();
			break;
		}
		m_retainedAssets[entry.assetTypeID].insert(std::move(key));
	}
}

void DataSystem::ClearRetainedAssets()
{
	std::lock_guard retainedGuard(m_retainedAssetsMutex);
	m_retainedAssets.clear();
}

size_t DataSystem::SnapshotRetainedAssetCount() const
{
	std::lock_guard retainedGuard(m_retainedAssetsMutex);
	size_t count = 0;
	for (const auto& [type, names] : m_retainedAssets) count += names.size();
	return count;
}

void DataSystem::UnloadUnusedAssets()
{
	std::lock_guard retainedGuard(m_retainedAssetsMutex);
	// 캐시에서 지운다고 곧바로 파괴되는 것이 아니다.
	// 컴포넌트·프록시·Model이 shared_ptr로 공동 소유하므로(2-2~2-5),
	// 아직 사용 중인 에셋은 참조가 남아 살아 있고 실제 해제는 마지막 참조가
	// 사라질 때 일어난다. 여기서 하는 일은 "캐시가 붙들고 있던 몫을 놓는 것"이다.
	auto removeUnused = [this](auto& container, int type, std::mutex& guardMutex)
	{
		// 이 타입을 에셋 번들이 관리하지 않으면 손대지 않는다.
		//
		// m_retainedAssets는 번들에 등록된 항목으로만 채워지는데, 현재 AssetBundleWindow는
		// Model/Material/Texture/SpriteFont 네 종만 등록한다. 그대로 두면 UITexture와
		// SpriteSheet는 "보존 목록이 비어 있다" = "전부 불필요"로 해석되어 통째로 날아간다.
		// 번들이 해당 타입을 다루기 시작하면 자동으로 정상 동작한다.
		auto retainIt = m_retainedAssets.find(type);
		if (retainIt == m_retainedAssets.end())
		{
			return;
		}

		std::lock_guard<std::mutex> guard(guardMutex);

		auto& retainSet = retainIt->second;
		auto it = container.begin();
		while (it != container.end())
		{
			if (retainSet.find(it->first) == retainSet.end())
			{
                it->second.retained.reset();
                it->second.retainedBytes = 0u;
                ++it;
			}
			else
			{
				++it;
			}
		}
        asset_cache_detail::PruneExpired(container);
	};

	removeUnused(Materials,    static_cast<int>(ManagedAssetType::Material),    m_materialMutex);
	removeUnused(Textures,     static_cast<int>(ManagedAssetType::Texture),     m_textureMutex);
	// 예전에는 이 두 캐시가 ManagedAssetType에 없어 언로드 대상에서 아예 빠져 있었다(12.2-②).
	removeUnused(UITextures,   static_cast<int>(ManagedAssetType::UITexture),   m_textureMutex);
	removeUnused(SpriteSheets, static_cast<int>(ManagedAssetType::SpriteSheet), m_textureMutex);
}

// ── I7-C1/C2 cooked catalog (MBC9: ExperimentModelMigration.cpp에서 이주) ──
// ── I7-C1: cooked catalog 기동 ──────────────────────────────────────────
//
// 굽는 쪽은 D5-b2c에서 다 섰는데(AssetCooker → Derived/ + CEMF → pak) **읽는
// 쪽이 이어져 있지 않았다**: resolver는 `nullptr` catalog로 불렸고
// `ModelLoadRequest::cookedPath`는 늘 비어 있었다(실측 texCooked=0). 그래서
// cooked 경로가 제품에서 한 번도 돌지 않았다 — 이 함수가 그 입구다.
bool DataSystem::MountCookedCatalog(const file::path& derivedRoot,
	std::string& outError)
{
	outError.clear();
    std::uint64_t expectedRevision{};
    std::uint64_t expectedEpoch{};
    own::shared_owner<AssetMetaRegistry> registryForFreshness;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
        {
            outError = "Catalog admission is stopped or invalidating.";
            return false;
        }
        if (m_cookedCatalog && m_cookedCatalog->MountCount() != 0u)
        {
            outError = "Unmount active AssetSets before replacing the legacy catalog.";
            return false;
        }
        if (m_assetDepotRevision == (std::numeric_limits<std::uint64_t>::max)())
        {
            outError = "Catalog resolver revision space exhausted.";
            return false;
        }
        expectedRevision = m_assetDepotRevision;
        expectedEpoch = m_assetPreparationEpoch;
        registryForFreshness = m_assetMetaRegistry;
    }
	const file::path manifestPath =
		derivedRoot / "Derived" / "asset-manifest.cemf";
	std::error_code errorCode;
	if (!file::is_regular_file(manifestPath, errorCode))
	{
		// 미게시는 실패가 아니다 — 에디터 작업 트리에는 Derived가 없다.
		// 조용히 두면 resolver·모델 로드가 예전처럼 source로 간다.
        // Failed or absent candidates preserve the currently published catalog.
		return false;
	}

	std::vector<std::byte> bytes;
	{
		std::ifstream input(manifestPath, std::ios::binary);
		if (!input)
		{
			outError = "manifest를 열 수 없다: " + manifestPath.string();
			return false;
		}
		input.seekg(0, std::ios::end);
		const std::streamoff size = input.tellg();
		input.seekg(0, std::ios::beg);
		bytes.resize(static_cast<std::size_t>(size));
		if (size > 0)
		{
			input.read(reinterpret_cast<char*>(bytes.data()), size);
			if (!input)
			{
				outError = "manifest 읽기가 끊겼다: " + manifestPath.string();
				return false;
			}
		}
	}

	std::vector<experiment::cooked::AssetManifestIssue> issues;
	// Load는 실패하면 **빈 catalog**를 준다(부분 표를 내놓지 않는 계약).
	auto catalog = own::make_shared<const experiment::cooked::CookedAssetCatalog>(
		experiment::cooked::CookedAssetCatalog::Load(bytes, derivedRoot, issues));
	if (catalog->IsEmpty())
	{
		outError = "catalog가 비었다(entry 0) — manifest 손상 또는 빈 게시";
		for (const auto& issue : issues)
		{
			outError += " | " + issue.context + ": " + issue.message;
		}
        // Failed or absent candidates preserve the currently published catalog.
		return false;
	}

	// Packaged Player에는 `.meta`가 없다. CEMF v2의 source identity table로
	// AssetMetaRegistry 전체를 새로 만든 뒤 한 번에 교체한다. Editor는 watcher가
	// 갱신하는 source registry가 정본이므로 optional cooked mount가 건드리지 않는다.
	const bool authoring = PathFinder::IsAssetAuthoringEnabled();
	own::shared_owner<AssetMetaRegistry> packagedRegistry;
	if (!authoring)
	{
		if (catalog->SourceAssetCount() == 0u)
		{
			outError = "CEMF source identity table이 비었다.";
			return false;
		}

		packagedRegistry = own::make_shared<AssetMetaRegistry>();
		for (const experiment::cooked::AssetSourceManifestEntry& source
			: catalog->SourceAssets())
		{
			const file::path sourcePath = catalog->ResolveSourcePath(source.assetId);
			const auto* cooked = catalog->Find(source.assetId);
            // Generated graph metadata/source and bytecode live in the v3
            // artifact. Player keeps the source identity, not an authoring file.
            const bool cookedGraph = cooked && sourcePath.extension() == ".shadergraph" &&
                cooked->kind == experiment::cooked::CookedAssetKind::MaterialProgram &&
                cooked->formatVersion == material_graph::CookedProgramVersion;
			std::error_code sourceError;
			if (sourcePath.empty()
				|| (!cookedGraph && (!file::is_regular_file(sourcePath, sourceError) || sourceError)))
			{
				outError = "CEMF source asset이 package에 없다: "
					+ source.sourcePath;
				return false;
			}

			FileGuid guid;
			guid.m_guid = source.assetId.value;
			if (AssetMetaRegistrationResult::Registered
				!= packagedRegistry->Register(guid, sourcePath))
			{
				outError = "CEMF source identity 등록 충돌: "
					+ source.sourcePath;
				return false;
			}
		}
	}

	// ── I7-C2: 신선도 판정 ──
	//
	// Editor optional mount에서만 source mtime과 비교한다. Packaged Player는 pak
	// 해제 시각이 source/Derived에 새로 찍혀 상대 순서가 원래 cook 순서를 뜻하지
	// 않는다. Player의 신선도는 같은 package transaction과 CEMF size/hash 검증이
	// 보증하며, 여기서 mtime을 비교하면 정상 패키지가 대량 stale로 오판된다.
	// registry가 GUID를 못 푸는 entry(모델 안의 subasset 등)는 부모 모델 entry가
	// 대신 책임진다.
	std::unordered_set<FileGuid> stale;
	if (authoring)
	{
		for (const experiment::cooked::CookedAssetManifestEntry& entry
			: catalog->Entries())
		{
			FileGuid guid;
			guid.m_guid = entry.assetId.value;
			const file::path sourcePath = registryForFreshness
				? registryForFreshness->GetPath(guid) : file::path{};
			if (sourcePath.empty()) continue;
			std::error_code sourceError;
			if (!file::is_regular_file(sourcePath, sourceError)) continue;
			const file::path artifact = catalog->ResolveArtifactPath(entry.assetId);
			std::error_code artifactError;
			if (artifact.empty()
				|| !file::is_regular_file(artifact, artifactError))
			{
				// 표에는 있는데 파일이 없다 — 낡음보다 나쁘다. 같이 끊는다.
				stale.insert(guid);
				continue;
			}
			const auto sourceTime = file::last_write_time(sourcePath, sourceError);
			const auto artifactTime = file::last_write_time(artifact, artifactError);
			if (sourceError || artifactError) continue;
			if (artifactTime < sourceTime) stale.insert(guid);
		}
	}

	const std::size_t entryCount = catalog->Size();
	const std::size_t sourceAssetCount = catalog->SourceAssetCount();
	const std::size_t staleCount = stale.size();
    own::shared_owner<const experiment::cooked::CookedAssetCatalog> retiredCatalog;
    own::shared_owner<AssetMetaRegistry> retiredRegistry;
    AssetDepot::TextureAssetRetiredEntries retiredTextures;
    AssetDepot::ModelAssetRetiredEntries retiredModels;
    LegacyCacheRetirement retiredLegacy;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u
            || m_assetPreparationEpoch != expectedEpoch || m_assetDepotRevision != expectedRevision)
        {
            outError = "Catalog changed before publication; legacy mount is stale.";
            return false;
        }
        StageLegacyCacheRetirementLocked(retiredLegacy);
        StageTextureAssetRetirementLocked(retiredTextures);
        StageModelAssetRetirementLocked(retiredModels);
        retiredCatalog = std::move(m_cookedCatalog);
        InvalidateTextureAssetsLocked(retiredTextures);
        InvalidateModelAssetsLocked(retiredModels);
        m_cookedCatalog = std::move(catalog);
        m_cookedStaleAssets = std::move(stale);
        ++m_assetDepotRevision;
        DetachLegacyCachesLocked(retiredLegacy);
        if (packagedRegistry)
        {
            retiredRegistry = std::move(m_assetMetaRegistry);
            m_assetMetaRegistry = std::move(packagedRegistry);
        }
    }
    // Current model/material-graph indexes were detached in the root commit;
    // existing scene owners continue to own their accepted generations.
    std::printf("[cooked.catalog] mount %s entries=%zu sources=%zu stale=%zu\n",
		manifestPath.string().c_str(), entryCount, sourceAssetCount, staleCount);
	return true;
}

own::shared_owner<const experiment::cooked::CookedAssetCatalog>
DataSystem::GetCookedCatalog() const
{
	std::lock_guard lock(m_cookedCatalogMutex);
	return m_cookedCatalog;
}

file::path DataSystem::ResolveCookedArtifact(
	const experiment::AssetId& assetId) const
{
	std::lock_guard lock(m_cookedCatalogMutex);
	if (!m_cookedCatalog) return {};
	FileGuid probe;
	probe.m_guid = assetId.value;
	if (m_cookedStaleAssets.contains(probe)) return {};
	return m_cookedCatalog->ResolveArtifactPath(assetId);
}

file::path DataSystem::ResolveCatalogAssetPath(FileGuid assetGuid) const
{
	if (FileGuid{} == assetGuid) return {};
	experiment::AssetId assetId{ assetGuid.m_guid };
	if (file::path cooked = ResolveCookedArtifact(assetId); !cooked.empty())
		return cooked;

	// Editor owns authoring recovery and stale-source fallback. A packaged Player
	// has no such authority: if a produced asset is absent/stale in CEMF, make the
	// caller fail instead of making source files an accidental runtime contract.
	return PathFinder::IsAssetAuthoringEnabled()
		? GetFilePath(assetGuid) : file::path{};
}

std::size_t DataSystem::CookedCatalogStaleCount() const
{
	std::lock_guard lock(m_cookedCatalogMutex);
	return m_cookedStaleAssets.size();
}

std::size_t DataSystem::CookedCatalogEntryCount() const
{
	const auto catalog = GetCookedCatalog();
	return catalog ? catalog->Size() : 0u;
}

std::size_t DataSystem::CookedCatalogSourceAssetCount() const
{
	const auto catalog = GetCookedCatalog();
	return catalog ? catalog->SourceAssetCount() : 0u;
}

// ── PHASE 3.75 MBC9 — 모델 generation 창구(경로·stem·sidecar 옵션) ─────────────

assets::ModelAssetGeneration::Shared DataSystem::LoadModelAssetGenerationByPath(std::string_view filePath)
{
    std::uint64_t epoch{}, resolverRevision{};
    own::shared_owner<AssetMetaRegistry> registry;
    {
        std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        epoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
        registry = SnapshotAssetMetaRegistry();
    }
    const file::path path = ResolveRuntimeAssetPath(filePath, "Models\\");
    auto guid = registry ? registry->GetGuid(path) : FileGuid{};
    if (guid == FileGuid{} && registry)
    {
        guid = registry->GetFilenameToGuid(path.filename().string());
    }
    if (guid == FileGuid{})
    {
        Debug::PrintLog(spdlog::level::err, "[model.generation] registry has no model path: " + path.string());
        return {};
    }
    auto model = LoadModelAssetGeneration(guid);
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
        || epoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
    return model;
}

assets::ModelAssetGeneration::Shared DataSystem::FindModelAssetGenerationByStem(std::string_view stem)
{
    std::uint64_t epoch{}, resolverRevision{};
    own::shared_owner<AssetMetaRegistry> registry;
    {
        std::lock_guard admissionLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            return {};
        }
        epoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
        registry = SnapshotAssetMetaRegistry();
    }
    const FileGuid guid = registry ? registry->GetStemToGuid(std::string(stem)) : FileGuid{};
    if (guid == FileGuid{})
    {
        return {};
    }
    auto model = LoadModelAssetGeneration(guid);
    std::lock_guard admissionLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0
        || epoch != m_assetPreparationEpoch || resolverRevision != m_assetDepotRevision)
    {
        return {};
    }
    return model;
}

bool DataSystem::ReadModelCreateMeshCollider(FileGuid guid) const
{
	const file::path sourcePath = GetFilePath(guid);
	if (sourcePath.empty()) return false;
	file::path sidecarPath = sourcePath;
	sidecarPath += ".meta";
	std::string parseError;
	const Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseFile(sidecarPath.string(), parseError);
	if (!document) return false;
	const Authoring::ReadNode importer = document.Root()["ModelImporter"];
	if (!importer || !importer["CreateMeshCollider"]) return false;
	return importer["CreateMeshCollider"].As<bool>();
}
