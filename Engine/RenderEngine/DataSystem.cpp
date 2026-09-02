#include "DataSystem.h"
#include "Model.h"	
#include <future>
#include <ppltasks.h>
#include <ppl.h>
#include "AuthoringBase64.h"
#include "Benchmark.hpp"
// SceneManager.h가 여기 있었다. LoadAssetBundle이 씬 매니저가 들고 있던
// 스레드풀을 빌려 쓰느라 층 3이 층 4를 올려다봤다. 풀의 소유를 층 1로
// 내리면서(WorkerPool.h) 그 이유가 사라졌다 — PHASE 4-3 슬라이스 3.
#include "WorkerPool.h"
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

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>

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

		return RuntimeAssetType::CatalogOnly;
	}

	file::path ResolveRuntimeAssetPath(std::string_view requestedPath,
		std::string_view fallbackDirectory)
	{
		const file::path requested(requestedPath);
		std::error_code error;
		if (file::is_regular_file(requested, error) && !error) return requested;
		return PathFinder::Relative(std::string(fallbackDirectory)) / requested.filename();
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

		Debug->LogError("Asset catalog rejected meta registration: guid="
			+ guid.ToString() + " path=" + path.string() + " reason=" + reason);
		return false;
	}
}

DataSystem::~DataSystem()
{
	Finalize();
}

void DataSystem::Initialize()
{
	m_assetMetaRegistry = std::make_shared<AssetMetaRegistry>();
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
		Debug->LogWarning("[cooked.catalog] 마운트 실패: " + cookedCatalogError);
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
    Models.clear();
    Textures.clear();
    Materials.clear();
	UITextures.clear();
	SpriteSheets.clear();
	m_retainedAssets.clear();
	{
		std::lock_guard lock(m_retiredTextureMutex);
		m_retiredTextureGenerations.clear();
	}
	{
		std::lock_guard lock(m_shaderMetaMutex);
		m_shaderMetaSlotByGuid.clear();
		m_shaderMetaSlots.clear();
		m_shaderMetaFreeSlots.clear();
	}
	{
		std::lock_guard lock(m_pendingAssetChangeMutex);
		m_pendingAssetChanges.clear();
	}

	m_assetMetaRegistry.reset();
}

void DataSystem::LoadAssetCatalog(const file::path& root)
{
	if (!file::exists(root)) return;

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
					Debug->LogWarning("Asset catalog ignored invalid meta: " +
						entry.path().string() + " (" + parseError + ")");
				}
				else
				{
					++parsedMetaCount;
					const Authoring::ReadNode node = document.Root();
					if (node["guid"] && node["guid"].IsScalar())
					{
						const FileGuid guid(node["guid"].AsString());
						if (guid != FileGuid{})
							RegisterAssetMeta(*m_assetMetaRegistry, guid, targetPath);
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
		static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			catalogElapsed).count()),
		parsedMetaCount);
}

Model* DataSystem::LoadModelGUID(FileGuid guid)
{
	file::path modelPath = m_assetMetaRegistry->GetPath(guid);
	std::string name = modelPath.stem().string();
	{
		std::unique_lock lock(m_modelMutex);
		if (Models.find(name) != Models.end())
		{
			Debug->Log("ModelLoader::LoadModel : Model already loaded");
			auto model = Models[name].get();
			return model;
		}
	}

	// I5-D1b — 로더 이중화. experiment(cooked→source) 로드가 정본을 향한
	// 경로이고, 성공하면 역브리지가 legacy 파이프에 소비시킨다. 실패는
	// Assimp 폴백(관측 로그) — 화면이 조용히 비는 것보다 legacy가 낫다.
	if (std::shared_ptr<Model> bridged = LoadModelViaExperiment(guid, modelPath))
	{
		Model* raw = bridged.get();
		{
			std::unique_lock lock(m_modelMutex);
			Models[name] = std::move(bridged);
		}
		return raw;
	}

	Model* model = Model::LoadModel(modelPath.string());
	if (model)
	{
		{
			std::unique_lock lock(m_modelMutex);
			Models[name] = std::shared_ptr<Model>(model);
		}
		return model;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}

	return nullptr;
}

