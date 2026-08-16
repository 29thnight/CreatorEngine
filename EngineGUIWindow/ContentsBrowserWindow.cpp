#include "ContentsBrowserWindow.h"
#ifndef DYNAMICCPP_EXPORTS
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Prefab.h"
#include "PrefabUtility.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include <yaml-cpp/yaml.h>

std::string						ContentsBrowserWindow::selectedFileName{};
std::string						ContentsBrowserWindow::selectedMetaFilePath{};
std::optional<MetaYml::Node>	ContentsBrowserWindow::selectedFileMetaNode{};

namespace
{
	using FileType = DataSystem::FileType;

	using FileTypeCharArr = std::array<std::pair<FileType, const char*>, (size_t)FileType::End>;

	constexpr FileTypeCharArr FileTypeStringTable{ {
		{ FileType::Model,          "Model"				},
		{ FileType::Texture,        "Texture"			},
		{ FileType::MaterialTexture,"MaterialTexture"	},
		{ FileType::TerrainTexture, "TerrainTexture"	},
		{ FileType::Shader,         "Shader"			},
		{ FileType::CppScript,      "CppScript"			},
		{ FileType::CSharpScript,   "CSharpScript"		},
		{ FileType::Prefab,         "Prefab"			},
		{ FileType::Sound,          "Sound"				},
		{ FileType::HDR,            "HDR"				},
		{ FileType::VolumeProfile , "VolumeProfile"		},
		{ FileType::Font,           "Font"				}
	} };

	constexpr const char* FileTypeToString(FileType type)
	{
		// 선형 검색
		for (auto&& kv : FileTypeStringTable)
		{
			if (kv.first == type)
				return kv.second;
		}
		return "Unknown";
	}

	// 이름을 DeduceFileType으로 바꿨다. GetFileType은 Win32 fileapi.h가
	// 이미 쓰는 이름이라, 에디터 TU에서 겹칠 이유를 남기지 않는다.
	FileType DeduceFileType(const file::path& filepath)
	{
		const auto ext = filepath.extension();
		if (ext == ".fbx" || ext == ".gltf" || ext == ".glb")			return FileType::Model;
		else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")		return FileType::Texture;
		else if (ext == ".mat")											return FileType::MaterialTexture;
		else if (ext == ".terrain")										return FileType::TerrainTexture;
		else if (ext == ".hlsl" || ext == ".fx" || ext == ".shader")		return FileType::Shader;
		else if (ext == ".cpp" || ext == ".h")							return FileType::CppScript;
		else if (ext == ".cs")											return FileType::CSharpScript;
		else if (ext == ".wav" || ext == ".mp3")						return FileType::Sound;
		else if (ext == ".hdr")											return FileType::HDR;
		else if (ext == ".prefab")										return FileType::Prefab;
		else if (ext == ".volume")										return FileType::VolumeProfile;
		else if (ext == ".spritefont")									return FileType::Font;
		return FileType::Unknown;
	}

