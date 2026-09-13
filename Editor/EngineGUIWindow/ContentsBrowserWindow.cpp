#include "ContentsBrowserWindow.h"
#include "EditorTheme.h"
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
#include "EditorIcons.h"
#include <algorithm>
#include <imgui_internal.h>

std::string						ContentsBrowserWindow::selectedFileName{};
std::string						ContentsBrowserWindow::selectedMetaFilePath{};
std::optional<Authoring::WriteDocument>
	ContentsBrowserWindow::selectedFileMetaNode{};

namespace
{
	using FileType = EditorAssetPresentation::FileType;

	using FileTypeCharArr = std::array<std::pair<FileType, const char*>, (size_t)FileType::End>;

	constexpr FileTypeCharArr FileTypeStringTable{ {
		{ FileType::Unknown,        "Unknown"                },
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
        // Filtering and artwork must classify an extension identically.
        return EditorAssetPresentation::Get().ResolveFilePresentation(filepath.extension().string()).type;
	}


}

namespace
{
    // Browser navigation and presentation state live with the browser window.
	ContentsBrowserWindow& content_browser_state()
	{
		static ContentsBrowserWindow state;
		return state;
	}
}

// 생성자가 하던 `open_window` 는 걷었다 — 선언의 `open_by_default` 가 같은
// 일을 하고(entry.open = open_by_default_value), 그쪽이 정본이다. 상태를
// 처음 그릴 때 만드는 지금 구조에서 생성자가 표시 상태를 정하면 순서가
// 거꾸로 돈다 — 열려 있어야 그려지는데 그려져야 열리기 때문이다.
void editor::windows::draw_content_browser()
{
	content_browser_state().Draw();
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

namespace
{
    std::string browser_utf8(const file::path& path)
    {
        const auto utf8 = path.u8string();
        return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
    }

    std::string browser_ellipsis(std::string text, float width)
    {
        if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
        while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > width)
        {
            size_t last = text.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
            text.resize(last);
        }
        return text + "...";
    }

    // Draw inside the tree item's hit rectangle, keeping its ID and interaction
    // state intact for navigation, context menus and prefab drop targets.
    void browser_tree_label(const std::string& name, Texture* texture, const char* fallbackIcon)
    {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        const float iconSize = editor::ThemePixels(16.f);
        const float gap = editor::ThemePixels(4.f);
        const float iconX = minimum.x + ImGui::GetTreeNodeToLabelSpacing();
        const float centerY = (minimum.y + maximum.y) * .5f;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(minimum, maximum, true);
        const auto image = texture ? EditorImGuiTexture::From(texture) : 0;
        if (image)
            draw->AddImage(image, { iconX, centerY - iconSize * .5f },
                { iconX + iconSize, centerY + iconSize * .5f });
        else
        {
            const auto glyph = ImGui::CalcTextSize(fallbackIcon);
            draw->AddText({ iconX + (iconSize - glyph.x) * .5f, centerY - glyph.y * .5f },
                ImGui::GetColorU32(ImGuiCol_Text), fallbackIcon);
        }
        const float labelX = iconX + iconSize + gap;
        const auto label = browser_ellipsis(name, std::max(0.f, maximum.x - labelX - gap));
        draw->AddText({ labelX, centerY - ImGui::GetFontSize() * .5f },
            ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        draw->PopClipRect();
    }

    bool browser_same_path(const file::path& a, const file::path& b)
    {
        std::error_code ec;
        return !a.empty() && !b.empty() && file::equivalent(a, b, ec) && !ec;
    }

    // Draw over the existing item so changing artwork never changes its ID or hit area.
    void browser_item_artwork(Texture* texture, const char* fallbackIcon,
        const std::string& name, bool listView, float tileIconSize = 40.f)
    {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        const float width = maximum.x - minimum.x;
        const float height = maximum.y - minimum.y;
        const float iconSize = listView ? ImGui::GetFontSize() : editor::ThemePixels(tileIconSize);
        const ImVec2 position{minimum.x + (listView ? editor::ThemePixels(6.f) : (width-iconSize)*.5f),
            minimum.y + (height-iconSize)*.5f};
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(minimum, maximum, true);
        const auto image = texture ? EditorImGuiTexture::From(texture) : 0;
        if (image)
            draw->AddImage(image, position, {position.x+iconSize, position.y+iconSize},
                {0,0}, {1,1}, ImGui::GetColorU32(ImVec4(1,1,1,1)));
        else
        {
            auto* font = ImGui::GetFont();
            const auto glyph = font->CalcTextSizeA(iconSize, FLT_MAX, 0.f, fallbackIcon);
            draw->AddText(font, iconSize, {position.x+(iconSize-glyph.x)*.5f, position.y+(iconSize-glyph.y)*.5f},
                ImGui::GetColorU32(ImGuiCol_Text), fallbackIcon);
        }
        if (listView)
            draw->AddText({position.x+iconSize+editor::ThemePixels(8.f), minimum.y+(height-ImGui::GetFontSize())*.5f},
                ImGui::GetColorU32(ImGuiCol_Text), browser_ellipsis(name, width-iconSize-editor::ThemePixels(22.f)).c_str());
        draw->PopClipRect();
    }

    bool browser_icon_button(const char* icon, const char* tip, bool enabled = true, Texture* texture = nullptr)
    {
        ImGui::BeginDisabled(!enabled);
        const std::string label = texture ? std::string("###") + icon : icon;
        const bool clicked = ImGui::Button(label.c_str(), { ImGui::GetFrameHeight(), ImGui::GetFrameHeight() });
        if (texture) browser_item_artwork(texture, icon, {}, false, 16.f);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
        return clicked;
    }
}

void ContentsBrowserWindow::Navigate(const file::path& directory, bool addHistory)
{
    std::error_code ec;
    const auto target = file::canonical(directory, ec);
    if (ec || !file::is_directory(target, ec))
    {
        m_error = "Folder is unavailable: " + browser_utf8(directory);
        return;
    }
    const auto relative = target.lexically_relative(m_rootDirectory);
    if (relative.empty() || *relative.begin() == "..")
    {
        m_error = "Choose a folder inside Assets.";
        return;
    }
    if (target == m_currentDirectory) return;
    m_currentDirectory = target;
    m_filter.Clear();
    selectedFileName.clear();
    selectedMetaFilePath.clear();
    selectedFileMetaNode.reset();
    m_error.clear();
    m_revealDirectory = true;
    if (addHistory)
    {
        if (!m_history.empty()) m_history.resize(m_historyIndex + 1);
        m_history.push_back(target);
        if (m_history.size() > 64) m_history.erase(m_history.begin());
        m_historyIndex = m_history.size() - 1;
    }
}

void ContentsBrowserWindow::DrawFolderMenu(const file::path& directory)
{
    if (ImGui::MenuItem("New Folder..."))
    {
        m_folderTarget = directory;
        m_folderName[0] = 0;
        m_openFolderDialog = true;
        m_error.clear();
    }
    if (browser_same_path(directory, PathFinder::VolumeProfilePath())
        && ImGui::MenuItem("Create Volume Profile..."))
        EditorAssetDatabase::Get().CreateVolumeProfile(directory);
    if (ImGui::MenuItem("Open in File Explorer"))
        EditorPlatform::Get().RevealInFileExplorer(directory);
    if (::editor::popup_host_has_items(::editor::popup_host::content_browser_folder))
    {
        ImGui::Separator();
        ::editor::draw_popup_menu_items<::editor::popup_host::content_browser_folder>(
            ::editor::folder_target{ directory });
    }
}

void ContentsBrowserWindow::DrawFolderDialog()
{
    if (m_openFolderDialog)
    {
        ImGui::OpenPopup("New Folder");
        m_openFolderDialog = false;
    }
    if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(browser_utf8(m_folderTarget).c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", m_folderName, sizeof(m_folderName),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
        if (ImGui::Button("Create") || enter)
        {
            file::path created;
            if (EditorAssetDatabase::Get().CreateFolder(m_folderTarget, m_folderName, created, m_error))
            {
                Navigate(created);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { m_error.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void ContentsBrowserWindow::ShowDirectoryTree(const file::path& directory)
{
    const std::string id = browser_utf8(directory);
    const std::string name = directory == m_rootDirectory ? "Assets" : browser_utf8(directory.filename());
    std::error_code ec;
    std::vector<file::path> children;
    for (file::directory_iterator it(directory, file::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
    {
        if (it->is_directory(ec) && !it->is_symlink(ec)) children.push_back(it->path());
    }
    std::sort(children.begin(), children.end());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_FramePadding;
    if (m_currentDirectory == directory) flags |= ImGuiTreeNodeFlags_Selected;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    const auto currentRelative = m_currentDirectory.lexically_relative(directory);
    if (m_revealDirectory && !currentRelative.empty() && *currentRelative.begin() != "..")
        ImGui::SetNextItemOpen(true);
    const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", "");
    browser_tree_label(name, EditorAssetPresentation::Get().GetDirectoryIcon(open && !children.empty()),
        EditorIcon::Folder);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", id.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Navigate(directory);
    if (browser_same_path(directory, PathFinder::RelativeToPrefab("")) && ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) HandleSceneObjectDrop(payload->Data);
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem())
    {
        DrawFolderMenu(directory);
        ImGui::EndPopup();
    }
    if (open)
    {
        for (const auto& child : children) ShowDirectoryTree(child);
        ImGui::TreePop();
    }
}

void ContentsBrowserWindow::DrawDirectoryPanel()
{
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, editor::ThemePixels(14.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.f, editor::ThemePixels(3.f) });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { editor::ThemePixels(3.f), editor::ThemePixels(1.f) });
    const auto& projectName = EditorSettingsStore::Get().Build().GetProjectName();
    if (m_revealDirectory) ImGui::SetNextItemOpen(true);
    const bool open = ImGui::TreeNodeEx("##ProjectRoot", ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "%s", "");
    browser_tree_label(projectName.empty() ? "Project" : projectName,
        EditorAssetPresentation::Get().GetProjectIcon(), EditorIcon::Game);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", projectName.c_str());
    if (open)
    {
        ShowDirectoryTree(m_rootDirectory);
        ImGui::TreePop();
    }
    ImGui::PopStyleVar(3);
    m_revealDirectory = false;
}

void ContentsBrowserWindow::ShowCurrentDirectoryFiles()
{
    std::error_code ec;
    std::vector<file::directory_entry> entries;
    size_t supported = 0;
    for (file::directory_iterator it(m_currentDirectory, ec), end; !ec && it != end; it.increment(ec))
    {
        const bool folder = it->is_directory(ec);
        if (!folder && !EditorAssetDatabase::Get().IsSupportExtension(it->path().extension().string())) continue;
        ++supported;
        const auto name = browser_utf8(it->path().filename());
        if (!m_filter.PassFilter(name.c_str())) continue;
        if (!folder && m_typeFilter >= 0 && static_cast<int>(DeduceFileType(it->path())) != m_typeFilter) continue;
        entries.push_back(*it);
    }
    if (ec) { ImGui::TextWrapped("Unable to read folder: %s", ec.message().c_str()); return; }
    std::sort(entries.begin(), entries.end(), [this](const auto& a, const auto& b)
    {
        std::error_code aError, bError;
        const bool aFolder = a.is_directory(aError), bFolder = b.is_directory(bError);
        if (aFolder != bFolder) return aFolder;
        return m_sortDescending ? a.path().filename() > b.path().filename() : a.path().filename() < b.path().filename();
    });
    if (entries.empty()) ImGui::TextDisabled(supported == 0 ? "No supported assets or folders." : "No matching assets or folders.");
    const float cell = editor::ThemePixels(m_tileSize + 12.f);
    const int columns = m_listView ? 1 : std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    if (ImGui::BeginTable("AssetGrid", columns, ImGuiTableFlags_SizingStretchSame))
    {
        for (const auto& entry : entries)
        {
            ImGui::TableNextColumn();
            const auto name = browser_utf8(entry.path().filename());
            const auto pathId = browser_utf8(entry.path());
            ImGui::PushID(pathId.c_str());
            if (entry.is_directory(ec))
            {
                const ImVec2 size = m_listView ? ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight())
                    : ImVec2(editor::ThemePixels(m_tileSize), editor::ThemePixels(m_tileSize));
                ImGui::Button("##Folder", size);
                browser_item_artwork(EditorAssetPresentation::Get().GetDirectoryIcon(false), EditorIcon::Folder, name, m_listView);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pathId.c_str());
                if ((ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) Navigate(entry.path());
                if (ImGui::BeginPopupContextItem()) { DrawFolderMenu(entry.path()); ImGui::EndPopup(); }
                if (!m_listView)
                {
                    ImGui::TextUnformatted(browser_ellipsis(name, size.x).c_str());
                    ImGui::TextDisabled("Folder");
                }
            }
            else
            {
                auto presentation = EditorAssetPresentation::Get().ResolveFilePresentation(entry.path().extension().string());
                DrawFileTile(presentation, entry.path(), name,
                    { editor::ThemePixels(m_tileSize), editor::ThemePixels(m_tileSize) });
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::BeginPopupContextWindow("ContentFolderAreaMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        DrawFolderMenu(m_currentDirectory);
        ImGui::EndPopup();
    }
    // Submit a real remaining-area item; never grow the window with cursor movement.
    const ImVec2 remaining = ImGui::GetContentRegionAvail();
    ImGui::Dummy({ std::max(1.f, remaining.x), std::max(1.f, remaining.y) });
    if (browser_same_path(m_currentDirectory, PathFinder::RelativeToPrefab("")) && ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) HandleSceneObjectDrop(payload->Data);
        ImGui::EndDragDropTarget();
    }
}
void ContentsBrowserWindow::DrawFileTile(const EditorAssetPresentation::FilePresentation& presentation,
										 const file::path& directory,
										 const std::string& fileName,
										 const ImVec2& tileSize)
{
	const auto fileType = presentation.type;
	ImGui::PushID(fileName.c_str());
	ImGui::BeginGroup();
    ImU32 color{};
    // Keep one item identity and hit rectangle when W7 replaces this icon with a thumbnail.
    const ImVec2 itemSize = m_listView
        ? ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()) : tileSize;
    const bool pressed = ImGui::InvisibleButton(fileName.c_str(), itemSize, ImGuiButtonFlags_EnableNav);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const bool selected = selectedMetaFilePath == directory.string() + ".meta";
    const ImGuiCol background = selected ? ImGuiCol_Header : ImGui::IsItemActive() ? ImGuiCol_ButtonActive
        : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button;
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(background),
                        ImGui::GetStyle().FrameRounding);
    if (ImGui::IsItemFocused())
        draw->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_NavCursor),
                      ImGui::GetStyle().FrameRounding);
    browser_item_artwork(presentation.type_image, presentation.type_icon, fileName, m_listView);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", browser_utf8(directory).c_str());
    if (pressed)
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
		if (browser_same_path(m_currentDirectory, PathFinder::VolumeProfilePath()))
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

    if (!m_listView)
    {
	ImVec2 pos = ImGui::GetCursorScreenPos();
	std::string typeID{};
	float lineWidth = tileSize.x;
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

	ImGui::Dummy(ImVec2(0, editor::ThemePixels(3.f)));

	ImGui::PushFont(EditorAssetPresentation::Get().GetSmallFont(), 0.0f);
	ImGui::TextUnformatted(browser_ellipsis(fileName, tileSize.x).c_str());
	ImGui::PopFont();
	ImGui::PushFont(EditorAssetPresentation::Get().GetExtraSmallFont(), 0.0f);
	ImGui::TextWrapped("%s", FileTypeToString(fileType));
	ImGui::PopFont();
    }

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


void ContentsBrowserWindow::DrawBreadcrumb(float width)
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float height = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(23, 24, 24, 255));
    ImGui::BeginChild("Breadcrumb", { std::max(1.f, width), height }, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    std::vector<file::path> chain{ m_rootDirectory };
    auto path = m_rootDirectory;
    for (const auto& part : m_currentDirectory.lexically_relative(m_rootDirectory))
    {
        if (part == ".") continue;
        path /= part;
        chain.push_back(path);
    }
    float required = 0.f;
    for (size_t i = 0; i < chain.size(); ++i)
        required += ImGui::CalcTextSize(i == 0 ? "Assets" : browser_utf8(chain[i].filename()).c_str()).x
            + ImGui::GetStyle().FramePadding.x * 2.f
            + (i == 0 ? 0.f : ImGui::CalcTextSize(EditorIcon::Collapse).x + gap * 2.f);
    size_t start = 0;
    if (required > width && chain.size() > 1)
    {
        if (browser_icon_button(EditorIcon::More, "Parent folders")) ImGui::OpenPopup("Parents");
        if (ImGui::BeginPopup("Parents"))
        {
            for (size_t i = 0; i + 1 < chain.size(); ++i)
                if (ImGui::MenuItem(browser_utf8(chain[i]).c_str())) Navigate(chain[i]);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        start = chain.size() - 1;
    }
    for (size_t i = start; i < chain.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const auto name = i == 0 ? std::string("Assets") : browser_utf8(chain[i].filename());
        const auto label = browser_ellipsis(name, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2.f);
        if (ImGui::Button(label.c_str(), { std::min(ImGui::GetContentRegionAvail().x,
            ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f), height })) Navigate(chain[i]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nRight-click to enter or copy a path", browser_utf8(chain[i]).c_str());
        if (ImGui::BeginPopupContextItem("PathActions"))
        {
            if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(browser_utf8(m_currentDirectory).c_str());
            ImGui::SetNextItemWidth(editor::ThemePixels(300.f));
            if (ImGui::InputTextWithHint("##Path", "Assets-relative or absolute path", m_pathInput, sizeof(m_pathInput), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                const auto entered = file::u8path(m_pathInput);
                Navigate(entered.is_absolute() ? entered : m_rootDirectory / entered);
                if (m_error.empty()) ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        if (i + 1 < chain.size())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", EditorIcon::Collapse);
            ImGui::SameLine();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ContentsBrowserWindow::DrawSearch(float width)
{
    ImGui::BeginGroup();
    const float iconWidth = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(1.f, width));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { iconWidth, ImGui::GetStyle().FramePadding.y });
    if (ImGui::InputTextWithHint("##AssetSearch", "Search this folder", m_filter.InputBuf, sizeof(m_filter.InputBuf))) m_filter.Build();
    ImGui::PopStyleVar();
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddText({ minimum.x + editor::ThemePixels(5.f), minimum.y + ImGui::GetStyle().FramePadding.y },
        ImGui::GetColorU32(ImGuiCol_TextDisabled), EditorIcon::Search);
    if (m_filter.IsActive())
    {
        const auto cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({ maximum.x - iconWidth, minimum.y });
        if (ImGui::InvisibleButton("ClearSearch", { iconWidth, maximum.y - minimum.y })) m_filter.Clear();
        draw->AddText({ maximum.x - iconWidth + editor::ThemePixels(5.f), minimum.y + ImGui::GetStyle().FramePadding.y },
            ImGui::GetColorU32(ImGuiCol_Text), EditorIcon::Close);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear search");
        ImGui::SetCursorScreenPos({ cursor.x, cursor.y - ImGui::GetStyle().ItemSpacing.y });
        ImGui::Dummy({ 0.f, 0.f });
    }
    ImGui::EndGroup();
}

void ContentsBrowserWindow::DrawToolbar(bool collapsedTree)
{
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const bool twoRows = width < editor::ThemePixels(570.f);
    if (browser_icon_button(EditorIcon::Folder, collapsedTree ? "Browse folders" : "Hide folder panel", true,
        EditorAssetPresentation::Get().GetDirectoryIcon(false)))
    {
        if (collapsedTree) ImGui::OpenPopup("FolderNavigation");
        else m_showTree = false;
    }
    if (ImGui::BeginPopup("FolderNavigation"))
    {
        if (!m_showTree && ImGui::MenuItem("Show folder panel")) m_showTree = true;
        ImGui::BeginChild("FolderPopupTree", { editor::ThemePixels(240.f), editor::ThemePixels(240.f) });
        DrawDirectoryPanel();
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(EditorIcon::Label<EditorIcon::Add, " New">)) ImGui::OpenPopup("ContentNew");
    if (ImGui::BeginPopup("ContentNew")) { DrawFolderMenu(m_currentDirectory); ImGui::EndPopup(); }
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Back, "Back", m_historyIndex > 0))
    {
        const auto index = m_historyIndex - 1;
        Navigate(m_history[index], false);
        if (m_error.empty()) m_historyIndex = index;
    }
    if (ImGui::BeginPopupContextItem("NavigationHistory"))
    {
        for (size_t i = 0; i < m_history.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::MenuItem(browser_utf8(m_history[i]).c_str(), nullptr, i == m_historyIndex))
            {
                Navigate(m_history[i], false);
                if (m_error.empty()) m_historyIndex = i;
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Forward, "Forward", m_historyIndex + 1 < m_history.size()))
    {
        const auto index = m_historyIndex + 1;
        Navigate(m_history[index], false);
        if (m_error.empty()) m_historyIndex = index;
    }
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Up, "Parent folder", m_currentDirectory != m_rootDirectory)) Navigate(m_currentDirectory.parent_path());
    ImGui::SameLine();
    const float searchWidth = std::clamp(width * .3f, editor::ThemePixels(140.f), editor::ThemePixels(300.f));
    DrawBreadcrumb(ImGui::GetContentRegionAvail().x - (twoRows ? 0.f : searchWidth + height * 2.f + gap * 3.f));
    if (!twoRows) ImGui::SameLine();
    DrawSearch(twoRows ? width - height * 2.f - gap * 2.f : searchWidth);
    ImGui::SameLine();
    if (browser_icon_button(m_listView ? EditorIcon::Menu : EditorIcon::Grid, "Toggle tiles / list")) m_listView = !m_listView;
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Inspector, "Filter and view options")) ImGui::OpenPopup("ContentOptions");
    if (ImGui::BeginPopup("ContentOptions"))
    {
        ImGui::TextDisabled("Asset type");
        if (ImGui::MenuItem("All types", nullptr, m_typeFilter < 0)) m_typeFilter = -1;
        for (const auto& [type, label] : FileTypeStringTable)
            if (ImGui::MenuItem(label, nullptr, m_typeFilter == static_cast<int>(type))) m_typeFilter = static_cast<int>(type);
        ImGui::Separator();
        ImGui::Checkbox("Descending names", &m_sortDescending);
        ImGui::Checkbox("List view", &m_listView);
        ImGui::SliderFloat("Tile size", &m_tileSize, 64.f, 160.f, "%.0f");
        ImGui::Checkbox("Folder panel", &m_showTree);
        if (ImGui::MenuItem("Reset folder width"))
        {
            EditorSettingsStore::Get().Preferences().SetContentTreeWidth(220.f);
            EditorSettingsStore::Get().Save();
            m_showTree = true;
        }
        ImGui::EndPopup();
    }
}

void ContentsBrowserWindow::Draw()
{
    std::error_code ec;
    const auto root = file::weakly_canonical(PathFinder::Relative(), ec);
    if (ec || !file::is_directory(root, ec)) { ImGui::TextDisabled("Assets folder is unavailable."); return; }
    if (root != m_rootDirectory)
    {
        m_rootDirectory = root;
        m_currentDirectory.clear();
        m_history.clear();
        m_historyIndex = 0;
        Navigate(root);
    }
    if (!file::is_directory(m_currentDirectory, ec)) Navigate(root);

    ImGui::PushFont(EditorAssetPresentation::Get().GetSmallFont(), 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { editor::ThemePixels(6.f), editor::ThemePixels(6.f) });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { editor::ThemePixels(3.f), editor::ThemePixels(3.f) });
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(23, 24, 24, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(23, 24, 24, 255));
    const auto size = ImGui::GetContentRegionAvail();
    auto& preferences = EditorSettingsStore::Get().Preferences();
    const float scale = editor::ThemePixels(1.f);
    const float split = editor::ThemePixels(8.f);
    const float contentGap = editor::ThemePixels(10.f);
    const bool treeVisible = m_showTree && size.x >= editor::ThemePixels(560.f);
    if (treeVisible)
    {
        const float treeWidth = std::clamp(editor::ThemePixels(preferences.GetContentTreeWidth()),
            editor::ThemePixels(140.f), size.x - editor::ThemePixels(340.f) - split - contentGap);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(23, 24, 24, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { editor::ThemePixels(2.f), editor::ThemePixels(2.f) });
        ImGui::BeginChild("DirectoryHierarchy", { treeWidth, std::max(1.f, size.y) },
            ImGuiChildFlags_AlwaysUseWindowPadding);
        DrawDirectoryPanel();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SameLine(0.f, 0.f);
        // Like ImGui's SplitterBehavior, flatten adjacent child windows for hit
        // testing so the press still belongs to the splitter at their boundary.
        ImGui::InvisibleButton("DirectorySplitter", { split, std::max(1.f, size.y) },
            ImGuiButtonFlags_EnableNav | ImGuiButtonFlags_FlattenChildren);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride)
            || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to resize. Double-click to reset. Arrow keys adjust width.");
        float preferred = preferences.GetContentTreeWidth();
        if (ImGui::IsItemActivated()) m_treeDragStart = treeWidth / scale;
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivated())
        {
            const float delta = ImGui::GetMouseDragDelta(0).x;
            if (delta != 0.f) preferred = m_treeDragStart + delta / scale;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) preferred = 220.f;
        if (ImGui::IsItemFocused())
        {
            ImGui::SetKeyOwner(ImGuiKey_LeftArrow, ImGui::GetItemID());
            ImGui::SetKeyOwner(ImGuiKey_RightArrow, ImGui::GetItemID());
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) preferred -= 8.f;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) preferred += 8.f;
        }
        preferred = std::clamp(preferred, 140.f, 600.f);
        const bool changed = preferred != preferences.GetContentTreeWidth();
        if (changed) preferences.SetContentTreeWidth(preferred);
        // W3 workspace autosave owns this personal width. Do not write project settings.
        // Keep the 8px grab target, but paint only a 1px line at its center.
        const ImVec2 splitMin = ImGui::GetItemRectMin();
        const ImVec2 splitMax = ImGui::GetItemRectMax();
        const float lineWidth = std::max(1.f, editor::ThemePixels(1.f));
        const float lineLeft = (splitMin.x + splitMax.x - lineWidth) * .5f;
        ImGui::GetWindowDrawList()->AddRectFilled({lineLeft, splitMin.y}, {lineLeft + lineWidth, splitMax.y},
            ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive :
                ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
        ImGui::SameLine(0.f, contentGap);
    }
    ImGui::BeginChild("ContentBody", { 0.f, std::max(1.f, size.y) });
    DrawToolbar(!treeVisible);
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
    if (m_typeFilter >= 0) ImGui::TextDisabled("Type: %s", FileTypeToString(static_cast<FileType>(m_typeFilter)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { editor::ThemePixels(2.f), editor::ThemePixels(2.f) });
    ImGui::BeginChild("FileList", { 0.f, 0.f }, ImGuiChildFlags_AlwaysUseWindowPadding);
    ShowCurrentDirectoryFiles();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    DrawFolderDialog();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
}
