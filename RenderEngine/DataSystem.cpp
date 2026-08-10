#include "DataSystem.h"
#include "EditorImGuiTexture.h"
#include "ShaderSystem.h"
#include "Model.h"	
#include <future>
#include <shellapi.h>
#include <ppltasks.h>
#include <ppl.h>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include "FileIO.h"
#include "VolumeProfile.h"
#include "Benchmark.hpp"
// SceneManager.h가 여기 있었다. LoadAssetBundle이 씬 매니저가 들고 있던
// 스레드풀을 빌려 쓰느라 층 3이 층 4를 올려다봤다. 풀의 소유를 층 1로
// 내리면서(WorkerPool.h) 그 이유가 사라졌다 — PHASE 4-3 슬라이스 3.
#include "WorkerPool.h"
// Meta::Serialize / Deserialize. SceneManager.h가 ReflectionYml.h를 대신
// 끌어와 주던 자리다 — 빌려 쓰던 것을 직접 든다.
#include "ReflectionYml.h"
#include "FileDialog.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ToggleUI.h"

// 검색 함수
std::atomic_bool DataSystem::m_isExecuteSolution = false;

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

DataSystem::~DataSystem()
{
#ifndef BUILD_FLAG
	Finalize();
#endif
}

void DataSystem::Initialize()
{
#ifndef BUILD_FLAG
	file::path iconpath		= PathFinder::IconPath();
	UnknownIcon				= Texture::LoadFormPath(iconpath.string() + "Unknown.png");
	TextureIcon				= Texture::LoadFormPath(iconpath.string() + "Texture.png");
	ModelIcon				= Texture::LoadFormPath(iconpath.string() + "Model.png");
	AssetsIcon				= Texture::LoadFormPath(iconpath.string() + "Assets.png");
	FolderIcon				= Texture::LoadFormPath(iconpath.string() + "Folder.png");
	ShaderIcon				= Texture::LoadFormPath(iconpath.string() + "Shader.png");
	CodeIcon				= Texture::LoadFormPath(iconpath.string() + "Code.png");
	MainLightIcon			= Texture::LoadFormPath(iconpath.string() + "MainLightGizmo.png");
	PointLightIcon			= Texture::LoadFormPath(iconpath.string() + "PointLightGizmo.png");
	SpotLightIcon			= Texture::LoadFormPath(iconpath.string() + "SpotLightGizmo.png");
	DirectionalLightIcon	= Texture::LoadFormPath(iconpath.string() + "DirectionalLightGizmo.png");
	CameraIcon				= Texture::LoadFormPath(iconpath.string() + "CameraGizmo.png");
	smallFont				= ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 12.0f);
	extraSmallFont			= ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 10.0f);

	kExtensionMap =
	{
		{ ".fbx",	 { FileType::Model,			(ImTextureID)EditorImGuiTexture::From(ModelIcon) }	},
		{ ".gltf",   { FileType::Model,			(ImTextureID)EditorImGuiTexture::From(ModelIcon) }	},
		{ ".obj",    { FileType::Model,			(ImTextureID)EditorImGuiTexture::From(ModelIcon) }	},
		{ ".glb",    { FileType::Model,			(ImTextureID)EditorImGuiTexture::From(ModelIcon) }	},
		{ ".png",    { FileType::Texture,		(ImTextureID)EditorImGuiTexture::From(TextureIcon) }	},
		{ ".dds",    { FileType::Texture,		(ImTextureID)EditorImGuiTexture::From(TextureIcon) }	},
		{ ".hdr",    { FileType::HDR,			(ImTextureID)EditorImGuiTexture::From(TextureIcon) }	},
		{ ".hlsl",   { FileType::Shader,		(ImTextureID)EditorImGuiTexture::From(ShaderIcon) }	},
		{ ".shader", { FileType::Shader,		(ImTextureID)EditorImGuiTexture::From(ShaderIcon) }	},
		{ ".cpp",    { FileType::CppScript,		(ImTextureID)EditorImGuiTexture::From(CodeIcon) }		},
		{ ".cs",     { FileType::CSharpScript,	(ImTextureID)EditorImGuiTexture::From(CodeIcon) }		},
		{ ".wav",    { FileType::Sound,			(ImTextureID)EditorImGuiTexture::From(UnknownIcon) }	},
		{ ".mp3",    { FileType::Sound,			(ImTextureID)EditorImGuiTexture::From(UnknownIcon) }	},
		{ ".terrain",{ FileType::TerrainTexture, (ImTextureID)EditorImGuiTexture::From(TextureIcon) } },
		{ ".prefab", { FileType::Prefab,		(ImTextureID)EditorImGuiTexture::From(AssetsIcon) }	},
		{ ".volume", { FileType::VolumeProfile,	(ImTextureID)EditorImGuiTexture::From(AssetsIcon) }	},
		{ ".spritefont",{ FileType::Font,		(ImTextureID)EditorImGuiTexture::From(AssetsIcon) }   }
	};

	RenderForEditer();