void DataSystem::LoadModel(std::string_view filePath)
{
	const file::path assetPath = ResolveRuntimeAssetPath(filePath, "Models\\");
	std::string name = assetPath.stem().string();
	{
		std::lock_guard<std::mutex> guard(m_modelMutex);
		auto iter = Models.find(name);
		if (iter != Models.end() && iter->second)
		{
			Debug->Log("ModelLoader::LoadModel : Model already loaded");
			return;
		}
	}

	// I5-D34b — 이름 경로도 experiment 이중화(LoadModelGUID와 같은 결). 이게
	// 없으면 CLI(model.load)·에디터의 이름 기반 로드가 legacy 경로로 남아
	// 병행 바인딩이 비고, 스킨 게이트가 성립하지 않는다. D4의 로드 수렴을
	// 이 절반만 앞당긴 것이다.
	std::shared_ptr<Model> model;
	const FileGuid guid = GetFileGuid(assetPath);
	if (FileGuid{} != guid)
	{
		model = LoadModelViaExperiment(guid, assetPath);
	}
	if (!model)
	{
		model = Model::LoadModelShared(assetPath.string());
	}
	if (model)
	{
		{
			std::unique_lock lock(m_modelMutex);
			Models[name] = model;
		}
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}
}

std::shared_ptr<Model> DataSystem::LoadCachedModelShared(std::string_view filePath)
{
	const file::path assetPath = ResolveRuntimeAssetPath(filePath, "Models\\");
	std::string name = assetPath.stem().string();
	{
		std::unique_lock lock(m_modelMutex);
		if (Models.find(name) != Models.end() && Models[name].get() != nullptr)
		{
			Debug->Log("ModelLoader::LoadModel : Model already loaded");
			return Models[name];
		}
	}

	// ★ I6-B4b 후속 — 여기가 **에디터 드롭 경로**다(HierarchyWindow·
	// SceneViewWindow의 콘텐츠 브라우저 드래그, TerrainComponent, Foliage).
	// D34b가 `LoadModel`을 experiment로 이중화하면서 주석에 "에디터의 이름
	// 기반 로드"를 적었는데, **정작 에디터가 부르는 것은 이 함수였다** —
	// 그래서 드롭한 모델만 experiment 등록이 비고, 재생 바인딩이 서지 못했다.
	//
	// B4b가 legacy 재귀 틱을 걷기 전까지는 이 구멍이 폴백에 덮여 보이지
	// 않았다(애니메이션이 legacy 경로로 돌았다). 틱이 하나가 되자 곧바로
	// "드롭한 애니메이션 모델이 아무것도 안 그린다"로 드러났다 — 팔레트가
	// 한 번도 안 쓰이면 스킨 정점이 원점으로 접힌다.
	std::shared_ptr<Model> model{};
	const FileGuid guid = GetFileGuid(assetPath);
	if (FileGuid{} != guid)
	{
		model = LoadModelViaExperiment(guid, assetPath);
	}
	if (!model)
	{
		try
		{
			std::string modelPath = assetPath.string();
			model = Model::LoadModelShared(modelPath);
		}
		catch (const std::exception& e)
		{
			Debug->LogError(e.what());
			return {};
		}
	}

	if (model)
	{
		{
			std::unique_lock lock(m_modelMutex);
			Models[name] = model;
		}
		return model;
	}

	return {};
}

Model* DataSystem::LoadCashedModel(std::string_view filePath)
{
	return LoadCachedModelShared(filePath).get();
}

void DataSystem::InsertMaterial(std::shared_ptr<Material> material)
{
	if (material) (void)RegisterImportedMaterial(material, material->m_name);
}

std::shared_ptr<Model> DataSystem::FindCachedModel(std::string_view name)
{
	std::lock_guard<std::mutex> guard(m_modelMutex);
	auto iter = Models.find(std::string(name));
	return iter == Models.end() ? nullptr : iter->second;
}

std::vector<std::pair<std::string, std::shared_ptr<Model>>> DataSystem::SnapshotModels()
{
	std::lock_guard<std::mutex> guard(m_modelMutex);
	return { Models.begin(), Models.end() };
}

std::vector<std::pair<std::string, std::shared_ptr<Texture>>> DataSystem::SnapshotTextures()
{
	std::lock_guard<std::mutex> guard(m_textureMutex);
	return { Textures.begin(), Textures.end() };
}

std::shared_ptr<Material> DataSystem::FindCachedMaterial(std::string_view name)
{
	std::lock_guard<std::mutex> guard(m_materialMutex);
	auto iter = Materials.find(std::string(name));
	return iter == Materials.end() ? nullptr : iter->second;
}

std::vector<std::pair<std::string, std::shared_ptr<Material>>> DataSystem::SnapshotMaterials()
{
	std::lock_guard<std::mutex> guard(m_materialMutex);
	return { Materials.begin(), Materials.end() };
}

