#include "DataSystem.h"
#include "ShaderSystem.h"
#include "Model.h"	
#include "PrefabEditor.h"
#include <future>
#include <shellapi.h>
#include <ppltasks.h>
#include <ppl.h>
#include "FileIO.h"
#include "Benchmark.hpp"
#include "SceneManager.h"
#include "PrefabUtility.h"
#include "ResourceAllocator.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ToggleUI.h"

using FileTypeCharArr = std::array<std::pair<DataSystem::FileType, const char*>, 10>;

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
	{ DataSystem::FileType::HDR,            "HDR"				}
}};

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
	else if (filepath.extension() == ".hlsl" || filepath.extension() == ".fx")
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

	RenderForEditer();
	m_watcher			= new efsw::FileWatcher();
	m_assetMetaRegistry = std::make_shared<AssetMetaRegistry>();
	m_assetMetaWatcher = std::make_shared<AssetMetaWatcher>(m_assetMetaRegistry.get());
	m_assetMetaWatcher->ScanAndGenerateMissingMeta(PathFinder::Relative());
	m_assetMetaWatcher->ScanAndCleanupInvalidMeta(PathFinder::Relative());
	m_watcher->addWatch(PathFinder::Relative().string(), m_assetMetaWatcher.get(), true);
	m_watcher->watch();
}

void DataSystem::Finalize()
{
    DeallocateResource(UnknownIcon);
    DeallocateResource(TextureIcon);
    DeallocateResource(ModelIcon);
    DeallocateResource(AssetsIcon);
    DeallocateResource(FolderIcon);
    DeallocateResource(ShaderIcon);
    DeallocateResource(CodeIcon);
	DeallocateResource(MainLightIcon);
	DeallocateResource(PointLightIcon);
	DeallocateResource(SpotLightIcon);
	DeallocateResource(DirectionalLightIcon);
	DeallocateResource(CameraIcon);

    Models.clear();
    Textures.clear();
    Materials.clear();

	delete m_watcher;
}

void DataSystem::RenderForEditer()
{
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
		if (!currentDirectory.empty() && std::filesystem::equivalent(currentDirectory, PathFinder::RelativeToPrefab("")))
		{
			ImGui::Dummy(ImGui::GetContentRegionAvail());
			overlayPos = ImGui::GetItemRectMin();
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
		auto deleter = [&](Model* model)
		{
			if (model)
			{
				DeallocateResource<Model>(model);
			}
		};

		Models[name] = std::shared_ptr<Model>(model, deleter);

		return model;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}
}

