#include "DataSystem.h"
#include "ShaderSystem.h"
#include "Model.h"	
#include "PrefabEditor.h"
#include <future>
#include <shellapi.h>
#include <ppltasks.h>
#include <ppl.h>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include "FileIO.h"
#include "VolumeProfile.h"
#include "Benchmark.hpp"
#include "SceneManager.h"
#include "PrefabUtility.h"
#include "FileDialog.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ToggleUI.h"

using FileTypeCharArr = std::array<std::pair<DataSystem::FileType, const char*>, (size_t)DataSystem::FileType::End>;

constexpr FileTypeCharArr FileTypeStringTable{{
	{ DataSystem::FileType::Model,          "Model"				},
	{ DataSystem::FileType::Texture,        "Texture"			},
	{ DataSystem::FileType::MaterialTexture,"MaterialTexture"	},
	{ DataSystem::FileType::TerrainTexture, "TerrainTexture"	},
	{ DataSystem::FileType::Shader,         "Shader"			},
	{ DataSystem::FileType::CppScript,      "CppScript"			},
	{ DataSystem::FileType::CSharpScript,   "CSharpScript"		},
    { DataSystem::FileType::Prefab,         "Prefab"            },
	{ DataSystem::FileType::Sound,          "Sound"				},
	{ DataSystem::FileType::HDR,            "HDR"				},
	{ DataSystem::FileType::VolumeProfile , "VolumeProfile"		},
	{ DataSystem::FileType::Font,           "Font"				}
}};

static const std::unordered_map<std::string_view, std::string_view> kExtensionToIcon = {
	// 모델 파일
	{ ".fbx", ICON_FA_CUBE " " },
	{ ".gltf", ICON_FA_CUBE " " },
	{ ".obj", ICON_FA_CUBE " " },
	{ ".glb", ICON_FA_CUBE " " },

	// 이미지 파일
	{ ".png", ICON_FA_IMAGE " " },
	{ ".dds", ICON_FA_IMAGE " " },
	{ ".hdr", ICON_FA_IMAGE " " },

	// 쉐이더, 코드 파일
	{ ".hlsl", ICON_FA_FILE_CONTRACT " " },
	{ ".shader", ICON_FA_FILE_CONTRACT " " },
	{ ".cpp",  ICON_FA_FILE_CODE " " },
	{ ".cs",   ICON_FA_FILE_CODE " " },

	// 오디오 파일
	{ ".wav", ICON_FA_FILE_AUDIO " " },
	{ ".mp3", ICON_FA_FILE_AUDIO " " },

	// 프리팹, 볼륨 등
	{ ".terrain", ICON_FA_MOUNTAIN " " },
	{ ".prefab", ICON_FA_BOX_OPEN " " },
	{ ".volume", ICON_FA_SLIDERS " " },

	// 기타 파일
	{ ".spritefont", ICON_FA_FONT " " }
};

// 검색 함수
constexpr const char* FileTypeToString(DataSystem::FileType type)
{
	// 선형 검색
	for (auto&& kv : FileTypeStringTable)
	{
		if (kv.first == type)
			return kv.second;
	}
	return "Unknown";
}

DataSystem::FileType GetFileType(const file::path& filepath)
{
	if (filepath.extension() == ".fbx" || filepath.extension() == ".gltf" || filepath.extension() == ".glb")
		return DataSystem::FileType::Model;
	else if (filepath.extension() == ".png" || filepath.extension() == ".jpg" || filepath.extension() == ".jpeg")
		return DataSystem::FileType::Texture;
	else if (filepath.extension() == ".mat")
		return DataSystem::FileType::MaterialTexture;
	else if (filepath.extension() == ".terrain")
		return DataSystem::FileType::TerrainTexture;
	else if (filepath.extension() == ".hlsl" || filepath.extension() == ".fx" || filepath.extension() == ".shader")
		return DataSystem::FileType::Shader;
	else if (filepath.extension() == ".cpp" || filepath.extension() == ".h")
		return DataSystem::FileType::CppScript;
	else if (filepath.extension() == ".cs")
		return DataSystem::FileType::CSharpScript;
	else if (filepath.extension() == ".wav" || filepath.extension() == ".mp3")
		return DataSystem::FileType::Sound;
	else if (filepath.extension() == ".hdr")
		return DataSystem::FileType::HDR;
	else if (filepath.extension() == ".prefab")
		return DataSystem::FileType::Prefab;
	else if (filepath.extension() == ".volume")
		return DataSystem::FileType::VolumeProfile;
	else if (filepath.extension() == ".spritefont")
		return DataSystem::FileType::Font;
	return DataSystem::FileType::Unknown;
}

