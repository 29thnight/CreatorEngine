#include "DataSystem.h"
#include "Model.h"	
#include <future>
#include <ppltasks.h>
#include <ppl.h>
#include <yaml-cpp/yaml.h>
#include "Benchmark.hpp"
// SceneManager.h가 여기 있었다. LoadAssetBundle이 씬 매니저가 들고 있던
// 스레드풀을 빌려 쓰느라 층 3이 층 4를 올려다봤다. 풀의 소유를 층 1로
// 내리면서(WorkerPool.h) 그 이유가 사라졌다 — PHASE 4-3 슬라이스 3.
#include "WorkerPool.h"
// Meta::Serialize / Deserialize. SceneManager.h가 ReflectionYml.h를 대신
// 끌어와 주던 자리다 — 빌려 쓰던 것을 직접 든다.
#include "ReflectionYml.h"
#include "ShaderMeta.h"
#include "ShaderPermutationDomain.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>

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
	constexpr std::uint16_t kMaterialPayloadVersion = 1;
	constexpr std::uint16_t kMaterialPayloadYamlEncoding = 1;
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
}

DataSystem::~DataSystem()
{
	Finalize();
}

void DataSystem::Initialize()
{
	m_assetMetaRegistry = std::make_shared<AssetMetaRegistry>();
	LoadAssetCatalog(PathFinder::Relative());
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
		std::lock_guard lock(m_retiredAssetMutex);
		m_retiredAssetGenerations.clear();
	}
	{
		std::lock_guard lock(m_shaderMetaMutex);
		m_shaderMetaSlotByGuid.clear();
		m_shaderMetaSlots.clear();
		m_shaderMetaFreeSlots.clear();
	}

	m_assetMetaRegistry.reset();
}

void DataSystem::LoadAssetCatalog(const file::path& root)
{
	if (!file::exists(root)) return;

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
				try
				{
					const YAML::Node node = YAML::LoadFile(entry.path().string());
					if (node["guid"] && node["guid"].IsScalar())
					{
						const FileGuid guid(node["guid"].as<std::string>());
						if (guid != FileGuid{})
							m_assetMetaRegistry->Register(guid, targetPath);
					}
				}
				catch (const std::exception& exception)
				{
					Debug->LogWarning("Asset catalog ignored invalid meta: " +
						entry.path().string() + " (" + exception.what() + ")");
				}
			}
		}

		error.clear();
		iterator.increment(error);
	}
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

	std::shared_ptr<Model> model = Model::LoadModelShared(assetPath.string());
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

Model* DataSystem::LoadCashedModel(std::string_view filePath)
{
	const file::path assetPath = ResolveRuntimeAssetPath(filePath, "Models\\");
	std::string name = assetPath.stem().string();
	{
		std::unique_lock lock(m_modelMutex);
		if (Models.find(name) != Models.end() && Models[name].get() != nullptr)
		{
			Debug->Log("ModelLoader::LoadModel : Model already loaded");
			return Models[name].get();
		}
	}

	std::shared_ptr<Model> model{};
    try
    {
		std::string modelPath = assetPath.string();
        model = Model::LoadModelShared(modelPath);
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return nullptr;
    }

	if (model)
	{
		{
			std::unique_lock lock(m_modelMutex);
			Models[name] = model;
		}
		return model.get();
	}

	return nullptr;
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

	synchronize("baseColorMap", material.m_baseColorTexName, material.m_pBaseColor);
	synchronize("normalMap", material.m_normalTexName, material.m_pNormal);
	synchronize("ormMap", material.m_ORM_TexName, material.m_pOccRoughMetal);
	synchronize("aoMap", material.m_AO_TexName, material.m_AOMap);
	synchronize("emissiveMap", material.m_EmissiveTexName, material.m_pEmissive);
}