#endif
	m_watcher			= new efsw::FileWatcher();
	m_assetMetaRegistry = std::make_shared<AssetMetaRegistry>();
	m_assetMetaWatcher	= std::make_shared<AssetMetaWatcher>(m_assetMetaRegistry.get());
	m_assetMetaWatcher->ScanAndGenerateMissingMeta(PathFinder::Relative());
#ifndef BUILD_FLAG
	m_assetMetaWatcher->ScanAndCleanupInvalidMeta(PathFinder::Relative());
#endif
	m_watcher->addWatch(PathFinder::Relative().string(), m_assetMetaWatcher.get(), true);
	m_watcher->watch();
}

void DataSystem::Finalize()
{
#ifndef BUILD_FLAG
    delete UnknownIcon;
    delete TextureIcon;
    delete ModelIcon;
    delete AssetsIcon;
    delete FolderIcon;
    delete ShaderIcon;
    delete CodeIcon;
	delete MainLightIcon;
	delete PointLightIcon;
	delete SpotLightIcon;
	delete DirectionalLightIcon;
	delete CameraIcon;
#endif // !BUILD_FLAG

    Models.clear();
    Textures.clear();
    Materials.clear();

	delete m_watcher;
}

void DataSystem::RenderForEditer()
{
#ifndef BUILD_FLAG
	ImGui::ContextRegister("SelectMatarial", true, [&]()
	{
		static ImGuiTextFilter searchFilter;
		float availableWidth = ImGui::GetContentRegionAvail().x;

		// 드래그앤드롭 대상으로 넘길 것이므로 공동 소유로 붙든다.
		static std::shared_ptr<Material> select_material = nullptr;

		const float tileWidth = 100.f;

		int tileColumns = (int)(availableWidth / tileWidth);
		tileColumns = (tileColumns > 0) ? tileColumns : 1;

		searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);

		ImTextureID iconTexture = (ImTextureID)EditorImGuiTexture::From(ModelIcon);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
		if (ImGui::BeginChild("DirectoryHierarchy", ImVec2(0, 300), ImGuiChildFlags_AlwaysUseWindowPadding, 0))
		{
			const float tileSize = 100.0f;
			float avail = ImGui::GetContentRegionAvail().x;
			int columns = (int)(avail / tileSize);
			if (columns < 1) columns = 1;

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
			int count = 0;

			for (auto& [name, Material] : Materials)
			{
				if (!searchFilter.PassFilter(name.c_str()))
					continue;

				if (count % columns != 0)
					ImGui::SameLine();

				ImGui::BeginGroup();

				if (name.empty())
				{
					const_cast<std::string&>(name) = "None";
				}

				if (ImGui::ImageButton(name.c_str(), iconTexture, ImVec2(70, 70)))
				{
					if (ImGui::IsItemHovered())
					{
						select_material = Material;
					}
				}

				ImGui::PushID(name.c_str());
				ImGui::Button(name.c_str(), ImVec2(80, 30));
				ImGui::PopID();
				ImGui::EndGroup();

				if (nullptr != select_material && 
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_trasfarMaterial = select_material;
					ImGui::GetContext("SelectMatarial").Close();
				}

				count++;
			}

			ImGui::PopStyleVar();
		}
		ImGui::EndChild();

		ImGui::BeginChild("FileList", ImVec2(0, 50), false);
		if (nullptr != select_material)
		{
			ImGui::Text(select_material->m_name.c_str());
			ImGui::Text(select_material->m_fileGuid.ToString().c_str());
		}
		else
		{
			ImGui::Text("No Material Selected");
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

	}, ImGuiWindowFlags_NoScrollbar);
	ImGui::GetContext("SelectMatarial").Close();

	ImGui::ContextRegister("TextureType Selector", true, [&]()
	{
		static std::vector<file::path> texturePaths;

		while (!m_LoadTextureAssetQueue.empty())
		{
			if(m_LoadTextureAssetQueue.try_pop(m_TargetTexturePath))
			{
				if (!m_TargetTexturePath.empty())
				{
					texturePaths.push_back(m_TargetTexturePath);
				}
			}
		}

		if (!texturePaths.empty())
		{
			for(const auto& path : texturePaths)
			{
				ImGui::Text("Selected Texture: %s", path.filename().string().c_str());
			}
		}

		static int selectedTextureType{};
		const char* textureTypeNames[] = {
			"Texture",
			"Material Texture",
			"Terrain Texture",
			"HDR"
		};

		if (ImGui::BeginCombo("Texture Type", textureTypeNames[selectedTextureType]))
		{
			for (int i = 0; i < IM_ARRAYSIZE(textureTypeNames); ++i)
			{
				const bool isSelected = (selectedTextureType == i);
				if (ImGui::Selectable(textureTypeNames[i], isSelected))
					selectedTextureType = i;

				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if (ImGui::Button("Select"))
		{
			for(const auto& path : texturePaths)
			{
				CopyTextureSelectType(path.string(), static_cast<TextureFileType>(selectedTextureType));
			}

			texturePaths.clear();
			ImGui::GetContext("TextureType Selector").Close();
		}

	}, ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::GetContext("TextureType Selector").Close();
#endif // BUILD_FLAG
}

void DataSystem::MonitorFiles()
{
}

void DataSystem::LoadModels()
{
	file::path shaderpath = PathFinder::Relative("Models\\");
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
	file::path source = filePath;
	file::path destination = PathFinder::Relative("Models\\") / file::path(filePath).filename();
	if(source != destination && file::exists(source) && !file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
	std::string name = file::path(filePath).stem().string();
	if (Models.find(name) != Models.end() && Models[name].get() != nullptr)
	{
		Debug->Log("ModelLoader::LoadModel : Model already loaded");
		return;
	}

	Managed::SharedPtr<Model> model = Model::LoadModelShared(destination.string());
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
	file::path source = filePath;
	file::path destination = PathFinder::Relative("Models\\") / file::path(filePath).filename();
	if (source != destination && file::exists(source) && file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}

	std::string name = file::path(filePath).stem().string();
	{
		std::unique_lock lock(m_modelMutex);
		if (Models.find(name) != Models.end() && Models[name].get() != nullptr)
		{
			Debug->Log("ModelLoader::LoadModel : Model already loaded");
			return Models[name].get();
		}
	}

	Managed::SharedPtr<Model> model{};
    try
    {
		std::string modelPath = destination.string();
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

void DataSystem::LoadTextures()
{
}

void DataSystem::LoadMaterials()
{
}

void DataSystem::InsertMaterial(std::shared_ptr<Material> material)
{
	std::unique_lock lock(m_materialMutex);
	std::string& mat_name = material->m_name;
	std::string baseName = mat_name;
	std::string uniqueName = baseName;
	auto m_fileGuid = material->m_fileGuid;
	int suffix = 1;

	while (true)
	{
		auto iter = DataSystems->Materials.find(uniqueName);
		if (iter != DataSystems->Materials.end())
		{
			if (iter->second->m_fileGuid == m_fileGuid)
			{
				return;
			}
			else
			{
				// 이름 충돌 발생 → 이름 뒤에 (숫자) 붙이기
				uniqueName = baseName + "(" + std::to_string(suffix++) + ")";
			}
		}
		else
		{
			break;
		}
	}

	mat_name = uniqueName;
	Materials[mat_name] = material;
}

void DataSystem::SaveMaterial(Material* material)
{
#ifndef BUILD_FLAG
    if (!material) return;

    file::path savePath = PathFinder::Relative("Materials\\") / (material->m_name + ".asset");
    std::ofstream fout(savePath);
    if (fout.is_open())
    {
        YAML::Node node = Meta::Serialize(material);
        if (!material->m_cbufferValues.empty())
        {
            YAML::Node cbNode;
            for (auto& [name, data] : material->m_cbufferValues)
            {
				YAML::Node entry;
				entry["name"] = name;
				entry["data"] = YAML::Binary(data.data(), data.size());
				cbNode.push_back(entry);
            }
            node["constant_buffers"] = cbNode;
        }
        fout << node;
        fout.close();
        ForceCreateYamlMetaFile(savePath);
    }
#endif // !BUILD_FLAG
}

Material* DataSystem::LoadMaterial(std::string_view name)
{
    std::string materialName = name.data();

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
#ifndef BUILD_FLAG
    file::path loadPath = PathFinder::Relative("Materials\\") / (materialName + ".asset");
    if (!file::exists(loadPath))
    {
		return nullptr;
    }

    MetaYml::Node node = MetaYml::LoadFile(loadPath.string());
    auto material = std::make_shared<Material>();
    Meta::Deserialize(material.get(), node);
    if (auto cbs = node["constant_buffers"])
    {
        for (auto cbEntry : cbs)
        {
            std::string cbName = cbEntry["name"].as<std::string>();
            YAML::Binary bin = cbEntry["data"].as<YAML::Binary>();
            std::vector<uint8_t> data(bin.data(), bin.data() + bin.size());
            material->m_cbufferValues.emplace(std::move(cbName), std::move(data));
        }
    }

    auto loadTex = [this](const std::string& texName, Texture*& texPtr, bool compress = false)
    {
        if (!texName.empty())
        {
            texPtr = LoadMaterialTexture(texName, compress);
        }
    };

    loadTex(material->m_baseColorTexName, material->m_pBaseColor, true);
    loadTex(material->m_normalTexName, material->m_pNormal);
    loadTex(material->m_ORM_TexName, material->m_pOccRoughMetal);
    loadTex(material->m_AO_TexName, material->m_AOMap);
    loadTex(material->m_EmissiveTexName, material->m_pEmissive);

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
#else
    return nullptr;
#endif
}

std::shared_ptr<Material> DataSystem::LoadMaterialShared(std::string_view name)
{
    // LoadMaterial이 로딩·캐시 삽입을 모두 처리하므로 그대로 태운 뒤,
    // 맵에서 shared_ptr을 꺼내 돌려준다(참조 카운트를 증가시켜 공동 소유).
    if (nullptr == LoadMaterial(name))
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> guard(m_materialMutex);
    auto it = Materials.find(std::string(name));
    return (it != Materials.end()) ? it->second : nullptr;
}

Texture* DataSystem::LoadTextureGUID(FileGuid guid)
{
	file::path texturePath = m_assetMetaRegistry->GetPath(guid);
	std::string name = texturePath.stem().string();
	if (Textures.find(name) != Textures.end())
	{
		Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
		return Textures[name].get();
	}
	Managed::SharedPtr<Texture> texture = Texture::LoadSharedFromPath(texturePath.string());
	if (texture)
	{
		Textures[name] = texture;
		texture->m_name = name;
		texture->m_extension = file::path(texturePath).extension().string();

		return texture.get();
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}

	return nullptr;
}

Texture* DataSystem::LoadTexture(std::string_view filePath, TextureFileType type)
{
	return LoadSharedTexture(filePath, type).get();
}

std::shared_ptr<Texture> DataSystem::LoadSharedTexture(std::string_view filePath, TextureFileType type)
{
	file::path source = filePath;
	file::path destination{};
	
	switch (type)
	{
	case DataSystem::TextureFileType::Texture:
		destination = PathFinder::Relative("Textures\\") / file::path(filePath).filename();
		break;
	case DataSystem::TextureFileType::UITexture:
		destination = PathFinder::Relative("UI\\") / file::path(filePath).filename();
		break;
	case DataSystem::TextureFileType::SpriteSheet:
		destination = PathFinder::Relative("SpriteSheets\\") / file::path(filePath).filename();
		break;
	default:
		break;
	}
		
	if (source != destination && file::exists(source) && !file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
	std::string name = file::path(filePath).stem().string();

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

	Managed::SharedPtr<Texture> texture = Texture::LoadSharedFromPath(destination.string());
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
		texture->m_extension = file::path(filePath).extension().string();

		return texture;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}

	return nullptr;
}

void DataSystem::CopyHDRTexture(std::string_view filePath)
{
	file::path source = filePath;
	file::path destination = PathFinder::Relative("HDR\\") / file::path(filePath).filename();
	if (source != destination && file::exists(source) && !file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
}

void DataSystem::CopyTexture(std::string_view filePath, const file::path& destination)
{
	if (filePath != destination && file::exists(filePath) && !file::exists(destination))
	{
		file::copy_file(filePath, destination, file::copy_options::update_existing);
	}
}

void DataSystem::SelectTextureType()
{
	if (!EngineSettingInstance->IsImGuiInitialized())
	{
		Debug->LogError("DataSystem::SelectTextureType : ImGui is not initialized");
		return;
	}

	auto context = ImGui::GetContext("TextureType Selector");

	if(!context.IsOpened())
	{
		ImGui::GetContext("TextureType Selector").Open();
	}
}

void DataSystem::CopyTextureSelectType(std::string_view filePath, TextureFileType type)
{
	file::path destination{};
	if (type == TextureFileType::Texture)
	{
		destination = PathFinder::Relative("Textures\\") / file::path(filePath).filename();
	}
	else if (type == TextureFileType::MaterialTexture)
	{
		destination = PathFinder::Relative("Materials\\") / file::path(filePath).filename();
	}
	else if (type == TextureFileType::TerrainTexture)
	{
		destination = PathFinder::Relative("Terrain\\Texture\\") / file::path(filePath).filename();
	}
	else if (type == TextureFileType::HDR)
	{
		destination = PathFinder::Relative("HDR\\") / file::path(filePath).filename();
	}
	else if (type == TextureFileType::UITexture)
	{
		destination = PathFinder::Relative("UI\\") / file::path(filePath).filename();
	}

	CopyTexture(filePath, destination);
}

Texture* DataSystem::LoadMaterialTexture(std::string_view filePath, bool isCompress)
{
    file::path destination = PathFinder::Relative("Materials\\") / file::path(filePath).filename();

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

void DataSystem::ForceCreateYamlMetaFile(const file::path& filepath)
{
	m_assetMetaWatcher->CreateYamlMeta(filepath);
}

void DataSystem::CreateVolumeProfile(const file::path& filepath)
{
#ifndef BUILD_FLAG
	VolumeProfile profile;
	profile.settings = EngineSettingInstance->GetRenderPassSettings();

	file::path savePath = ShowSaveFileDialog(L"", L"Save File", PathFinder::VolumeProfilePath());

	std::string baseName = savePath.stem().string();
	file::path fullPath = filepath / (baseName + ".volume");
	int index = 1;
	while (std::filesystem::exists(fullPath))
	{
		fullPath = filepath / (baseName + std::to_string(index++) + ".volume");
	}

	std::ofstream fout(fullPath);
	if (fout.is_open())
	{
		YAML::Node node = Meta::Serialize(&profile);
		fout << node;
		fout.close();
	}

	ForceCreateYamlMetaFile(fullPath);
#endif // !BUILD_FLAG
}

void DataSystem::SaveExistVolumeProfile(FileGuid guid, VolumeProfile* volume)
{
#ifndef BUILD_FLAG
	file::path savePath = m_assetMetaRegistry->GetPath(guid);
	if (savePath.empty())
	{
		Debug->LogError("DataSystem::SaveExistVolumeProfile : Save path is empty");
		return;
	}

	std::ofstream fout(savePath);
	if (fout.is_open())
	{
		YAML::Node node = Meta::Serialize(volume);
		fout << node;
		fout.close();
	}
#endif // !BUILD_FLAG
}

void DataSystem::AddSupportExtension(std::string_view ext)
{
	if (m_assetMetaWatcher)
	{
		m_assetMetaWatcher->AddRegisteredFile(ext.data());
	}
}

void DataSystem::RemoveSupportExtension(std::string_view ext)
{
	if (m_assetMetaWatcher)
	{
		m_assetMetaWatcher->RemoveRegisteredFile(ext.data());
	}
}

bool DataSystem::IsSupportExtension(std::string_view ext) const
{
	if (m_assetMetaWatcher)
	{
		return m_assetMetaWatcher->IsRegisteredFile(ext.data());
	}
	return false;
}

void DataSystem::OpenFile(const file::path& filepath)
{
#ifndef BUILD_FLAG
	// 확장자별로 "누가 연다"를 정하는 것은 에디터의 일이다. 프리팹은
	// PrefabEditor가 열었는데, 그것을 여기서 알면 자산 시스템이
	// 게임플레이 헤더를 여는 이유가 된다 (PHASE 4-3).
	if (m_openFileOverride && m_openFileOverride(filepath))
	{
		return;
	}
	HINSTANCE result = ShellExecute(NULL, L"open", filepath.c_str(), NULL, NULL, SW_SHOWNORMAL);

	if ((int)result <= 32)
	{
		MessageBox(NULL, L"Failed Open File", L"Error", MB_OK | MB_ICONERROR);
	}
#endif
}

void DataSystem::OpenExplorerSelectFile(const std::filesystem::path& filePath)
{
#ifndef BUILD_FLAG
	std::wstring args = L"/select,\"" + filePath.wstring() + L"\"";

	HINSTANCE result = ShellExecuteW(
		nullptr,         // HWND hwnd
		L"open",         // LPCWSTR lpOperation
		L"explorer.exe", // LPCWSTR lpFile
		args.c_str(),    // LPCWSTR lpParameters
		nullptr,         // LPCWSTR lpDirectory
		SW_SHOWNORMAL   // nShowCmd
	);

	// ShellExecute 실패 시 오류 코드 (0 ~ 32)
	if ((INT_PTR)result <= 32)
	{
		MessageBoxW(nullptr, L"Failed to open file in Explorer.", L"Error", MB_OK | MB_ICONERROR);
	}
#endif
}

void DataSystem::OpenSolutionAndFile(const file::path& slnPath, const file::path& filepath)
{
#ifndef BUILD_FLAG
	if (m_isExecuteSolution)
	{
		return;
	}

	std::wstring cmdLine = L"\"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe\" \"" +
		slnPath.wstring() + L"\" /Command \"File.OpenFile " + filepath.wstring() + L"\"";

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	std::wstring mutableCmd = cmdLine;

	if (CreateProcessW(
		nullptr,
		mutableCmd.data(),
		nullptr, nullptr,
		FALSE,
		0,
		nullptr, nullptr,
		&si,
		&pi))
	{
		m_isExecuteSolution = true;
		std::thread([hProcess = pi.hProcess, this]() 
		{
			while (true)
			{
				DWORD result = WaitForSingleObject(hProcess, 1);

				if (result == WAIT_OBJECT_0)
				{
					break;
				}
				else if (result == WAIT_FAILED)
				{
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}

			m_assetMetaRegistry->Clear();
			m_assetMetaWatcher->ScanAndGenerateMissingMeta(PathFinder::Relative());
			m_assetMetaWatcher->ScanAndCleanupInvalidMeta(PathFinder::Relative());
			m_isExecuteSolution = false;
			CloseHandle(hProcess);
		}).detach();

		CloseHandle(pi.hThread); // 스레드는 곧바로 닫아도 됨
	}
	else
	{
		MessageBoxW(nullptr, L"Visual Studio Execute Failed", L"Error", MB_ICONERROR);
	}
#endif // !BUILD_FLAG
}

FileGuid DataSystem::GetFileGuid(const file::path& filepath) const
{
	return m_assetMetaRegistry->GetGuid(filepath);
}

FileGuid DataSystem::GetFilenameToGuid(const std::string& filename) const
{
	return m_assetMetaRegistry->GetFilenameToGuid(filename);
}

FileGuid DataSystem::GetStemToGuid(const std::string& stem) const
{
	return m_assetMetaRegistry->GetStemToGuid(stem);
}

file::path DataSystem::GetFilePath(FileGuid fileguid) const
{
	return m_assetMetaRegistry->GetPath(fileguid);
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