ImGuiTextFilter DataSystem::filter;
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
	Finalize();
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
	m_ContentsBrowserStyle = EngineSettingInstance->GetContentsBrowserStyle();

	kExtensionMap =
	{
		{ ".fbx",	 { FileType::Model,			(ImTextureID)ModelIcon->m_pSRV }	},
		{ ".gltf",   { FileType::Model,			(ImTextureID)ModelIcon->m_pSRV }	},
		{ ".obj",    { FileType::Model,			(ImTextureID)ModelIcon->m_pSRV }	},
		{ ".glb",    { FileType::Model,			(ImTextureID)ModelIcon->m_pSRV }	},
		{ ".png",    { FileType::Texture,		(ImTextureID)TextureIcon->m_pSRV }	},
		{ ".dds",    { FileType::Texture,		(ImTextureID)TextureIcon->m_pSRV }	},
		{ ".hdr",    { FileType::HDR,			(ImTextureID)TextureIcon->m_pSRV }	},
		{ ".hlsl",   { FileType::Shader,		(ImTextureID)ShaderIcon->m_pSRV }	},
		{ ".shader", { FileType::Shader,		(ImTextureID)ShaderIcon->m_pSRV }	},
		{ ".cpp",    { FileType::CppScript,		(ImTextureID)CodeIcon->m_pSRV }		},
		{ ".cs",     { FileType::CSharpScript,	(ImTextureID)CodeIcon->m_pSRV }		},
		{ ".wav",    { FileType::Sound,			(ImTextureID)UnknownIcon->m_pSRV }	},
		{ ".mp3",    { FileType::Sound,			(ImTextureID)UnknownIcon->m_pSRV }	},
		{ ".terrain",{ FileType::TerrainTexture, (ImTextureID)TextureIcon->m_pSRV } },
		{ ".prefab", { FileType::Prefab,		(ImTextureID)AssetsIcon->m_pSRV }	},
		{ ".volume", { FileType::VolumeProfile,	(ImTextureID)AssetsIcon->m_pSRV }	},
		{ ".spritefont",{ FileType::Font,		(ImTextureID)AssetsIcon->m_pSRV }   }
	};

	RenderForEditer();
