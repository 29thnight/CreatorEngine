#include "DataSystem.h"
//#include "MaterialLoader.h"
#include "ShaderSystem.h"
#include "Model.h"	
#include <future>
#include <shellapi.h>
#include <ppltasks.h>
#include <ppl.h>
#include "Benchmark.hpp"
#include "ResourceAllocator.h"

#include "IconsFontAwesome6.h"
#include "fa.h"

ImGuiTextFilter DataSystem::filter;

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

}

void DataSystem::Initialize()
{
	file::path iconpath = PathFinder::IconPath();
	UnknownIcon = Texture::LoadFormPath(iconpath.string() + "Unknown.png");
	TextureIcon = Texture::LoadFormPath(iconpath.string() + "Texture.png");
	ModelIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");
	AssetsIcon = Texture::LoadFormPath(iconpath.string() + "Assets.png");
	FolderIcon = Texture::LoadFormPath(iconpath.string() + "Folder.png");
	ShaderIcon = Texture::LoadFormPath(iconpath.string() + "Shader.png");
	CodeIcon = Texture::LoadFormPath(iconpath.string() + "Code.png");

	smallFont = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 12.0f);
	extraSmallFont = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 10.0f);

	RenderForEditer();
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

    Models.clear();
    Textures.clear();
    Materials.clear();
}

void DataSystem::RenderForEditer()
{
	ImGui::ContextRegister(ICON_FA_FOLDER_OPEN " Content Browser", true, [&]()
	{
		static file::path DataDirectory = PathFinder::Relative();
		if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Reload", ImVec2(0, 0)))
		{
			ShaderSystem->SetReloading(true);
		}

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
		ImGui::BeginChild("DirectoryHierarchy", ImVec2(200, 0), true);
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

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
		ImGui::SameLine();
		ImGui::BeginChild("FileList", ImVec2(0, 0), true);
		ImGui::PopStyleVar();

		ShowCurrentDirectoryFiles();
		ImGui::PopStyleColor();
		ImGui::EndChild();

	}, ImGuiWindowFlags_None);
}

void DataSystem::MonitorFiles()
{
}

void DataSystem::LoadModels()
{
	file::path shaderpath = PathFinder::Relative("Models\\");
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

Texture* DataSystem::LoadTexture(const std::string_view& filePath)
{
	file::path source = filePath;
	file::path destination = PathFinder::Relative("UI\\") / file::path(filePath).filename();
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


void DataSystem::OpenContentsBrowser()
{
	ImGui::GetContext(ICON_FA_FOLDER_OPEN " Content Browser").Open();
}

void DataSystem::CloseContentsBrowser()
{
	ImGui::GetContext(ICON_FA_FOLDER_OPEN " Content Browser").Close();
}

void DataSystem::ShowDirectoryTree(const file::path& directory)
{
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
			if (ImGui::IsItemClicked())
			{
				currentDirectory = entry.path();
			}
			if (nodeOpen)
			{
				ShowDirectoryTree(entry.path());
				ImGui::TreePop();
			}
		}
	}
}

void DataSystem::ShowCurrentDirectoryFiles()
{

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
			std::string extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            if (extension == ".fbx" || extension == ".gltf" || extension == ".obj" || extension == ".glb" ||
				extension == ".png" || extension == ".dds" || extension == ".hdr" ||
				extension == ".hlsl" || extension == ".cpp" || extension == ".h" || 
				extension == ".cs" || extension == ".wav" || extension == ".mp3")
			{
				ImTextureID iconTexture{};
				FileType fileType = FileType::Unknown;
				if (extension == ".fbx" || extension == ".gltf" || extension == ".obj" || extension == ".glb")
				{
					fileType = FileType::Model;
					iconTexture = (ImTextureID)ModelIcon->m_pSRV;
				}
				else if (extension == ".png" || extension == ".dds" || extension == ".hdr")
				{
					fileType = FileType::Texture;
					iconTexture = (ImTextureID)TextureIcon->m_pSRV;
				}
				else if (extension == ".hlsl")
				{
					fileType = FileType::Shader;
					iconTexture = (ImTextureID)ShaderIcon->m_pSRV;
				}
				else if (extension == ".cpp" || extension == ".h")
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

				DrawFileTile(iconTexture, entry.path(), entry.path().filename().string(), fileType);

				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns(1);
}

void DataSystem::DrawFileTile(ImTextureID iconTexture, const file::path& directory, const std::string& fileName, FileType& fileType, const ImVec2& tileSize)
{
	ImGui::PushID(fileName.c_str());
	ImGui::BeginGroup();
	ImU32 color{};
	if (ImGui::ImageButton(fileName.c_str(), iconTexture, tileSize))
	{

	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
	{
		OpenFile(directory);
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
	ImGui::TextWrapped("%s", FileTypeString[fileType].c_str());
	ImGui::PopFont();

	ImGui::EndGroup();

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload(FileTypeString[fileType].c_str(), fileName.c_str(), fileName.size() + 1);
		ImGui::Text("Dragging %s", fileName.c_str());
		ImGui::EndDragDropSource();
	}

	ImGui::PopID();
}

void DataSystem::OpenFile(const file::path& filepath)
{
	HINSTANCE result = ShellExecute(NULL, L"open", filepath.c_str(), NULL, NULL, SW_SHOWNORMAL);

	if ((int)result <= 32)
	{
		MessageBox(NULL, L"Failed Open File", L"Error", MB_OK | MB_ICONERROR);
	}
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