std::shared_ptr<Material> DataSystem::RegisterImportedMaterial(
	std::shared_ptr<Material> material, std::string_view baseName)
{
	if (!material) return nullptr;

	std::lock_guard<std::mutex> guard(m_materialMutex);
	const std::string base = baseName.empty() ? material->m_name : std::string(baseName);
	std::string candidate = material->m_name.empty() ? base : material->m_name;
	int suffix = 1;
	while (true)
	{
		auto iter = Materials.find(candidate);
		if (iter == Materials.end() || !iter->second)
		{
			material->m_name = candidate;
			Materials[candidate] = material;
			return material;
		}
		if (iter->second->m_fileGuid == material->m_fileGuid)
			return iter->second;

		candidate = base + "(" + std::to_string(suffix++) + ")";
	}
}

void DataSystem::SynchronizeLegacyMaterialProperties(Material& material) const
{
	auto resolveGuid = [this](std::string_view textureName)
	{
		if (textureName.empty() || !m_assetMetaRegistry) return FileGuid{};

		const file::path filename = file::path(textureName).filename();
		const file::path materialPath = PathFinder::Relative("Materials\\") / filename;
		if (const FileGuid exact = m_assetMetaRegistry->GetGuid(materialPath);
			exact != FileGuid{})
		{
			return exact;
		}
		if (const FileGuid byFilename =
			m_assetMetaRegistry->GetFilenameToGuid(filename.string());
			byFilename != FileGuid{})
		{
			return byFilename;
		}
		return m_assetMetaRegistry->GetStemToGuid(filename.stem().string());
	};

	auto synchronize = [this, &material, &resolveGuid](std::string_view property,
		std::string& legacyName, Texture* runtimeTexture)
	{
		auto value = std::find_if(material.m_propertyValues.begin(),
			material.m_propertyValues.end(), [property](const MaterialPropertyValue& candidate)
			{
				return candidate.m_name == property;
			});

		FileGuid guid = value == material.m_propertyValues.end()
			? FileGuid{} : value->m_textureGuid;
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
		else if (guid != FileGuid{} && m_assetMetaRegistry)
		{
			const file::path path = m_assetMetaRegistry->GetPath(guid);
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
		material.m_baseColorTexName, material.GetBaseColorMapShared().get());
	synchronize(standard_material::property::NormalMap,
		material.m_normalTexName, material.GetNormalMapShared().get());
	synchronize(standard_material::property::OrmMap,
		material.m_ORM_TexName, material.GetOccRoughMetalMapShared().get());
	synchronize(standard_material::property::AoMap,
		material.m_AO_TexName, material.GetAOMapShared().get());
	synchronize(standard_material::property::EmissiveMap,
		material.m_EmissiveTexName, material.GetEmissiveMapShared().get());
}

bool DataSystem::SerializeMaterialPayload(Material& material,
	Authoring::WriteNode outNode) const
{
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
		// LoadShaderMetaHandle은 캐시 적재만 하는 논리적 const다 — 이 함수의
		// const 계약(재질 형상 관찰)은 유지된다.
		const ShaderMetaHandle handle = const_cast<DataSystem*>(this)
			->LoadShaderMetaHandle(material.m_shaderMetaGuid, error);
		if (const std::shared_ptr<const ShaderMeta> meta = ResolveShaderMeta(handle))
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
		Debug->LogWarning("Material 새 정본 writer 실패 — legacy 표기로 폴백"
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
	return DeserializeMaterialPayload(material, view, nullptr);
}

bool DataSystem::DeserializeMaterialPayload(Material& material,
	const Authoring::NodeView& view, experiment::Material* outAuthored)
{
	const Authoring::ReadNode readNode = Authoring::NodeViewAccess::Node(view);
	if (!readNode || !readNode.IsMap()) return false;

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
			Debug->LogError("Material 새 정본 decode 실패: " + error);
			return false;
		}
		const ShaderMeta* metaForKeywords = nullptr;
		std::shared_ptr<const ShaderMeta> metaOwner;
		if (!authored.keywords.empty())
		{
			FileGuid shaderGuid{};
			shaderGuid.m_guid = authored.shaderAssetId.value;
			const ShaderMetaHandle handle =
				LoadShaderMetaHandle(shaderGuid, error);
			metaOwner = ResolveShaderMeta(handle);
			if (!metaOwner)
			{
				Debug->LogError("Material 새 정본 keywords 정규화용 ShaderMeta"
					" 로드 실패: " + error);
				return false;
			}
			metaForKeywords = metaOwner.get();
		}
		if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(authored,
			metaForKeywords, material, error))
		{
			Debug->LogError("Material 새 정본 변환 실패: " + error);
			return false;
		}
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
		Debug->LogError("Material payload deserialize failed: "
			+ std::string(exception.what()));
		return false;
	}

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
		Debug->LogError("Material binary payload encode failed: " + encodeError);
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
		Debug->LogError("Material binary payload decode failed: " + parseError);
		return false;
	}
	const Authoring::ReadNode payloadNode = document.Root();
	return DeserializeMaterialPayload(material,
		Authoring::NodeViewAccess::Make(payloadNode));
}