	const std::unordered_map<std::string_view, std::string_view> kExtensionToIcon = {
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

	constexpr const char* kBrowserTitle = ICON_FA_HARD_DRIVE " Content Browser";
}

ContentsBrowserWindow::ContentsBrowserWindow()
{
	ImGui::ContextRegister(kBrowserTitle, true, [&]()
	{
		ImGui::GetContext(kBrowserTitle).SetPopup(Style() == ContentsBrowserStyle::Tile);

		static file::path DataDirectory = PathFinder::Relative();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
		ImGui::BeginDisabled();
		ImGui::Button(ICON_FA_MAGNIFYING_GLASS);
		ImGui::EndDisabled();
		ImGui::SameLine();
		m_filter.Draw("##Assets Search", ImGui::GetContentRegionAvail().x - 90);
		ImGui::PopStyleVar();

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

		if (Style() == ContentsBrowserStyle::Tile)
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
		m_overlayPos = ImGui::GetItemRectMin();
		if (!m_currentDirectory.empty() &&
			std::filesystem::equivalent(m_currentDirectory, PathFinder::RelativeToPrefab("")))
		{
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
				{
					HandleSceneObjectDrop(payload->Data);
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::SetCursorScreenPos(m_overlayPos);

		ShowCurrentDirectoryFiles();
		ImGui::PopStyleColor();
		ImGui::EndChild();

	}, ImGuiWindowFlags_None);

	// 타일 스타일은 하단 서랍이라 접힌 채로, 트리 스타일은 도킹된 창이라
	// 펼친 채로 시작한다. DataSystem::RenderForEditer 끝에 있던 판단이다.
	if (Style() == ContentsBrowserStyle::Tile)
	{
		ImGui::GetContext(kBrowserTitle).Close();
	}
	else
	{
		ImGui::GetContext(kBrowserTitle).Open();
	}
}

void ContentsBrowserWindow::HandleSceneObjectDrop(const void* payload)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (!scene) return;

	const GameObject::Index index = *static_cast<const GameObject::Index*>(payload);
	auto objPtr = scene->GetGameObject(index);
	if (!objPtr) return;

	GameObject* obj = objPtr.get();
	Prefab* prefab = PrefabUtilitys->CreatePrefab(obj, obj->m_name.ToString());
	if (!prefab) return;

	const file::path savePath =
		PathFinder::RelativeToPrefab(obj->m_name.ToString() + ".prefab");
	PrefabUtilitys->SavePrefab(prefab, savePath.string());
	DataSystems->ForceCreateYamlMetaFile(savePath);
	// prefab은 PrefabUtility::m_createdPrefabs가 소유한다(비소유 포인터) — 여기서 지우지 않는다.
}

void ContentsBrowserWindow::ShowDirectoryTree(const file::path& directory)
{
	static file::path MenuDirectory{};
	static bool isRightClicked = false;
	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_directory())
		{
			std::string dirName = entry.path().filename().string();
			ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
			if (m_currentDirectory == entry.path())
				nodeFlags |= ImGuiTreeNodeFlags_Selected;

			std::string iconName = ICON_FA_FOLDER + std::string(" ") + dirName;
			bool nodeOpen = ImGui::TreeNodeEx(iconName.c_str(), nodeFlags);
			if (!entry.path().empty() &&
				std::filesystem::equivalent(entry.path(), PathFinder::RelativeToPrefab("")))
			{
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
					{
						HandleSceneObjectDrop(payload->Data);
					}
					ImGui::EndDragDropTarget();
				}
			}
			if (ImGui::IsItemClicked())
			{
				m_currentDirectory = entry.path();
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

	if (isRightClicked)
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
				DataSystems->CreateVolumeProfile(MenuDirectory);
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			DataSystems->OpenExplorerSelectFile(MenuDirectory);
			MenuDirectory.clear();
		}
		ImGui::EndPopup();
	}
}

void ContentsBrowserWindow::ShowCurrentDirectoryFiles()
{
	if (Style() == ContentsBrowserStyle::Tile)
	{
		ShowCurrentDirectoryFilesTile();
	}
	else
	{
		m_currentDirectory = PathFinder::Relative();

		ShowCurrentDirectoryFilesTree(m_currentDirectory);
	}
}

