#include "ContentsBrowserWindow.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "EditorMenuDraw.h"
#include "Windows/EditorStandardWindows.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Entity.h"
#include "Prefab.h"
#include "PrefabUtility.h"
#include "EditorImGuiTexture.h"
#include "EditorPlatform.h"
#include "EditorAssetDatabase.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

std::string						ContentsBrowserWindow::selectedFileName{};
std::string						ContentsBrowserWindow::selectedMetaFilePath{};
std::optional<Authoring::WriteDocument>
	ContentsBrowserWindow::selectedFileMetaNode{};

namespace
{
	using FileType = EditorAssetPresentation::FileType;

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
		else if (ext == ".hlsl" || ext == ".fx" || ext == ".shadermeta"
			|| ext == ".shader")									return FileType::Shader;
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
		{ ".shadermeta", ICON_FA_FILE_CONTRACT " " },
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

}

ContentsBrowserWindow::ContentsBrowserWindow()
{
	// PHASE 21 M4 2단계: 프레임은 셸이 연다. 본문 첫 줄에서 매 프레임
	// `SetPopup` 으로 하던 판단은 선언의 `closable_when` 술어로 갔다가, 서랍
	// 스타일이 사라지면서 술어 자체가 없어졌다 — 이 창은 이제 닫히지 않는
	// 도킹 패널 하나다.
	editor::windows::bind_window_body(EditorWindowName::kContentBrowser, [&]()
	{
		static file::path DataDirectory = PathFinder::Relative();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
		ImGui::BeginDisabled();
		ImGui::Button(ICON_FA_MAGNIFYING_GLASS);
		ImGui::EndDisabled();
		ImGui::SameLine();
		m_filter.Draw("##Assets Search", ImGui::GetContentRegionAvail().x - 90);
		ImGui::PopStyleVar();

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

		// 왼쪽 디렉터리 트리와 오른쪽 자산 격자. 참조로 삼은 S&Box Asset
		// Browser와 같은 배치이고, 예전에는 이 둘이 스타일 설정에 따라
		// 켜지고 꺼졌다 — 그 분기를 걷었다.
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

	});

	// 도킹된 패널이라 펼친 채로 시작한다. 예전에는 스타일 설정이 하단 서랍
	// (접힌 채 시작)과 도킹 패널 둘로 갈랐는데, 서랍 쪽을 걷었다.
	editor::open_window(EditorWindowName::kContentBrowser);
}

void ContentsBrowserWindow::HandleSceneObjectDrop(const void* payload)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (!scene) return;

	const Entity::Index index = *static_cast<const Entity::Index*>(payload);
	auto objPtr = scene->GetEntity(index);
	if (!objPtr) return;

	Entity* obj = objPtr;
	Prefab* prefab = PrefabUtilitys->CreatePrefab(obj, obj->m_name.ToString());
	if (!prefab) return;

	const file::path savePath =
		PathFinder::RelativeToPrefab(obj->m_name.ToString() + ".prefab");
	PrefabUtilitys->SavePrefab(prefab, savePath.string());
	EditorAssetDatabase::Get().CreateMeta(savePath);
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
		ImGui::OpenPopup("ContentFolderTreeMenu");
		isRightClicked = false;
	}

	if (ImGui::BeginPopup("ContentFolderTreeMenu"))
	{
		if (MenuDirectory.empty() && std::filesystem::equivalent(MenuDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				EditorAssetDatabase::Get().CreateVolumeProfile(MenuDirectory);
			}
		}
		if (ImGui::MenuItem("Open Save Directory"))
		{
			EditorPlatform::Get().RevealInFileExplorer(MenuDirectory);
			MenuDirectory.clear();
		}

		// 선언된 폴더 팝업 항목(PHASE 21 M1). 항목 0 이면 구분선도 없다(A.6).
		if (::editor::popup_host_has_items(::editor::popup_host::content_browser_folder))
		{
			ImGui::Separator();
			::editor::draw_popup_menu_items<::editor::popup_host::content_browser_folder>(
				::editor::folder_target{ MenuDirectory });
		}
		ImGui::EndPopup();
	}
}