void DataSystem::FinalizeMaterialRuntime(Material& material)
{
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
		std::shared_ptr<Texture> texture;
		const auto value = std::find_if(material.m_propertyValues.begin(),
			material.m_propertyValues.end(), [property](const MaterialPropertyValue& candidate)
			{
				return candidate.m_name == property;
			});
		if (value != material.m_propertyValues.end()
			&& value->m_textureGuid != FileGuid{})
		{
			const file::path path = GetFilePath(value->m_textureGuid);
			if (!path.empty()) texture = LoadSharedMaterialTexture(path.string(), compress);
			// I2-E 후속 — registry 가 모르는 GUID 는 임베디드 등록부다(모델
			// sidecar subasset). 소스 로드가 바이트에서 만들어 둔 것.
			if (!texture) texture = FindEmbeddedTexture(value->m_textureGuid);
		}
		if (!texture && !name.empty())
			texture = LoadSharedMaterialTexture(name, compress);
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

		const file::path path = GetFilePath(value.m_textureGuid);
		std::shared_ptr<Texture> texture = path.empty()
			? FindEmbeddedTexture(value.m_textureGuid)
			: LoadSharedMaterialTexture(path.string(), false);
		if (!texture) continue;
		material.UseTextureMap(value.m_name, std::move(texture));
	}
}

void DataSystem::RegisterEmbeddedTexture(FileGuid guid, std::shared_ptr<Texture> texture)
{
	if (FileGuid{} == guid || !texture) return;
	std::lock_guard<std::mutex> guard(m_embeddedTextureMutex);
	// 먼저 넣은 쪽이 이긴다 — 같은 모델을 두 스레드가 로드해도 재질이
	// 서로 다른 텍스처 객체를 붙들지 않게.
	m_embeddedTextures.try_emplace(guid, std::move(texture));
}

std::shared_ptr<Texture> DataSystem::FindEmbeddedTexture(FileGuid guid) const
{
	if (FileGuid{} == guid) return nullptr;
	std::lock_guard<std::mutex> guard(m_embeddedTextureMutex);
	const auto found = m_embeddedTextures.find(guid);
	return found != m_embeddedTextures.end() ? found->second : nullptr;
}

