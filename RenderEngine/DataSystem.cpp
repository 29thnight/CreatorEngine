#include "DataSystem.h"
//#include "MaterialLoader.h"
#include "Model.h"	
#include <ppltasks.h>
#include <ppl.h>
#include "Banchmark.hpp"
#include <future>

#include "IconsFontAwesome6.h"
#include "fa.h"

ImGuiTextFilter DataSystem::filter;

bool HasImageFile(const file::path& directory)
{
	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_regular_file()) // �������� Ȯ��
		{
			std::string ext = entry.path().extension().string();
			if (ext == ".png" || ext == ".jpg")
			{
				return true; // �ϳ��� ������ �ٷ� true ��ȯ
			}
		}
	}
	return false; // �̹��� ���� ����
}

DataSystem::~DataSystem()
{
}

void DataSystem::Initialize()
{
	file::path iconpath = PathFinder::IconPath();
	UnknownIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");
	TextureIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");
	ModelIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");
	AssetsIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");
	FolderIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");
	ShaderIcon = Texture::LoadFormPath(iconpath.string() + "Shader.png");
	CodeIcon = Texture::LoadFormPath(iconpath.string() + "Model.png");

	smallFont = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 12.0f);
	extraSmallFont = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 10.0f);

	RenderForEditer();
}

void DataSystem::RenderForEditer()
{
	ImGui::ContextRegister(ICON_FA_FOLDER_OPEN " Content Browser", true, [&]()
	{
		static file::path DataDirectory = PathFinder::Relative();
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
	file::copy_file(source, destination, file::copy_options::update_existing);
	std::string name = file::path(filePath).stem().string();
	if (Models.find(name) != Models.end())
	{
		Debug->Log("ModelLoader::LoadModel : Model already loaded");
		return;
	}
	Model* model = Model::LoadModel(destination.string());
	if (model)
	{
		Models[name] = std::shared_ptr<Model>(model);
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
	}
}

void DataSystem::LoadTextures()
{
}

void DataSystem::LoadMaterials()
{
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
			// Ʈ�� ��� ����: ���� �����̸� �ڽ� ������ ���������� ����.
			bool nodeOpen = ImGui::TreeNodeEx(iconName.c_str(), nodeFlags);
			if (ImGui::IsItemClicked())
			{
				currentDirectory = entry.path(); // ���õ� ���丮 ����
			}
			if (nodeOpen)
			{
				// ���� ���丮 ��� (��� ȣ��)
				ShowDirectoryTree(entry.path());
				ImGui::TreePop();
			}
		}
	}
}

void DataSystem::ShowCurrentDirectoryFiles()
{
	// �� �࿡ ǥ���� Ÿ�� �� (��: 4��)
	float availableWidth = ImGui::GetContentRegionAvail().x;

	// Ÿ���� ���� �ʺ� (���⼭�� ���÷� 100px)
	const float tileWidth = 200.0f;

	// �� �࿡ �� Ÿ�� ������ ��� (�ּ� 1�� �̻�)
	int tileColumns = (int)(availableWidth / tileWidth);
	tileColumns = (tileColumns > 0) ? tileColumns : 1;

	// ����� �� ������ Columns ����
	ImGui::Columns(tileColumns, nullptr, false);

	//const int tileColumns = 4;
	//ImGui::Columns(tileColumns, nullptr, false);
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
			if (extension == ".fbx" || extension == ".gltf" || extension == ".obj" ||
				extension == ".png" || extension == ".dds" || extension == ".hdr" ||
				extension == ".hlsl" || extension == ".cpp" || extension == ".h" || 
				extension == ".cs")
			{
				// ���Ͽ� �´� ������ �ؽ�ó�� �����ϴ� ���� (���÷� nullptr ���)
				ImTextureID iconTexture{};/* ���� ������ ���� �ؽ�ó �ε� */;
				FileType fileType = FileType::Unknown;
				if (extension == ".fbx" || extension == ".gltf" || extension == ".obj")
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

				// ���� �̸�, Ȯ���� ���� ����
				DrawFileTile(iconTexture, entry.path().filename().string(), fileType);

				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns(1);
}

void DataSystem::DrawFileTile(ImTextureID iconTexture, const std::string& fileName, FileType& fileType, const ImVec2& tileSize)
{
	// �� Ÿ�ϸ��� ���� ID�� �ο��Ͽ� DrawList ȣ�� ���� ����
	ImGui::PushID(fileName.c_str());
	ImGui::BeginGroup();

	// �̹��� ������ ��� (Ŭ�� �̺�Ʈ�� �ʿ��� ��� ImageButton ���)
	if (ImGui::ImageButton(fileName.c_str(), iconTexture, tileSize))
	{
		// Ŭ�� �� ó���� ����
	}


	// ������ �ٷ� �Ʒ��� ����� ���� ������ ������ �׸��� ���� ���� ��ũ�� ��ǥ�� ������
	ImVec2 pos = ImGui::GetCursorScreenPos();
	// ���� ���̸� Ÿ�� �ʺ�� �����ϰ� ���� (�ʿ�� tileSize.x ���)
	float lineWidth = tileSize.x + 10;
	// ���� ���� ��ġ�� �� ��ġ ��� (���� ������ y ������)
	ImVec2 lineStart = ImVec2(pos.x, pos.y + 2);
	ImVec2 lineEnd = ImVec2(pos.x + lineWidth, pos.y + 2);
	// ����: ��Ȳ�� ����, �β� 2.0f
	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(255, 165, 0, 255), 2.0f);

	// ������ �׸� �� ���� ���� Ȯ�� (���� ������ŭ Dummy ȣ��)
	ImGui::Dummy(ImVec2(0, 8));

	// ���� �̸��� ���� ���� �ؽ�Ʈ ��� (���ϴ� ��Ÿ�� ���� ����)
	ImGui::PushFont(smallFont);
	ImGui::TextWrapped("%s", fileName.c_str());
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 2));
	ImGui::PushFont(extraSmallFont);
	ImGui::TextWrapped("%s", FileTypeString[fileType].c_str());
	ImGui::PopFont();

	ImGui::EndGroup();
	ImGui::PopID();
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