YAML::Node DataSystem::SerializeMaterialPayload(Material& material) const
{
	SynchronizeLegacyMaterialProperties(material);
	YAML::Node node = Meta::Serialize(&material);
	if (material.m_cbufferValues.empty()) return node;

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

	YAML::Node buffers(YAML::NodeType::Sequence);
	for (const std::string_view name : names)
	{
		const auto& data = material.m_cbufferValues.at(std::string(name));
		YAML::Node entry;
		entry["name"] = std::string(name);
		entry["data"] = YAML::Binary(data.data(), data.size());
		buffers.push_back(entry);
	}
	node["constant_buffers"] = buffers;
	return node;
}

bool DataSystem::DeserializeMaterialPayload(Material& material, const YAML::Node& node)
{
	if (!node || !node.IsMap()) return false;

	try
	{
		Meta::Deserialize(&material, node);
		material.m_cbufferValues.clear();
		if (const YAML::Node buffers = node["constant_buffers"])
		{
			if (!buffers.IsSequence()) return false;
			for (const YAML::Node& entry : buffers)
			{
				if (!entry.IsMap() || !entry["name"] || !entry["data"])
					return false;
				std::string name = entry["name"].as<std::string>();
				if (name.empty() || material.m_cbufferValues.contains(name))
					return false;
				const YAML::Binary binary = entry["data"].as<YAML::Binary>();
				material.m_cbufferValues.emplace(std::move(name),
					std::vector<std::uint8_t>(binary.data(), binary.data() + binary.size()));
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
	std::ostringstream yaml;
	yaml << SerializeMaterialPayload(material);
	const std::string payload = yaml.str();
	if (payload.size() > kMaxMaterialPayloadBytes
		|| payload.size() > std::numeric_limits<std::uint32_t>::max())
	{
		return false;
	}

	output.write(kMaterialPayloadMagic.data(), kMaterialPayloadMagic.size());
	WriteU16(output, kMaterialPayloadVersion);
	WriteU16(output, kMaterialPayloadYamlEncoding);
	WriteU32(output, static_cast<std::uint32_t>(payload.size()));
	output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
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
		|| encoding != kMaterialPayloadYamlEncoding
		|| payloadSize > kMaxMaterialPayloadBytes)
	{
		return false;
	}

	std::string payload(payloadSize, '\0');
	if (payloadSize != 0)
		input.read(payload.data(), static_cast<std::streamsize>(payload.size()));
	if (!input) return false;

	try
	{
		return DeserializeMaterialPayload(material, YAML::Load(payload));
	}
	catch (const std::exception& exception)
	{
		Debug->LogError("Material binary payload decode failed: "
			+ std::string(exception.what()));
		return false;
	}
}

void DataSystem::FinalizeMaterialRuntime(Material& material)
{
	// 디스크/scene 논리 값이 바뀌면 기존 schema가 가리키는 applied generation도
	// 더는 유효한 runtime 상태가 아니다. legacy CB bytes는 Configure에서 새 layout에
	// repack할 입력이므로 ResetShaderRuntime은 그것을 보존한다.
	material.ResetShaderRuntime();
	if (0.04f > material.m_materialInfo.m_IOR || 4.f < material.m_materialInfo.m_IOR)
		material.m_materialInfo.m_IOR = 1.5f;

	// property GUID가 저장 정본이다. decode 대상에 남아 있을 수 있는 runtime raw
	// pointer를 먼저 버려, 낡은 포인터 이름이 새 GUID를 역으로 덮지 못하게 한다.
	material.m_pBaseColor = nullptr;
	material.m_pNormal = nullptr;
	material.m_pOccRoughMetal = nullptr;
	material.m_AOMap = nullptr;
	material.m_pEmissive = nullptr;
	SynchronizeLegacyMaterialProperties(material);
	auto loadTexture = [this, &material](std::string_view property,
		const std::string& name, Texture*& destination, bool compress)
	{
		const auto value = std::find_if(material.m_propertyValues.begin(),
			material.m_propertyValues.end(), [property](const MaterialPropertyValue& candidate)
			{
				return candidate.m_name == property;
			});
		if (value != material.m_propertyValues.end()
			&& value->m_textureGuid != FileGuid{})
		{
			const file::path path = GetFilePath(value->m_textureGuid);
			if (!path.empty()) destination = LoadMaterialTexture(path.string(), compress);
		}
		if (!destination && !name.empty())
			destination = LoadMaterialTexture(name, compress);
	};

	loadTexture("baseColorMap", material.m_baseColorTexName, material.m_pBaseColor, true);
	loadTexture("normalMap", material.m_normalTexName, material.m_pNormal, false);
	loadTexture("ormMap", material.m_ORM_TexName, material.m_pOccRoughMetal, false);
	loadTexture("aoMap", material.m_AO_TexName, material.m_AOMap, false);
	loadTexture("emissiveMap", material.m_EmissiveTexName, material.m_pEmissive, false);

	material.m_materialInfo.m_useBaseColor = nullptr != material.m_pBaseColor;
	material.m_materialInfo.m_useOccRoughMetal = nullptr != material.m_pOccRoughMetal;
	material.m_materialInfo.m_useAOMap = nullptr != material.m_AOMap;
	material.m_materialInfo.m_useEmissive = nullptr != material.m_pEmissive;
	if (!material.m_pNormal)
		material.m_materialInfo.m_useNormalMap = 0;
	else if (0 == material.m_materialInfo.m_useNormalMap)
		material.m_materialInfo.m_useNormalMap = USE_NORMAL_MAP;
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
    file::path loadPath = PathFinder::Relative("Materials\\") / (materialName + ".asset");
    if (!file::exists(loadPath))
    {
		return nullptr;
    }

    MetaYml::Node node = MetaYml::LoadFile(loadPath.string());
    auto material = std::make_shared<Material>();
    if (!DeserializeMaterialPayload(*material, node)) return nullptr;
    // 파일 stem이 cache key의 정본이다. 내부 m_name이 낡았거나 비어 있어도
    // LoadMaterialShared(name)가 같은 세대를 찾도록 게시 직전에 맞춘다.
    material->m_name = materialName;

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
	const file::path texturePath = m_assetMetaRegistry->GetPath(guid);
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
	file::path destination = PathFinder::Relative("Materials\\") / file::path(filePath).filename();
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
		material->m_fileGuid = make_file_guid(name);
		
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

	const file::path path = m_assetMetaRegistry->GetPath(guid);
	if (path.empty())
	{
		outError = "ShaderMeta catalog GUID 경로를 찾지 못했다: " + guid.ToString();
		return {};
	}

	ShaderMeta loaded;
	if (!ShaderMetaLoader::LoadFile(path, guid, loaded, outError)) return {};

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
			m_assetMetaRegistry->Register(change.guid, change.path);
		break;
	case RuntimeAssetChangeKind::ContentReload:
		// 파일 게시는 이미 끝났다. 먼저 이전 generation을 cache lookup에서
		// 분리한 뒤 catalog를 갱신해, 이 호출 이후의 load가 새 파일을 읽게 한다.
		RetireCachedAsset(assetType, change.path, change.guid, false);
		if (change.guid != FileGuid{})
			m_assetMetaRegistry->Register(change.guid, change.path);
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
	auto retire = [this, &key](auto& cache, std::mutex& cacheMutex)
	{
		typename std::decay_t<decltype(cache)>::mapped_type generation;
		{
			std::lock_guard lock(cacheMutex);
			const auto iterator = cache.find(key);
			if (iterator == cache.end()) return;
			generation = std::move(iterator->second);
			cache.erase(iterator);
		}

		if (generation)
		{
			std::lock_guard lock(m_retiredAssetMutex);
			m_retiredAssetGenerations.emplace_back(std::move(generation));
		}
	};

	switch (assetType)
	{
	case RuntimeAssetType::Model:
		retire(Models, m_modelMutex);
		break;
	case RuntimeAssetType::Material:
		retire(Materials, m_materialMutex);
		break;
	case RuntimeAssetType::Texture:
		retire(Textures, m_textureMutex);
		break;
	case RuntimeAssetType::UITexture:
		retire(UITextures, m_textureMutex);
		break;
	case RuntimeAssetType::SpriteSheet:
		retire(SpriteSheets, m_textureMutex);
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