void DataSystem::LoadModel(const std::string_view& filePath)
{
	file::path source = filePath;
	file::path destination = PathFinder::Relative("Models\\") / file::path(filePath).filename();
	if(source != destination && file::exists(source) && !file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
	std::string name = file::path(filePath).stem().string();
	if (Models.find(name) != Models.end())
	{
		Debug->Log("ModelLoader::LoadModel : Model already loaded");
		return;
	}

	Model* model = Model::LoadModel(destination.string());
	if (model)
	{
        auto deleter = [&](Model* model)
        {
            if (model)
            {
                DeallocateResource<Model>(model);
            }
        };

		Models[name] = std::shared_ptr<Model>(model, deleter);

	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}
}

Model* DataSystem::LoadCashedModel(const std::string_view& filePath)
{
	file::path source = filePath;
	file::path destination = PathFinder::Relative("Models\\") / file::path(filePath).filename();
	if (source != destination && file::exists(source) && file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
	std::string name = file::path(filePath).stem().string();
	if (Models.find(name) != Models.end())
	{
		Debug->Log("ModelLoader::LoadModel : Model already loaded");
		return Models[name].get();
	}

    Model* model{};
    try
    {
        model = Model::LoadModel(destination.string());
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return nullptr;
    }

	if (model)
	{
        auto deleter = [&](Model* model)
        {
            if (model)
            {
                DeallocateResource<Model>(model);
            }
        };

		Models[name] = std::shared_ptr<Model>(model, deleter);
		return model;
	}
}

void DataSystem::LoadTextures()
{
}

void DataSystem::LoadMaterials()
{
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
	Texture* texture = Texture::LoadFormPath(texturePath.string());
	if (texture)
	{
		Textures[name] = std::shared_ptr<Texture>(texture, [&](Texture* texture)
		{
			if (texture)
			{
				DeallocateResource<Texture>(texture);
			}
		});
		return texture;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}
}

Texture* DataSystem::LoadTexture(const std::string_view& filePath)
{
	file::path source = filePath;
	file::path destination = PathFinder::Relative("Textures\\") / file::path(filePath).filename();
	if (source != destination && file::exists(source) && !file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
	std::string name = file::path(filePath).stem().string();
	if (Textures.find(name) != Textures.end())
	{
		Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
        return Textures[name].get();
	}

	Texture* texture = Texture::LoadFormPath(destination.string());

	if (texture)
	{
		Textures[name] = std::shared_ptr<Texture>(texture, [&](Texture* texture)
        {
            if (texture)
            {
                DeallocateResource<Texture>(texture);
            }
        });
        return texture;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}

	return nullptr;
}

void DataSystem::CopyHDRTexture(const std::string_view& filePath)
{
	file::path source = filePath;
	file::path destination = PathFinder::Relative("HDR\\") / file::path(filePath).filename();
	if (source != destination && file::exists(source) && !file::exists(destination))
	{
		file::copy_file(source, destination, file::copy_options::update_existing);
	}
}

void DataSystem::CopyTexture(const std::string_view& filePath, const file::path& destination)
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

void DataSystem::CopyTextureSelectType(const std::string_view& filePath, TextureFileType type)
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

	CopyTexture(filePath, destination);
}

Texture* DataSystem::LoadMaterialTexture(const std::string_view& filePath)
{
    file::path destination = PathFinder::Relative("Materials\\") / file::path(filePath).filename();

    std::string name = file::path(filePath).stem().string();
    if (Textures.find(name) != Textures.end())
    {
        Debug->Log("TextureLoader::LoadTexture : Texture already loaded");
        return Textures[name].get();
    }

    Texture* texture = Texture::LoadFormPath(destination.string());

    if (texture)
    {
        Textures[name] = std::shared_ptr<Texture>(texture, [&](Texture* texture)
        {
            if (texture)
            {
                DeallocateResource<Texture>(texture);
            }
        });
        return texture;
    }
    else
    {
        Debug->LogError("ModelLoader::LoadModel : Model file not found");
    }

    return nullptr;
}

Material* DataSystem::CreateMaterial()
{
	Material* material = AllocateResource<Material>();
	if (material)
	{
		auto deleter = [&](Material* mat)
		{
			if (mat)
			{
				DeallocateResource<Material>(mat);
			}
		};
		std::string name = "NewMaterial";
		int index = 1;
		while (Materials.find(name) != Materials.end())
		{
			name = "NewMaterial" + std::to_string(index++);
		}
		material->m_name = name;
		material->m_fileGuid = make_file_guid(name);
		
		Materials[name] = std::shared_ptr<Material>(material, deleter);
		
		return material;
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

	SFonts.emplace(name, std::make_shared<SpriteFont>(DeviceState::g_pDevice, destination.c_str()));
	
	return SFonts[name].get();
}

void DataSystem::OpenContentsBrowser()
{
	ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Open();
}

void DataSystem::CloseContentsBrowser()
{
	ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser").Close();
}

void DataSystem::ShowDirectoryTree(const file::path& directory)
{
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
		if (ImGui::MenuItem("Open Save Directory"))
		{
			OpenExplorerSelectFile(MenuDirectory);
			MenuDirectory.clear();
		}
		ImGui::EndPopup();
	}
}

void DataSystem::ShowCurrentDirectoryFiles()
{
	if(m_ContentsBrowserStyle == ContentsBrowserStyle::Tile)
	{
		ShowCurrentDirectoryFilesTile();
	}
	else
	{
		currentDirectory = PathFinder::Relative();

		ShowCurrentDirectoryFilesTree(currentDirectory);
	}
}

void DataSystem::ShowCurrentDirectoryFilesTile()
{
	ImGui::SetCursorScreenPos(overlayPos);
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
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			if (extension == ".fbx" || extension == ".gltf" || extension == ".obj" ||
				extension == ".glb" || extension == ".png" || extension == ".dds" ||
				extension == ".hdr" || extension == ".hlsl" || extension == ".cpp" ||
				extension == ".cs" || extension == ".wav" || extension == ".mp3" ||
				extension == ".prefab")
			{
				ImTextureID iconTexture{};
				FileType fileType = FileType::Unknown;
				if (extension == ".fbx" ||
					extension == ".gltf" ||
					extension == ".obj" ||
					extension == ".glb")
				{
					fileType = FileType::Model;
					iconTexture = (ImTextureID)ModelIcon->m_pSRV;
				}
				else if (extension == ".png" || extension == ".dds")
				{
					fileType = FileType::Texture;
					iconTexture = (ImTextureID)TextureIcon->m_pSRV;
				}
				else if (extension == ".hdr")
				{
					fileType = FileType::HDR;
					iconTexture = (ImTextureID)TextureIcon->m_pSRV;
				}
				else if (extension == ".hlsl")
				{
					fileType = FileType::Shader;
					iconTexture = (ImTextureID)ShaderIcon->m_pSRV;
				}
				else if (extension == ".cpp")
				{
					fileType = FileType::CppScript;
					iconTexture = (ImTextureID)CodeIcon->m_pSRV;
				}
				else if (extension == ".cs")
				{
					fileType = FileType::CSharpScript;
					iconTexture = (ImTextureID)CodeIcon->m_pSRV;
				}
				else if (extension == ".wav" || extension == ".mp3")
				{
					fileType = FileType::Sound;
					iconTexture = (ImTextureID)UnknownIcon->m_pSRV;
				}
				else if (extension == ".prefab")
				{
					fileType = FileType::Prefab;
					iconTexture = (ImTextureID)ModelIcon->m_pSRV;
				}

				DrawFileTile(iconTexture, entry.path(), entry.path().filename().string(), fileType);

				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns(1);
}

void DataSystem::ShowCurrentDirectoryFilesTree(const file::path& directory)
{
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
				ShowCurrentDirectoryFilesTree(entry.path());
				ImGui::TreePop();
			}
		}
		else if (entry.is_regular_file())
		{
			if(filter.IsActive() && !filter.PassFilter(entry.path().filename().string().c_str()))
				continue;

			std::string extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			if (extension == ".fbx" || extension == ".gltf" || extension == ".obj" ||
				extension == ".glb" || extension == ".png" || extension == ".dds" ||
				extension == ".hdr" || extension == ".hlsl" || extension == ".cpp" ||
				extension == ".cs" || extension == ".wav" || extension == ".mp3" ||
				extension == ".prefab")
			{
				std::string label = entry.path().filename().string();
				std::string apliedIcon;

				if (extension == ".fbx" ||
					extension == ".gltf" ||
					extension == ".obj" ||
					extension == ".glb")
				{
					apliedIcon = ICON_FA_CUBE " ";
				}
				else if (extension == ".png" || extension == ".dds")
				{
					apliedIcon = ICON_FA_IMAGE " ";
				}
				else if (extension == ".hdr")
				{
					apliedIcon = ICON_FA_IMAGE " ";
				}
				else if (extension == ".hlsl")
				{
					apliedIcon = ICON_FA_FILE_CONTRACT " ";
				}
				else if (extension == ".cpp")
				{
					apliedIcon = ICON_FA_FILE_CODE " ";
				}
				else if (extension == ".cs")
				{
					apliedIcon = ICON_FA_FILE_CODE " ";
				}
				else if (extension == ".wav" || extension == ".mp3")
				{
					apliedIcon = ICON_FA_FILE_AUDIO " ";
				}
				else if (extension == ".prefab")
				{
					apliedIcon = ICON_FA_BOX_OPEN " ";
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

}

void DataSystem::DrawFileTile(ImTextureID iconTexture, const file::path& directory, const std::string& fileName, FileType& fileType, const ImVec2& tileSize)
{
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
}

void DataSystem::ForceCreateYamlMetaFile(const file::path& filepath)
{
	m_assetMetaWatcher->CreateYamlMeta(filepath);
}

void DataSystem::OpenFile(const file::path& filepath)
{
    if(filepath.extension() == ".prefab") { PrefabEditors->Open(filepath.string()); return; }
	HINSTANCE result = ShellExecute(NULL, L"open", filepath.c_str(), NULL, NULL, SW_SHOWNORMAL);

	if ((int)result <= 32)
	{
		MessageBox(NULL, L"Failed Open File", L"Error", MB_OK | MB_ICONERROR);
	}
}

void DataSystem::OpenExplorerSelectFile(const std::filesystem::path& filePath)
{
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
}

void DataSystem::OpenSolutionAndFile(const file::path& slnPath, const file::path& filepath)
{
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