void ContentsBrowserWindow::ShowCurrentDirectoryFilesTile()
{
	float availableWidth = ImGui::GetContentRegionAvail().x;

	const float tileWidth = 200.0f;

	int tileColumns = (int)(availableWidth / tileWidth);
	tileColumns = (tileColumns > 0) ? tileColumns : 1;

	ImGui::Columns(tileColumns, nullptr, false);

	if (m_currentDirectory.empty())
	{
		m_currentDirectory = PathFinder::Relative();
	}

	for (const auto& entry : file::directory_iterator(m_currentDirectory))
	{
		if (entry.is_regular_file())
		{
			if (m_filter.IsActive() && !m_filter.PassFilter(entry.path().filename().string().c_str()))
				continue;

			std::string extension = entry.path().extension().string();
			if (DataSystems->IsSupportExtension(extension))
			{
				ImTextureID iconTexture{};
				FileType fileType = FileType::Unknown;
				const auto& extensionMap = DataSystems->kExtensionMap;
				if (auto it = extensionMap.find(extension); it != extensionMap.end())
				{
					fileType = it->second.type;
					iconTexture = (ImTextureID)EditorImGuiTexture::From(it->second.icon);
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
		if (!m_currentDirectory.empty() &&
			std::filesystem::equivalent(m_currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				DataSystems->CreateVolumeProfile(m_currentDirectory);
			}
		}
		ImGui::EndPopup();
	}
}

void ContentsBrowserWindow::ShowCurrentDirectoryFilesTree(const file::path& directory)
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

			if (ImGui::TreeNode(label.c_str()))
			{
				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
				{
					currentDirectory = entry.path();
					selectedFileType = DeduceFileType(entry.path());
					isRightClicked = true;
				}
				if (!entry.path().empty() &&
					std::filesystem::equivalent(entry.path(), PathFinder::RelativeToPrefab("")))
				{
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
						{
							HandleSceneObjectDrop(payload->Data);
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
			if (m_filter.IsActive() && !m_filter.PassFilter(entry.path().filename().string().c_str()))
				continue;

			std::string extension = entry.path().extension().string();
			if (DataSystems->IsSupportExtension(extension))
			{
				std::string label = entry.path().filename().string();
				std::string apliedIcon;

				if (auto it = kExtensionToIcon.find(extension); it != kExtensionToIcon.end())
				{
					apliedIcon = it->second;
				}

				label = apliedIcon + label;

				ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				{
					DataSystems->OpenFile(entry.path());
				}
				else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
				{
					currentDirectory = entry.path();
					selectedFileType = DeduceFileType(entry.path());
					isHoverAndClicked = true;
				}
				else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
				{
					currentDirectory = entry.path();
					selectedFileType = DeduceFileType(entry.path());
					isRightClicked = true;
				}

				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				{
					auto find = entry.path().parent_path();
					if (find == PathFinder::Relative("SpriteSheets"))
					{
						ImGui::SetDragDropPayload("SPRITESHEET", entry.path().string().c_str(), entry.path().string().size() + 1);
					}
					else if (find == PathFinder::Relative("UI"))
					{
						ImGui::SetDragDropPayload("UI_TEXTURE", entry.path().string().c_str(), entry.path().string().size() + 1);
					}
					else
					{
						ImGui::SetDragDropPayload(FileTypeToString(selectedFileType), entry.path().string().c_str(), entry.path().string().size() + 1);
					}
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
				DataSystems->CreateVolumeProfile(currentDirectory);
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
				}
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			DataSystems->OpenExplorerSelectFile(currentDirectory);
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

void ContentsBrowserWindow::DrawFileTile(ImTextureID iconTexture,
										 const file::path& directory,
										 const std::string& fileName,
										 DataSystem::FileType& fileType,
										 const ImVec2& tileSize)
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
		DataSystems->OpenFile(directory);
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
				}
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			DataSystems->OpenExplorerSelectFile(directory);
		}
		if (m_currentDirectory.empty() && std::filesystem::equivalent(m_currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				DataSystems->CreateVolumeProfile(m_currentDirectory);
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

	ImGui::PushFont(DataSystems->GetSmallFont());
	ImGui::TextWrapped("%s", fileName.c_str());
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 2));
	ImGui::PushFont(DataSystems->GetExtraSmallFont());
	ImGui::TextWrapped("%s", FileTypeToString(fileType));
	ImGui::PopFont();

	ImGui::EndGroup();

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		if (directory.parent_path() == PathFinder::Relative("SpriteSheets"))
		{
			ImGui::SetDragDropPayload("SPRITESHEET", directory.string().c_str(), directory.string().size() + 1);
		}
		else if (directory.parent_path() == PathFinder::Relative("UI"))
		{
			ImGui::SetDragDropPayload("UI_TEXTURE", directory.string().c_str(), directory.string().size() + 1);
		}
		else
		{
			ImGui::SetDragDropPayload(FileTypeToString(fileType), fileName.c_str(), fileName.size() + 1);
		}
		ImGui::Text("Dragging %s", fileName.c_str());
		ImGui::EndDragDropSource();
	}

	ImGui::PopID();
}
#endif // !DYNAMICCPP_EXPORTS