Material* DataSystem::LoadMaterial(std::string_view name)
{
    std::string materialName(name);

    // 조회와 삽입만 락으로 감싼다. 중간의 파일 로딩은 LoadMaterialTexture를 호출하는데
    // 그쪽이 m_textureMutex를 잡으므로, 여기서 락을 유지하면 material→texture 순서의
    // 락 중첩이 생긴다. 락을 겹치지 않게 두어 데드락 여지를 없앤다.
    {
        std::lock_guard<std::mutex> guard(m_materialMutex);
        if (Materials.find(materialName) != Materials.end())
        {
            Debug->Log("MaterialLoader::LoadMaterial : Material already loaded");
            return Materials[materialName].get();
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
    auto material = std::make_shared<Material>();
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

    {
        std::lock_guard<std::mutex> guard(m_materialMutex);
        // 로딩 중 다른 스레드가 같은 머티리얼을 먼저 넣었을 수 있다.
        // 그 경우 맵에 있는 쪽을 반환해 인스턴스가 갈라지지 않게 한다.
        auto& slot = Materials[material->m_name];
        if (!slot)
        {
            slot = material;
        }
        return slot.get();
    }
}

// I5-D5c1 — base 재질 자산의 저작 원본. legacy 캐시(Materials)와 별개로
// 자산 GUID로 캐시한다 — 씬 로드가 renderer마다 같은 base를 다시 파싱하지
// 않게. 캐시 키는 GUID다(파일 stem이 아니라): ref 표기의 정본 주소가 GUID다.
std::shared_ptr<const experiment::Material> DataSystem::LoadAuthoredMaterialShared(
	FileGuid assetGuid)
{
	if (FileGuid{} == assetGuid) return nullptr;
	{
		std::lock_guard<std::mutex> guard(m_authoredMaterialMutex);
		const auto it = m_authoredMaterials.find(assetGuid);
		if (it != m_authoredMaterials.end()) return it->second;
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
	auto authored = std::make_shared<experiment::Material>();
	if (!DeserializeMaterialPayload(discardedLegacy,
		Authoring::NodeViewAccess::Make(node), authored.get()))
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

	std::lock_guard<std::mutex> guard(m_authoredMaterialMutex);
	auto& slot = m_authoredMaterials[assetGuid];
	if (!slot) slot = std::move(authored);
	return slot;
}

std::shared_ptr<Material> DataSystem::LoadMaterialShared(std::string_view name)
{
    // LoadMaterial이 로딩·캐시 삽입을 모두 처리하므로 그대로 태운 뒤,
    // 맵에서 shared_ptr을 꺼내 돌려준다(참조 카운트를 증가시켜 공동 소유).
    Material* loaded = LoadMaterial(name);
    if (nullptr == loaded)
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> guard(m_materialMutex);
    auto it = Materials.find(loaded->m_name);
    return (it != Materials.end()) ? it->second : nullptr;
}

Texture* DataSystem::LoadTextureGUID(FileGuid guid)
{
	if (!m_assetMetaRegistry) return nullptr;
	const file::path texturePath = ResolveCatalogAssetPath(guid);
	return texturePath.empty() ? nullptr : LoadTexture(texturePath.string());
}

Texture* DataSystem::LoadTexture(std::string_view filePath, TextureFileType type)
{
	return LoadSharedTexture(filePath, type).get();
}

std::shared_ptr<Texture> DataSystem::LoadSharedTexture(std::string_view filePath, TextureFileType type)
{
	std::string_view fallbackDirectory;
	
	switch (type)
	{
	case DataSystem::TextureFileType::Texture:
		fallbackDirectory = "Textures\\";
		break;
	case DataSystem::TextureFileType::MaterialTexture:
		fallbackDirectory = "Materials\\";
		break;
	case DataSystem::TextureFileType::TerrainTexture:
		fallbackDirectory = "Terrain\\Texture\\";
		break;
	case DataSystem::TextureFileType::HDR:
		fallbackDirectory = "HDR\\";
		break;
	case DataSystem::TextureFileType::UITexture:
		fallbackDirectory = "UI\\";
		break;
	case DataSystem::TextureFileType::SpriteSheet:
		fallbackDirectory = "SpriteSheets\\";
		break;
	default:
		break;
	}

	const file::path assetPath = ResolveRuntimeAssetPath(filePath, fallbackDirectory);
	std::string name = assetPath.stem().string();

	// 캐시 조회와 삽입만 락으로 감싼다.
	// 이 함수는 LoadAssetBundle이 스레드풀로 병렬 호출하는데 예전에는 무잠금이라
	// 동시 삽입 시 unordered_map 리해시와 겹쳐 힙이 손상될 수 있었다.
	// 디스크 로딩은 오래 걸리므로 락 밖에서 수행한다(같은 파일을 두 번 읽는
	// 낭비는 있을 수 있으나 삽입 시 정리되며, 정확성에는 문제가 없다).
	{
		std::lock_guard<std::mutex> guard(m_textureMutex);
		if (Textures.find(name) != Textures.end())
		{
			Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
			return Textures[name];
		}
	}

	std::shared_ptr<Texture> texture = Texture::LoadSharedFromPath(assetPath.string());
	if (texture)
	{
		{
			std::lock_guard<std::mutex> guard(m_textureMutex);
			switch (type)
			{
			case DataSystem::TextureFileType::Texture:
				Textures[name] = texture;
				break;
			case DataSystem::TextureFileType::UITexture:
				UITextures[name] = texture;
				break;
			case DataSystem::TextureFileType::SpriteSheet:
				SpriteSheets[name] = texture;
				break;
			default:
				break;
			}
		}
		texture->m_name = name;
		texture->m_extension = assetPath.extension().string();

		return texture;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}

	return nullptr;
}

Texture* DataSystem::LoadMaterialTexture(std::string_view filePath, bool isCompress)
{
    const file::path destination = ResolveRuntimeAssetPath(filePath, "Materials\\");

    std::string name = file::path(filePath).stem().string();
	{
		std::unique_lock lock(m_textureMutex);
		if (Textures.find(name) != Textures.end())
		{
			Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
			return Textures[name].get();
		}
	}

    auto texture = Texture::LoadSharedFromPath(destination.string(), isCompress);
    if (texture)
    {
		{
			std::unique_lock lock(m_textureMutex);
			Textures[name] = texture;
		}
        return texture.get();
    }
    else
    {
        Debug->LogError("ModelLoader::LoadModel : Model file not found");
    }

    return nullptr;
}

std::shared_ptr<Texture> DataSystem::LoadSharedMaterialTexture(std::string_view filePath, bool isCompress)
{
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
	std::string key = file::path(destination).stem().string();

	// 1차 조회 (락 짧게)
	{
		std::unique_lock lock(m_textureMutex);
		if (auto it = Textures.find(key); it != Textures.end())
			return it->second; // shared_ptr 복사로 참조 증가
	}

	// 로드 (락 없이 I/O)
	auto loaded = Texture::LoadSharedFromPath(destination.string(), isCompress);
	if (!loaded)
	{
		Debug->LogError("TextureLoader::LoadTexture : file not found");
		return nullptr;
	}

	// 삽입 단계: 이미 다른 스레드가 넣었을 수 있으니 '덮어쓰지 말고' 기존 걸 사용
	{
		std::unique_lock lock(m_textureMutex);
		auto [it, inserted] = Textures.emplace(key, loaded);
		if (!inserted)
		{
			// 누군가 먼저 넣은 경우: 그걸 사용 (중복 로드였지만 dangling 방지)
			return it->second;
		}
	}

	return loaded;
}

Material* DataSystem::CreateMaterial()
{
	std::shared_ptr<Material> material = std::make_shared<Material>();
	if (material)
	{
		std::lock_guard<std::mutex> guard(m_materialMutex);
		std::string name = "NewMaterial";
		int index = 1;
		while (Materials.find(name) != Materials.end())
		{
			name = "NewMaterial" + std::to_string(index++);
		}
		material->m_name = name;
		material->m_fileGuid = FileGuid::CreateRandomV4();
		
		Materials[name] = material;
		
		return material.get();
	}
	return nullptr;
}

// ★ LoadSFont(DirectXTK SpriteFont)를 걷었다 (D4, 2026-08-09).
//   ID3D11Device로 폰트를 만들던 유일한 자리였고, 그 결과를 그리는 쪽은
//   T6에서 사라졌다. 게다가 Assets/Font/ 가 비어 있어 읽을 자산도 없었다.
//   폰트는 SDF 계통으로 새로 세운다.

// ★ 콘텐츠 브라우저 UI 전체가 여기 있었다 (PHASE 4-3 슬라이스 2).
//   창 등록과 그리기 함수 일곱, 그리고 그 상태(현재 폴더·검색 필터·
//   선택 메타)가 EngineGUIWindow/ContentsBrowserWindow로 옮겨 갔다.
//   자산 시스템은 캐시와 아이콘·폰트를 가질 뿐, 이제 그리지 않는다.

FileGuid DataSystem::GetFileGuid(const file::path& filepath) const
{
	return m_assetMetaRegistry ? m_assetMetaRegistry->GetGuid(filepath) : FileGuid{};
}

ShaderMetaHandle DataSystem::LoadShaderMetaHandle(FileGuid guid,
	std::string& outError)
{
	if (!m_assetMetaRegistry || FileGuid{} == guid)
	{
		outError = "ShaderMeta catalog GUID가 비었거나 catalog가 초기화되지 않았다";
		return {};
	}

	std::lock_guard lock(m_shaderMetaMutex);
	const auto cached = m_shaderMetaSlotByGuid.find(guid);
	if (cached != m_shaderMetaSlotByGuid.end())
	{
		ShaderMetaCacheSlot& slot = m_shaderMetaSlots[cached->second];
		if (slot.occupied && slot.value)
		{
			outError.clear();
			return { cached->second + 1, slot.generation };
		}
	}

	const file::path sourcePath = GetFilePath(guid);
	if (sourcePath.empty())
	{
		outError = "ShaderMeta source identity를 찾지 못했다: " + guid.ToString();
		return {};
	}
	const file::path path = ResolveCatalogAssetPath(guid);
	if (path.empty())
	{
		outError = "ShaderMeta runtime 경로를 찾지 못했다: " + guid.ToString();
		return {};
	}

	ShaderMeta loaded;
	if (!ShaderMetaLoader::LoadFile(path, sourcePath, guid, loaded, outError)) return {};
	std::printf("[shadermeta.document] source=%s guid=%s\n",
		path.lexically_normal() != sourcePath.lexically_normal()
			? "cooked" : "authoring",
		guid.ToString().c_str());

	ShaderMetaPermutationStats stats;
	if (!ShaderPermutationDomain::Measure(loaded, stats, outError)) return {};

	Debug->Log("ShaderMeta loaded: " + loaded.name + " [" + guid.ToString()
		+ "] variants/pass=" + std::to_string(stats.variantsPerPass)
		+ ", compile requests=" + std::to_string(stats.compileRequests));
	if (stats.compileRequests
		> ShaderPermutationDomain::kDefaultBuildCompileLimit)
	{
		Debug->LogWarning("ShaderMeta Build permutation 상한 초과: "
			+ loaded.name + " requests=" + std::to_string(stats.compileRequests)
			+ ", limit=" + std::to_string(
				ShaderPermutationDomain::kDefaultBuildCompileLimit));
	}

	std::uint32_t slotIndex{};
	if (cached != m_shaderMetaSlotByGuid.end())
	{
		slotIndex = cached->second;
	}
	else if (!m_shaderMetaFreeSlots.empty())
	{
		slotIndex = m_shaderMetaFreeSlots.back();
		m_shaderMetaFreeSlots.pop_back();
	}
	else
	{
		slotIndex = static_cast<std::uint32_t>(m_shaderMetaSlots.size());
		m_shaderMetaSlots.emplace_back();
	}

	ShaderMetaCacheSlot& slot = m_shaderMetaSlots[slotIndex];
	slot.guid = guid;
	slot.occupied = true;
	slot.value = std::make_shared<const ShaderMeta>(std::move(loaded));
	m_shaderMetaSlotByGuid[guid] = slotIndex;
	outError.clear();
	return { slotIndex + 1, slot.generation };
}

std::shared_ptr<const ShaderMeta> DataSystem::ResolveShaderMeta(
	ShaderMetaHandle handle) const
{
	if (!handle.IsValid()) return {};
	std::lock_guard lock(m_shaderMetaMutex);
	const std::uint32_t slotIndex = handle.slot - 1;
	if (slotIndex >= m_shaderMetaSlots.size()) return {};
	const ShaderMetaCacheSlot& slot = m_shaderMetaSlots[slotIndex];
	if (!slot.occupied || slot.generation != handle.generation) return {};
	return slot.value;
}

bool DataSystem::LoadShaderMetaGUID(FileGuid guid, ShaderMeta& outMeta,
	std::string& outError)
{
	const ShaderMetaHandle handle = LoadShaderMetaHandle(guid, outError);
	const std::shared_ptr<const ShaderMeta> meta = ResolveShaderMeta(handle);
	if (!meta)
	{
		if (outError.empty()) outError = "ShaderMeta cache handle을 resolve하지 못했다";
		return false;
	}
	outMeta = *meta;
	outError.clear();
	return true;
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

void DataSystem::ApplyAssetChange(const RuntimeAssetChange& change)
{
	if (!m_assetMetaRegistry || change.path.empty()) return;

	RuntimeAssetType assetType = change.assetType;
	if (RuntimeAssetType::Auto == assetType)
		assetType = ResolveRuntimeAssetType(change.path);

	switch (change.kind)
	{
	case RuntimeAssetChangeKind::CatalogUpsert:
		if (change.guid != FileGuid{})
			RegisterAssetMeta(*m_assetMetaRegistry, change.guid, change.path);
		break;
	case RuntimeAssetChangeKind::ContentReload:
		// 파일 게시는 이미 끝났다. 먼저 이전 generation을 cache lookup에서
		// 분리한 뒤 catalog를 갱신해, 이 호출 이후의 load가 새 파일을 읽게 한다.
		RetireCachedAsset(assetType, change.path, change.guid, false);
		if (change.guid != FileGuid{})
			RegisterAssetMeta(*m_assetMetaRegistry, change.guid, change.path);
		break;
	case RuntimeAssetChangeKind::Removed:
		RetireCachedAsset(assetType, change.path, change.guid, true);
		m_assetMetaRegistry->Unregister(change.path);
		break;
	}
}

void DataSystem::RetireCachedAsset(RuntimeAssetType assetType,
	const file::path& path, FileGuid guid, bool remove)
{
	if (RuntimeAssetType::Auto == assetType)
		assetType = ResolveRuntimeAssetType(path);
	if (RuntimeAssetType::CatalogOnly == assetType) return;
	if (RuntimeAssetType::ShaderMeta == assetType)
	{
		if (FileGuid{} == guid && m_assetMetaRegistry)
			guid = m_assetMetaRegistry->GetGuid(path);
		InvalidateShaderMeta(guid, remove);
		return;
	}

	const std::string key = path.stem().string();
	auto detach = [&key](auto& cache, std::mutex& cacheMutex)
	{
		typename std::decay_t<decltype(cache)>::mapped_type generation;
		{
			std::lock_guard lock(cacheMutex);
			const auto iterator = cache.find(key);
			if (iterator == cache.end()) return generation;
			generation = std::move(iterator->second);
			cache.erase(iterator);
		}
		return generation;
	};
	const auto detachForType = [this, &detach](RuntimeAssetType type,
		auto& cache, std::mutex& cacheMutex)
	{
		auto generation = detach(cache, cacheMutex);
		if (!generation || !RequiresLegacyRetiredGeneration(type)) return;
		std::lock_guard lock(m_retiredTextureMutex);
		m_retiredTextureGenerations.emplace_back(std::move(generation));
	};

	switch (assetType)
	{
	case RuntimeAssetType::Model:
		detachForType(assetType, Models, m_modelMutex);
		break;
	case RuntimeAssetType::Material:
		detachForType(assetType, Materials, m_materialMutex);
		break;
	case RuntimeAssetType::Texture:
		detachForType(assetType, Textures, m_textureMutex);
		break;
	case RuntimeAssetType::UITexture:
		detachForType(assetType, UITextures, m_textureMutex);
		break;
	case RuntimeAssetType::SpriteSheet:
		detachForType(assetType, SpriteSheets, m_textureMutex);
		break;
	default:
		break;
	}
}

void DataSystem::InvalidateShaderMeta(FileGuid guid, bool remove)
{
	if (FileGuid{} == guid) return;
	std::lock_guard lock(m_shaderMetaMutex);
	const auto found = m_shaderMetaSlotByGuid.find(guid);
	if (found == m_shaderMetaSlotByGuid.end()) return;

	ShaderMetaCacheSlot& slot = m_shaderMetaSlots[found->second];
	slot.value.reset();
	if (++slot.generation == 0) ++slot.generation;
	if (!remove) return;

	slot.guid = {};
	slot.occupied = false;
	m_shaderMetaFreeSlots.push_back(found->second);
	m_shaderMetaSlotByGuid.erase(found);
}

FileGuid DataSystem::GetFilenameToGuid(const std::string& filename) const
{
	return m_assetMetaRegistry
		? m_assetMetaRegistry->GetFilenameToGuid(filename) : FileGuid{};
}

FileGuid DataSystem::GetStemToGuid(const std::string& stem) const
{
	return m_assetMetaRegistry ? m_assetMetaRegistry->GetStemToGuid(stem) : FileGuid{};
}

file::path DataSystem::GetFilePath(FileGuid fileguid) const
{
	return m_assetMetaRegistry ? m_assetMetaRegistry->GetPath(fileguid) : file::path{};
}

void DataSystem::AddModel(const file::path& filepath, const file::path& dir)
{
	std::string name = file::path(filepath.filename()).replace_extension().string();

	if(Models[name])
	{
		return;
	}

	std::shared_ptr<Model> model;
	if (model)
	{
		Models[model->name] = model;
	}
}

void DataSystem::LoadAssetBundle(const AssetBundle& bundle)
{
	for (const auto& entry : bundle.assets)
	{
		auto type = static_cast<ManagedAssetType>(entry.assetTypeID);
		file::path name = entry.assetName;

		WorkerPools->Enqueue([this, type, name]
		{
			switch (type)
			{
			case ManagedAssetType::Model:
				LoadModel(name.string());
				break;
			case ManagedAssetType::Material:
				LoadMaterial(name.string());
				break;
			case ManagedAssetType::Texture:
				LoadTexture(name.string());
				break;
			case ManagedAssetType::SpriteFont:
				// 폰트 로딩은 D4에서 은퇴했다(DX11 SpriteFont). 번들에 옛
				// 항목이 남아 있어도 조용히 건너뛴다 - SDF 계통이 서면
				// 그때 새 타입으로 받는다.
				break;
			default:
				break;
			}
		});
	}

	WorkerPools->NotifyAllAndWait();
}

void DataSystem::RetainAssets(const AssetBundle& bundle)
{
	for (const auto& entry : bundle.assets)
	{
		file::path name = entry.assetName;
		m_retainedAssets[entry.assetTypeID].insert(name.stem().string());
	}
}

void DataSystem::ClearRetainedAssets()
{
	m_retainedAssets.clear();
}

void DataSystem::UnloadUnusedAssets()
{
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
				it = container.erase(it);
			}
			else
			{
				++it;
			}
		}
	};

	removeUnused(Models,       static_cast<int>(ManagedAssetType::Model),       m_modelMutex);
	removeUnused(Materials,    static_cast<int>(ManagedAssetType::Material),    m_materialMutex);
	removeUnused(Textures,     static_cast<int>(ManagedAssetType::Texture),     m_textureMutex);
	// 예전에는 이 두 캐시가 ManagedAssetType에 없어 언로드 대상에서 아예 빠져 있었다(12.2-②).
	removeUnused(UITextures,   static_cast<int>(ManagedAssetType::UITexture),   m_textureMutex);
	removeUnused(SpriteSheets, static_cast<int>(ManagedAssetType::SpriteSheet), m_textureMutex);
}