void ContentsBrowserWindow::ShowCurrentDirectoryFiles()
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
			if (EditorAssetDatabase::Get().IsSupportExtension(extension))
			{
				ImTextureID iconTexture{};
				FileType fileType = FileType::Unknown;
				const auto filePresentation =
					EditorAssetPresentation::Get().ResolveFilePresentation(extension);
				fileType = filePresentation.type;
				iconTexture = (ImTextureID)EditorImGuiTexture::From(filePresentation.icon);

				DrawFileTile(iconTexture, entry.path(), entry.path().filename().string(), fileType);

				ImGui::NextColumn();
			}
		}
	}
	ImGui::Columns(1);

	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("ContentFolderAreaMenu");
	}

	if (ImGui::BeginPopup("ContentFolderAreaMenu"))
	{
		if (!m_currentDirectory.empty() &&
			std::filesystem::equivalent(m_currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				EditorAssetDatabase::Get().CreateVolumeProfile(m_currentDirectory);
			}
		}

		// 선언된 폴더 팝업 항목(PHASE 21 M1) — 타일 빈 영역 쪽. 문맥은 지금 보고
		// 있는 폴더다. 항목 0 이면 구분선조차 넣지 않는다(A.6).
		if (::editor::popup_host_has_items(::editor::popup_host::content_browser_folder))
		{
			ImGui::Separator();
			::editor::draw_popup_menu_items<::editor::popup_host::content_browser_folder>(
				::editor::folder_target{ m_currentDirectory });
		}
		ImGui::EndPopup();
	}
}

void ContentsBrowserWindow::DrawFileTile(ImTextureID iconTexture,
										 const file::path& directory,
										 const std::string& fileName,
										 EditorAssetPresentation::FileType& fileType,
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

		std::string parseError;
		selectedFileMetaNode = Authoring::WriteDocument::ParseFile(
			selectedMetaFilePath, &parseError);
		if (!selectedFileMetaNode)
		{
			Debug->LogError(parseError);
		}
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
	{
		EditorPlatform::Get().OpenFile(directory);
	}
	else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("ContentAssetTileMenu");
	}

	// 이름이 "Context Menu" 였다 — 트리 쪽과 같은 이름이라 둘이 서로의 팝업을
	// 집을 수 있었다. 호스트별로 갈랐다(PHASE 21 M1).
	if (ImGui::BeginPopup("ContentAssetTileMenu"))
	{
		// Delete 인라인 구현이 여기 있었다 — 트리 쪽과 **같은 코드의 복제**였고,
		// 둘 다 확인도 Undo 도 없이 file::remove 를 불렀다. 선언 하나
		// (editor_core_menus 의 delete_asset, confirm 붙음)가 둘을 대체한다.
		if (ImGui::MenuItem("Open Save Directory"))
		{
			EditorPlatform::Get().RevealInFileExplorer(directory);
		}
		if (m_currentDirectory.empty() && std::filesystem::equivalent(m_currentDirectory, PathFinder::VolumeProfilePath()))
		{
			if (ImGui::MenuItem("Create Volume Profile"))
			{
				EditorAssetDatabase::Get().CreateVolumeProfile(m_currentDirectory);
			}
		}

		// 선언된 자산 팝업 항목(PHASE 21 M1). 문맥은 이 타일이 가리키는 파일이다.
		if (::editor::popup_host_has_items(::editor::popup_host::content_browser_asset))
		{
			ImGui::Separator();
			::editor::draw_popup_menu_items<::editor::popup_host::content_browser_asset>(
				::editor::asset_target{ directory });
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

	ImGui::PushFont(EditorAssetPresentation::Get().GetSmallFont());
	ImGui::TextWrapped("%s", fileName.c_str());
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 2));
	ImGui::PushFont(EditorAssetPresentation::Get().GetExtraSmallFont());
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
