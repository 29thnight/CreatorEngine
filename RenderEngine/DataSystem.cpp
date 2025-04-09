#include "DataSystem.h"
//#include "MaterialLoader.h"
#include "Model.h"	
#include <ppltasks.h>
#include <ppl.h>
#include "Benchmark.hpp"
#include <future>

#include "IconsFontAwesome6.h"
#include "fa.h"

ImGuiTextFilter DataSystem::filter;

bool HasImageFile(const file::path& directory)
{
	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_regular_file()) // 파일인지 확인
		{
			std::string ext = entry.path().extension().string();
			if (ext == ".png" || ext == ".jpg")
			{
				return true; // 하나라도 있으면 바로 true 반환
			}
		}
	}
	return false; // 이미지 파일 없음
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
		Models[name] = std::shared_ptr<Model>(model);
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

	Model* model = Model::LoadModel(destination.string());
	if (model)
	{
		Models[name] = std::shared_ptr<Model>(model);
		return model;
	}
	else
	{
		Debug->LogError("ModelLoader::LoadModel : Model file not found");
		return nullptr;
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
			// 트리 노드 생성: 닫힌 상태이면 자식 노드들은 렌더링되지 않음.
			bool nodeOpen = ImGui::TreeNodeEx(iconName.c_str(), nodeFlags);
			if (ImGui::IsItemClicked())
			{
				currentDirectory = entry.path(); // 선택된 디렉토리 갱신
			}
			if (nodeOpen)
			{
				// 하위 디렉토리 출력 (재귀 호출)
				ShowDirectoryTree(entry.path());
				ImGui::TreePop();
			}
		}
	}
}

void DataSystem::ShowCurrentDirectoryFiles()
{
	// 한 행에 표시할 타일 수 (예: 4개)
	float availableWidth = ImGui::GetContentRegionAvail().x;

	// 타일의 고정 너비 (여기서는 예시로 100px)
	const float tileWidth = 200.0f;

	// 한 행에 들어갈 타일 개수를 계산 (최소 1개 이상)
	int tileColumns = (int)(availableWidth / tileWidth);
	tileColumns = (tileColumns > 0) ? tileColumns : 1;

	// 계산한 열 개수로 Columns 생성
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
				// 파일에 맞는 아이콘 텍스처를 결정하는 로직 (예시로 nullptr 사용)
				ImTextureID iconTexture{};/* 파일 종류에 따른 텍스처 로드 */;
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

				// 파일 이름, 확장자 등을 전달
				DrawFileTile(iconTexture, entry.path().filename().string(), fileType);

				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns(1);
}

void DataSystem::DrawFileTile(ImTextureID iconTexture, const std::string& fileName, FileType& fileType, const ImVec2& tileSize)
{
	// 각 타일마다 고유 ID를 부여하여 DrawList 호출 간섭 방지
	ImGui::PushID(fileName.c_str());
	ImGui::BeginGroup();

	// 이미지 아이콘 출력 (클릭 이벤트가 필요한 경우 ImageButton 사용)
	if (ImGui::ImageButton(fileName.c_str(), iconTexture, tileSize))
	{
		// 클릭 시 처리할 내용
	}


	// 아이콘 바로 아래에 사용자 정의 색상의 라인을 그리기 위해 현재 스크린 좌표를 가져옴
	ImVec2 pos = ImGui::GetCursorScreenPos();
	// 라인 길이를 타일 너비와 동일하게 설정 (필요시 tileSize.x 사용)
	float lineWidth = tileSize.x + 10;
	// 라인 시작 위치와 끝 위치 계산 (조정 가능한 y 오프셋)
	ImVec2 lineStart = ImVec2(pos.x, pos.y + 2);
	ImVec2 lineEnd = ImVec2(pos.x + lineWidth, pos.y + 2);
	// 예시: 주황색 라인, 두께 2.0f

	switch (fileType)
	{
	case FileType::Model:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(255, 165, 0, 255), 2.0f);
		break;
	case FileType::Texture:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(0, 255, 0, 255), 2.0f);
		break;
	case FileType::Shader:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(0, 0, 255, 255), 2.0f);
		break;
	case FileType::CppScript:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(255, 0, 0, 255), 2.0f);
		break;
	case FileType::CSharpScript:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(255, 0, 255, 255), 2.0f);
		break;
	case FileType::Sound:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(255, 255, 0, 255), 2.0f);
		break;
	case FileType::Unknown:
		ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32(128, 128, 128, 255), 2.0f);
		break;
	}

	// 라인을 그린 후 일정 공간 확보 (라인 영역만큼 Dummy 호출)
	ImGui::Dummy(ImVec2(0, 8));

	// 파일 이름과 파일 종류 텍스트 출력 (원하는 스타일 적용 가능)
	ImGui::PushFont(smallFont);
	ImGui::TextWrapped("%s", fileName.c_str());
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 2));
	ImGui::PushFont(extraSmallFont);
	ImGui::TextWrapped("%s", FileTypeString[fileType].c_str());
	ImGui::PopFont();

	ImGui::EndGroup();

	// 드래그 드랍 소스 설정
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		// 파일 경로를 payload로 설정 ("MODEL_RESOURCE" 라는 타입을 지정)
		ImGui::SetDragDropPayload("MODEL_RESOURCE", fileName.c_str(), fileName.size() + 1);
		ImGui::Text("Dragging %s", fileName.c_str());
		ImGui::EndDragDropSource();
	}

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