#endif
	m_watcher			= new efsw::FileWatcher();
	m_assetMetaRegistry = std::make_shared<AssetMetaRegistry>();
	m_assetMetaWatcher	= std::make_shared<AssetMetaWatcher>(m_assetMetaRegistry.get());
	m_assetMetaWatcher->ScanAndGenerateMissingMeta(PathFinder::Relative());
	m_assetMetaWatcher->ScanAndCleanupInvalidMeta(PathFinder::Relative());
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

		static Material* select_material = nullptr;

		const float tileWidth = 100.f;

		int tileColumns = (int)(availableWidth / tileWidth);
		tileColumns = (tileColumns > 0) ? tileColumns : 1;

		searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);

		ImTextureID iconTexture = (ImTextureID)ModelIcon->m_pSRV;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
		if (ImGui::BeginChild("DirectoryHierarchy", ImVec2(0, 300), false, ImGuiWindowFlags_AlwaysUseWindowPadding))
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
						select_material = Material.get();
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

	ImGui::ContextRegister(ICON_FA_HARD_DRIVE " Content Browser", true, [&]()
	{
		ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").SetPopup(
			m_ContentsBrowserStyle == ContentsBrowserStyle::Tile);

		static file::path DataDirectory = PathFinder::Relative();
		if (ImGui::Button(ICON_FA_ARROWS_ROTATE, ImVec2(0, 0)))
		{
			ShaderSystem->SetReloading(true);
		}

		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
		ImGui::BeginDisabled();
		ImGui::Button(ICON_FA_MAGNIFYING_GLASS);
		ImGui::EndDisabled();
		ImGui::SameLine();
		filter.Draw("##Assets Search", ImGui::GetContentRegionAvail().x - 90);
		ImGui::PopStyleVar();

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

		if(m_ContentsBrowserStyle == ContentsBrowserStyle::Tile)
		{
			ImGui::BeginChild("DirectoryHierarchy", ImVec2(200, 0), false);
			ImGuiTreeNodeFlags rootFlags =
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_SpanFullWidth |
				ImGuiTreeNodeFlags_DefaultOpen;

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 1));
			if (ImGui::TreeNodeEx(ICON_FA_FOLDER " Assets", rootFlags))
			{
				ShowDirectoryTree(DataDirectory);
				ImGui::TreePop();
			}
			ImGui::PopStyleVar();
			ImGui::EndChild();

			ImGui::SameLine();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
		ImGui::BeginChild("FileList", ImVec2(0, 0), false);
		ImGui::PopStyleVar();
		ImGui::Dummy(ImGui::GetContentRegionAvail());
		overlayPos = ImGui::GetItemRectMin();
		if (!currentDirectory.empty() && std::filesystem::equivalent(currentDirectory, PathFinder::RelativeToPrefab("")))
		{
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
				{
					auto scene = SceneManagers->GetActiveScene();
					if (scene)
					{
						GameObject::Index index = *static_cast<GameObject::Index*>(payload->Data);
						auto objPtr = scene->GetGameObject(index);
						if (objPtr)
						{
							GameObject* obj = objPtr.get();
							Prefab* prefab = PrefabUtilitys->CreatePrefab(obj, obj->m_name.ToString());
							if (prefab)
							{
								file::path savePath = PathFinder::RelativeToPrefab(obj->m_name.ToString() + ".prefab");
								PrefabUtilitys->SavePrefab(prefab, savePath.string());
								ForceCreateYamlMetaFile(savePath);
								delete prefab;
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::SetCursorScreenPos(overlayPos);

		ShowCurrentDirectoryFiles();
		ImGui::PopStyleColor();
		ImGui::EndChild();

	}, ImGuiWindowFlags_None);

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
	//ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Close();
	if (m_ContentsBrowserStyle == ContentsBrowserStyle::Tile) 
	{
		ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Close();
	}
	else 
	{
		ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Open();
	}
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
	if (Models.find(name) != Models.end())
	{
		Debug->Log("ModelLoader::LoadModel : Model already loaded");
		auto model = Models[name].get();
		return model;
	}

	Model* model = Model::LoadModel(modelPath.string());
	if (model)
	{

		Models[name] = std::shared_ptr<Model>(model);

		return model;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}
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
		Models[name] = model;
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
	if (Models.find(name) != Models.end() && Models[name].get() != nullptr)
	{
		Debug->Log("ModelLoader::LoadModel : Model already loaded");
		return Models[name].get();
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
		Models[name] = model;
		return model.get();
	}
}

void DataSystem::LoadTextures()
{
}

void DataSystem::LoadMaterials()
{
}

void DataSystem::SaveMaterial(Material* material)
{
#ifndef BUILD_FLAG
        if (!material)
                return;

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
        if (Materials.find(materialName) != Materials.end())
        {
                Debug->Log("MaterialLoader::LoadMaterial : Material already loaded");
                return Materials[materialName].get();
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

        Materials[material->m_name] = material;
        return material.get();
#else
        return nullptr;
#endif
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
	if (Textures.find(name) != Textures.end())
	{
		Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
		return Textures[name];
	}
	Managed::SharedPtr<Texture> texture = Texture::LoadSharedFromPath(destination.string());
	if (texture)
	{
		switch (type)
		{
		case DataSystem::TextureFileType::Texture:
			Textures[name] = texture;
			break;
		case DataSystem::TextureFileType::UITexture:
			UITextures[name] = texture;
			break;
		default:
			break;
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
    if (Textures.find(name) != Textures.end())
    {
        Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
        return Textures[name].get();
    }

    auto texture = Texture::LoadSharedFromPath(destination.string(), isCompress);

    if (texture)
    {
		Textures[name] = texture;
        return texture.get();
    }
    else
    {
        Debug->LogError("ModelLoader::LoadModel : Model file not found");
    }

    return nullptr;
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

SpriteFont* DataSystem::LoadSFont(const std::wstring_view& filePath)
{
	file::path destination = PathFinder::Relative("Font\\") / file::path(filePath).filename();
	std::string name = file::path(filePath).stem().string();

	if (!SFonts.empty())
	{
		if (SFonts.find(name) != SFonts.end())
		{
			Debug->Log("Font already loaded");
			return SFonts[name].get();
		}
	}

	SFonts.emplace(name, std::make_shared<SpriteFont>(DirectX11::DeviceStates->g_pDevice, destination.c_str()));
	
	return SFonts[name].get();
}

void DataSystem::OpenContentsBrowser()
{
#ifndef BUILD_FLAG
	ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Open();
#endif
}

void DataSystem::CloseContentsBrowser()
{
#ifndef BUILD_FLAG
	ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Close();
#endif
}

void DataSystem::ShowDirectoryTree(const file::path& directory)
{
#ifndef BUILD_FLAG
	static file::path MenuDirectory{};
	static bool isRightClicked = false;
	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_directory())
		{
			std::string dirName = entry.path().filename().string();
			ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
			if (currentDirectory == entry.path())
				nodeFlags |= ImGuiTreeNodeFlags_Selected;

			std::string iconName = ICON_FA_FOLDER + std::string(" ") + dirName;
			bool nodeOpen = ImGui::TreeNodeEx(iconName.c_str(), nodeFlags);
			if (!entry.path().empty() && std::filesystem::equivalent(entry.path(), PathFinder::RelativeToPrefab("")))
			{
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
					{
						auto scene = SceneManagers->GetActiveScene();
						if (scene)
						{
							GameObject::Index index = *static_cast<GameObject::Index*>(payload->Data);
							auto objPtr = scene->GetGameObject(index);
							if (objPtr)
							{
								GameObject* obj = objPtr.get();
								Prefab* prefab = PrefabUtilitys->CreatePrefab(obj, obj->m_name.ToString());
								if (prefab)
								{
									file::path savePath = PathFinder::RelativeToPrefab(obj->m_name.ToString() + ".prefab");
									PrefabUtilitys->SavePrefab(prefab, savePath.string());
									ForceCreateYamlMetaFile(savePath);
									delete prefab;
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
			if (ImGui::IsItemClicked())
			{
				currentDirectory = entry.path();
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				MenuDirectory = entry.path();
				isRightClicked = true;
			}

			if (nodeOpen)
			{
				ShowDirectoryTree(entry.path());
				ImGui::TreePop();
			}
		}
	}

	if(isRightClicked)
	{
		ImGui::OpenPopup("Context Menu");
		isRightClicked = false;
	}

	if (ImGui::BeginPopup("Context Menu"))
	{
		if (MenuDirectory.empty() && std::filesystem::equivalent(MenuDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				CreateVolumeProfile(MenuDirectory);
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			OpenExplorerSelectFile(MenuDirectory);
			MenuDirectory.clear();
		}
		ImGui::EndPopup();
	}
#endif // !BUILD_FLAG
}

void DataSystem::ShowCurrentDirectoryFiles()
{
#ifndef BUILD_FLAG
	if(m_ContentsBrowserStyle == ContentsBrowserStyle::Tile)
	{
		ShowCurrentDirectoryFilesTile();
	}
	else
	{
		currentDirectory = PathFinder::Relative();

		ShowCurrentDirectoryFilesTree(currentDirectory);
	}
#endif
}

void DataSystem::ShowCurrentDirectoryFilesTile()
{
#ifndef BUILD_FLAG
	float availableWidth = ImGui::GetContentRegionAvail().x;

	const float tileWidth = 200.0f;

	int tileColumns = (int)(availableWidth / tileWidth);
	tileColumns = (tileColumns > 0) ? tileColumns : 1;

	ImGui::Columns(tileColumns, nullptr, false);

	if (currentDirectory.empty())
	{
		currentDirectory = PathFinder::Relative();
	}

	for (const auto& entry : file::directory_iterator(currentDirectory))
	{
		if (entry.is_regular_file())
		{
			if (filter.IsActive() && !filter.PassFilter(entry.path().filename().string().c_str()))
				continue;

			std::string extension = entry.path().extension().string();
			if (IsSupportExtension(extension))
			{
				ImTextureID iconTexture{};
				FileType fileType = FileType::Unknown;
				if (auto it = kExtensionMap.find(extension); it != kExtensionMap.end())
				{
					fileType = it->second.type;
					iconTexture = it->second.icon;
				}

				DrawFileTile(iconTexture, entry.path(), entry.path().filename().string(), fileType);

				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns(1);

	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("Directory Context");
	}

	if (ImGui::BeginPopup("Directory Context"))
	{
		if (!currentDirectory.empty() && std::filesystem::equivalent(currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				CreateVolumeProfile(currentDirectory);
			}
		}
		ImGui::EndPopup();
	}
#endif // !BUILD_FLAG
}

void DataSystem::ShowCurrentDirectoryFilesTree(const file::path& directory)
{
#ifndef BUILD_FLAG
	static file::path currentDirectory;
	static FileType selectedFileType = FileType::Unknown;
	static std::string draggedFileType{};
	static bool isRightClicked = false;
	static bool isHoverAndClicked = false;

	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_directory())
		{
			std::string name = entry.path().filename().string();
			std::string label = std::string(ICON_FA_FOLDER " ") + name;

			if(ImGui::TreeNode(label.c_str()))
			{
				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
				{
					currentDirectory = entry.path();
					selectedFileType = GetFileType(entry.path());
					isRightClicked = true;
				}
				if (!entry.path().empty() && std::filesystem::equivalent(entry.path(), PathFinder::RelativeToPrefab("")))
				{
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
						{
							auto scene = SceneManagers->GetActiveScene();
							if (scene)
							{
								GameObject::Index index = *static_cast<GameObject::Index*>(payload->Data);
								auto objPtr = scene->GetGameObject(index);
								if (objPtr)
								{
									GameObject* obj = objPtr.get();
									Prefab* prefab = PrefabUtilitys->CreatePrefab(obj, obj->m_name.ToString());
									if (prefab)
									{
										file::path savePath = PathFinder::RelativeToPrefab(obj->m_name.ToString() + ".prefab");
										PrefabUtilitys->SavePrefab(prefab, savePath.string());
										ForceCreateYamlMetaFile(savePath);
										delete prefab;
									}
								}
							}
						}
						ImGui::EndDragDropTarget();
					}
				}
				ShowCurrentDirectoryFilesTree(entry.path());
				ImGui::TreePop();
			}
		}
		else if (entry.is_regular_file())
		{
			if(filter.IsActive() && !filter.PassFilter(entry.path().filename().string().c_str()))
				continue;

			std::string extension = entry.path().extension().string();
			if (IsSupportExtension(extension))
			{
				std::string label = entry.path().filename().string();
				std::string apliedIcon;

				if(auto it = kExtensionToIcon.find(extension); it != kExtensionToIcon.end())
				{
					apliedIcon = it->second;
				}

				label = apliedIcon + label;

				ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				{
					OpenFile(entry.path());
				}
				else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
				{
					currentDirectory = entry.path();
					selectedFileType = GetFileType(entry.path());
					isHoverAndClicked = true;
				}
				else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
				{
					currentDirectory = entry.path();
					selectedFileType = GetFileType(entry.path());
					isRightClicked = true;
				}

				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				{
					ImGui::SetDragDropPayload(FileTypeToString(selectedFileType), entry.path().string().c_str(), entry.path().string().size() + 1);
					ImGui::Text("Dragging %s", entry.path().filename().string().c_str());
					ImGui::EndDragDropSource();
				}
			}
		}
	}

	if (isRightClicked)
	{
		ImGui::OpenPopup("Context Menu");
		isRightClicked = false;
	}

	if (ImGui::BeginPopup("Context Menu"))
	{
		if (!currentDirectory.empty() && std::filesystem::equivalent(currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				CreateVolumeProfile(currentDirectory);
			}
		}
		if (ImGui::MenuItem("Delete"))
		{
			file::remove(currentDirectory);
			if (currentDirectory.extension() == ".cpp")
			{
				file::path headerPath = currentDirectory;
				headerPath.replace_extension(".h");
				if (file::exists(headerPath))
				{
					file::remove(headerPath);
					std::this_thread::sleep_for(std::chrono::seconds(1));
					ScriptManager->CompileEvent();
				}
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			OpenExplorerSelectFile(currentDirectory);
		}
		ImGui::EndPopup();
	}

	if (isHoverAndClicked && !currentDirectory.empty())
	{
		selectedMetaFilePath = currentDirectory.string() + ".meta";
		selectedFileName = currentDirectory.filename().string();
		draggedFileType = FileTypeToString(selectedFileType);
		try
		{
			selectedFileMetaNode = YAML::LoadFile(selectedMetaFilePath);
		}
		catch (const std::exception& e)
		{
			Debug->LogError(e.what());
			selectedFileMetaNode = std::nullopt;
		}

		isHoverAndClicked = false;
	}
#endif // !BUILD_FLAG
}

void DataSystem::DrawFileTile(ImTextureID iconTexture, const file::path& directory, const std::string& fileName, FileType& fileType, const ImVec2& tileSize)
{
#ifndef BUILD_FLAG
	ImGui::PushID(fileName.c_str());
	ImGui::BeginGroup();
	ImU32 color{};
	if (ImGui::ImageButton(fileName.c_str(), iconTexture, tileSize))
	{
		selectedMetaFilePath = directory.string();
		selectedMetaFilePath += ".meta";

		selectedFileName = fileName;

		try
		{
			selectedFileMetaNode = YAML::LoadFile(selectedMetaFilePath);
		}
		catch (const std::exception& e)
		{
			Debug->LogError(e.what());
			selectedFileMetaNode = std::nullopt;
		}
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
	{
		OpenFile(directory);
	}
	else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("Context Menu");
	}

	if (ImGui::BeginPopup("Context Menu"))
	{
		if (ImGui::MenuItem("Delete"))
		{
			file::remove(directory);
			if (directory.extension() == ".cpp")
			{
				file::path headerPath = directory;
				headerPath.replace_extension(".h");
				if (file::exists(headerPath))
				{
					file::remove(headerPath);
					std::this_thread::sleep_for(std::chrono::seconds(1));
					ScriptManager->CompileEvent();
				}
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			OpenExplorerSelectFile(directory);
		}
		if (currentDirectory.empty() && std::filesystem::equivalent(currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				CreateVolumeProfile(currentDirectory);
			}
		}
		ImGui::EndPopup();
	}

	ImVec2 pos = ImGui::GetCursorScreenPos();
	std::string typeID{};
	float lineWidth = tileSize.x + 10;
	ImVec2 lineStart = ImVec2(pos.x, pos.y + 2);
	ImVec2 lineEnd = ImVec2(pos.x + lineWidth, pos.y + 2);

	switch (fileType)
	{
	case FileType::Model:
		color = IM_COL32(255, 165, 0, 255);
		break;
	case FileType::Texture:
	case FileType::HDR:
		color = IM_COL32(0, 255, 0, 255);
		break;
	case FileType::Shader:
		color = IM_COL32(0, 0, 255, 255);
		break;
	case FileType::CppScript:
		color = IM_COL32(255, 0, 0, 255);
		break;
	case FileType::CSharpScript:
		color = IM_COL32(255, 0, 255, 255);
		break;
        case FileType::Prefab:
        color = IM_COL32(0, 128, 255, 255);
        break;
	case FileType::Sound:
		color = IM_COL32(255, 255, 0, 255);
		break;
	case FileType::Font:
		color = IM_COL32(128, 0, 128, 255);
		break;
	case FileType::Unknown:
		color = IM_COL32(128, 128, 128, 255);
		break;
	}

	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, color, 2.0f);

	ImGui::Dummy(ImVec2(0, 8));

	ImGui::PushFont(smallFont);
	ImGui::TextWrapped("%s", fileName.c_str());
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 2));
	ImGui::PushFont(extraSmallFont);
	ImGui::TextWrapped("%s", FileTypeToString(fileType));
	ImGui::PopFont();

	ImGui::EndGroup();

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload(FileTypeToString(fileType), fileName.c_str(), fileName.size() + 1);
		ImGui::Text("Dragging %s", fileName.c_str());
		ImGui::EndDragDropSource();
	}

	ImGui::PopID();
#endif // !BUILD_FLAG
}

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
    if(filepath.extension() == ".prefab") { PrefabEditors->Open(filepath.string()); return; }
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

			// Visual Studio 종료 감지 완료
			if (!ScriptManager->IsCompileEventInvoked())
			{
				ScriptManager->SetCompileEventInvoked(true);
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
		switch (type)
		{
		case ManagedAssetType::Model:
			LoadModel(entry.assetName.string());
			break;
		case ManagedAssetType::Material:
			LoadMaterial(entry.assetName.string());
			break;
		case ManagedAssetType::Texture:
			LoadTexture(entry.assetName.string());
			break;
		case ManagedAssetType::SpriteFont:
			LoadSFont(entry.assetName.wstring());
			break;
		default:
			break;
		}
	}
}

void DataSystem::RetainAssets(const AssetBundle& bundle)
{
	for (const auto& entry : bundle.assets)
	{
		m_retainedAssets[entry.assetTypeID].insert(entry.assetName.stem().string());
	}
}

void DataSystem::ClearRetainedAssets()
{
	m_retainedAssets.clear();
}

void DataSystem::UnloadUnusedAssets()
{
	auto removeUnused = [this](auto& container, int type)
	{
		auto it = container.begin();
		auto& retainSet = m_retainedAssets[type];
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

	removeUnused(Models, static_cast<int>(ManagedAssetType::Model));
	removeUnused(Materials, static_cast<int>(ManagedAssetType::Material));
	removeUnused(Textures, static_cast<int>(ManagedAssetType::Texture));
	removeUnused(SFonts, static_cast<int>(ManagedAssetType::SpriteFont));
}
