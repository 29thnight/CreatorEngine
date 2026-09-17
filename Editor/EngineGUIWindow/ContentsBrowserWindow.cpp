#include "ContentsBrowserWindow.h"
#include "EditorPanelCost.h"
#include "BrowserDirectorySnapshot.h"
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
#include "BrowserThumbnailCache.h"
#include "EditorPlatform.h"
#include "EditorAssetDatabase.h"
#include "EditorIcons.h"
#include "EditorAssetDragPayload.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
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
	// W7-1: 프리팹 파일이 생긴다. 스냅샷을 버려 다음 프레임에 보이게 한다.
	struct invalidate_on_exit { ~invalidate_on_exit() { editor::browser_cache_invalidate(); } } invalidateScope;
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

    // W7-5: 이 함수는 `std::filesystem::equivalent` 였다 — 경로를 **실제로 열어**
    // 파일 식별자를 비교하므로 트리 노드마다 부르면 프레임당 파일 핸들이 수십 개
    // 열린다. 그것이 "스캔은 0 인데 비쌌다" 의 정체였다.
    //
    // 지금은 디스크를 만지지 않는 어휘 비교다. 이 물음("이 폴더가 프리팹 폴더인가")
    // 에는 그것으로 충분하다 — 양쪽 다 같은 `PathFinder` 뿌리에서 나오고, 트리는
    // 심볼릭 링크를 타지 않으므로 `equivalent` 의 링크 의미론이 필요 없다.
    bool browser_same_path(const file::path& a, const file::path& b)
    {
        return ::editor::browser_same_directory(a, b);
    }

    /// W7 썸네일을 만들 해상도(정사각 한 변). 타일 보기와 목록 보기가 **같은
    /// 값을 쓴다** — 보기마다 다른 해상도를 요청하면 같은 자산에 키가 둘이 생겨
    /// 같은 그림을 두 번 디코드한다. 그리는 크기는 그릴 때 정한다.
    constexpr std::uint16_t kThumbnailResolution = 128;

    // Draw over the existing item so changing artwork never changes its ID or hit area.
    //
    // W7 썸네일: `badgeIcon` 은 그림 오른쪽 아래의 **작은 유형 기호**다. 썸네일이
    // 붙은 타일에서만 준다 — 계약이 *"실제 에셋 썸네일 + 작은 유형 배지"* 로
    // 적었고, 그림만 남으면 그 파일이 무엇인지가 사라지기 때문이다.
    void browser_item_artwork(Texture* texture, const char* fallbackIcon,
        const std::string& name, bool listView, float tileIconSize = 40.f,
        const char* badgeIcon = nullptr)
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
        if (badgeIcon && image)
        {
            // 그림의 오른쪽 아래 모서리. 어두운 판을 깔아 밝은 썸네일 위에서도
            // 기호가 읽히게 한다.
            const float badgeSize = iconSize * .38f;
            const ImVec2 badgeMin{ position.x + iconSize - badgeSize,
                                   position.y + iconSize - badgeSize };
            draw->AddRectFilled(badgeMin, { badgeMin.x + badgeSize, badgeMin.y + badgeSize },
                ImGui::GetColorU32(ImGuiCol_WindowBg, .82f), badgeSize * .25f);
            auto* badgeFont = ImGui::GetFont();
            const auto badgeGlyph = badgeFont->CalcTextSizeA(badgeSize, FLT_MAX, 0.f, badgeIcon);
            draw->AddText(badgeFont, badgeSize,
                { badgeMin.x + (badgeSize - badgeGlyph.x) * .5f,
                  badgeMin.y + (badgeSize - badgeGlyph.y) * .5f },
                ImGui::GetColorU32(ImGuiCol_Text), badgeIcon);
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

// ══ PHASE 21 W2-B — 탐색 상태 ════════════════════════════════════════════════
//
// 착수 전의 이력은 `std::vector<file::path>` 였다. 경로만 들고 있어서 뒤로 가면
// 그 자리에서 찾던 검색어도, 고른 자산도, 내려 둔 스크롤도 사라졌다 — 계획서
// 계약 4 가 요구한 *"해당 방문의 검색어·스크롤·선택을 복원한다"* 를 담을 칸이
// 없었다. 그리고 최근·전체라는 가상 위치는 경로가 아니라서 들어갈 자리도 없었다.
//
// 그래서 이력의 한 칸을 **방문**으로 바꿨다. 모든 이동은 네 함수만 지난다:
//   `Navigate`(폴더) · `NavigateScope`(가상 위치) → `PushVisit`
//   `JumpHistory`(뒤로·앞으로·이력 메뉴)
// 그리고 둘 다 떠나기 전에 `CaptureVisit` 로 지금 방문을 적는다.

namespace
{
    constexpr size_t kBrowserRequestCapacity = 32;
    constexpr size_t kHistoryCapacity = 64;
    constexpr size_t kRecentCapacity = 32;
    constexpr size_t kPublishedResultCapacity = 256;
    /// 전체 자산·최근 항목이 프레임당 **새로** 훑을 수 있는 폴더 수. 캐시에 없는
    /// 폴더를 한 프레임에 전부 물으면 W7-1 이 없앤 스캔 스파이크가 돌아온다.
    constexpr int kWarmBudget = 2;

    std::mutex g_browserMailboxMutex;
    std::vector<editor::windows::content_browser_request> g_browserRequests;
    editor::windows::content_browser_snapshot g_browserSnapshot;

    /// 캐시에 있으면 그것을(낡았으면 공유 예산 안에서 다시 훑는다), 없으면
    /// `budget` 이 남았을 때만 처음 훑는다. 둘 다 아니면 nullptr — 다음 프레임에 온다.
    const editor::browser_directory_listing* browser_warm_listing(const file::path& folder, int& budget)
    {
        if (editor::browser_cache_peek(folder)) return &editor::browser_cache_listing(folder);
        if (budget <= 0) return nullptr;
        --budget;
        return &editor::browser_cache_listing(folder);
    }
}

namespace editor::windows
{
    bool request_content_browser(content_browser_request request)
    {
        std::lock_guard lock(g_browserMailboxMutex);
        if (g_browserRequests.size() >= kBrowserRequestCapacity) return false;
        g_browserRequests.push_back(std::move(request));
        return true;
    }

    content_browser_snapshot read_content_browser()
    {
        std::lock_guard lock(g_browserMailboxMutex);
        return g_browserSnapshot;
    }

    const char* content_browser_scope_name(content_browser_scope scope)
    {
        switch (scope)
        {
        case content_browser_scope::recent:     return "recent";
        case content_browser_scope::everything: return "everything";
        default:                                return "folder";
        }
    }
}

std::string ContentsBrowserWindow::RelativeUtf8(const file::path& path) const
{
    if (path.empty()) return {};
    const auto relative = path.lexically_relative(m_rootDirectory);
    if (relative.empty() || relative == ".") return ".";
    const auto utf8 = relative.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

bool ContentsBrowserWindow::Navigate(const file::path& directory)
{
    std::error_code ec;
    const auto target = file::canonical(directory, ec);
    if (ec || !file::is_directory(target, ec))
    {
        m_error = "Folder is unavailable: " + browser_utf8(directory);
        return false;
    }
    const auto relative = target.lexically_relative(m_rootDirectory);
    if (relative.empty() || *relative.begin() == "..")
    {
        m_error = "Choose a folder inside Assets.";
        return false;
    }
    // 같은 위치로의 이동은 이력을 늘리지 않는다(계약 4).
    if (m_scope == Scope::folder && target == m_currentDirectory) return true;
    Visit visit;
    visit.scope = Scope::folder;
    visit.directory = target;
    PushVisit(std::move(visit));
    return true;
}

void ContentsBrowserWindow::NavigateScope(Scope scope)
{
    if (scope == Scope::folder || scope == m_scope) return;
    Visit visit;
    visit.scope = scope;
    PushVisit(std::move(visit));
}

void ContentsBrowserWindow::PushVisit(Visit visit)
{
    CaptureVisit();
    // 뒤로 간 뒤 새로 이동하면 앞으로 이력을 버린다(계약 4).
    if (!m_history.empty()) m_history.resize(m_historyIndex + 1);
    m_history.push_back(std::move(visit));
    if (m_history.size() > kHistoryCapacity) m_history.erase(m_history.begin());
    m_historyIndex = m_history.size() - 1;
    // 새 방문은 검색어가 비어 있다 — 정상 이동이 이전 검색어로 내용을 숨기지 않는다.
    RestoreVisit(m_history[m_historyIndex]);
}

void ContentsBrowserWindow::CaptureVisit()
{
    if (m_historyIndex >= m_history.size()) return;
    Visit& visit = m_history[m_historyIndex];
    visit.search = m_filter.InputBuf;
    visit.selected = m_selectedPath;
    visit.scrollY = m_fileListScrollY;
}

void ContentsBrowserWindow::RestoreVisit(const Visit& visit)
{
    m_scope = visit.scope;
    if (visit.scope == Scope::folder)
    {
        m_currentDirectory = visit.directory;
        m_revealDirectory = true;
    }
    const size_t length = std::min(visit.search.size(), sizeof(m_filter.InputBuf) - 1);
    std::memcpy(m_filter.InputBuf, visit.search.data(), length);
    m_filter.InputBuf[length] = 0;
    m_filter.Build();
    m_error.clear();
    m_fileListScrollY = 0.f;
    m_pendingScrollY = visit.scrollY;

    // 선택은 **지금도 있을 때만** 되살린다(계약 6). 있는지는 부모 폴더의 캐시
    // 목록으로 묻는다 — 뒤로 가기 한 번에 디스크를 새로 만지지 않는다. 목록이
    // 캐시에 없으면 확인할 수 없으므로 되살리지 않는다(거짓 선택보다 빈 선택이 낫다).
    ClearSelection();
    if (visit.selected.empty()) return;
    const auto* listing = editor::browser_cache_peek(visit.selected.parent_path());
    if (!listing || !listing->valid) return;
    const std::string name = browser_utf8(visit.selected.filename());
    const bool exists = std::any_of(listing->entries.begin(), listing->entries.end(),
        [&](const editor::browser_directory_entry& entry)
        { return !entry.isDirectory && entry.nameUtf8 == name; });
    if (exists) SelectAsset(visit.selected);
}

void ContentsBrowserWindow::JumpHistory(size_t index)
{
    if (index >= m_history.size() || index == m_historyIndex) return;
    CaptureVisit();
    Visit target = m_history[index];
    std::string recovered;
    if (target.scope == Scope::folder)
    {
        // 지워진 위치는 뿌리 안의 가장 가까운 상위로 복구한다(계약 4). 디스크를
        // 묻는 것은 사람이 누른 이 한 번뿐이다.
        std::error_code ec;
        file::path folder = target.directory;
        while (!(file::is_directory(folder, ec) && !ec))
        {
            ec.clear();
            const auto relative = folder.lexically_relative(m_rootDirectory);
            if (relative.empty() || relative == "." || *relative.begin() == "..")
            {
                folder = m_rootDirectory;
                break;
            }
            folder = folder.parent_path();
        }
        if (folder != target.directory)
        {
            recovered = "That folder no longer exists. Showing " + RelativeUtf8(folder) + ".";
            target.directory = folder;
            target.search.clear();
            target.selected.clear();
            target.scrollY = 0.f;
            m_history[index] = target;
        }
    }
    m_historyIndex = index;
    RestoreVisit(target);
    if (!recovered.empty()) m_error = std::move(recovered);
}

bool ContentsBrowserWindow::SelectAsset(const file::path& path)
{
    selectedMetaFilePath = path.string();
    selectedMetaFilePath += ".meta";
    selectedFileName = browser_utf8(path.filename());
    m_selectedPath = path;

    std::string parseError;
    selectedFileMetaNode = Authoring::WriteDocument::ParseFile(selectedMetaFilePath, &parseError);
    if (!selectedFileMetaNode)
    {
        Debug::PrintLog(spdlog::level::err, parseError);
        return false;
    }
    // 인스펙터에 편집할 수 있게 올라갔다 — 계약 5 의 *"실제 열기/편집에 성공한
    // 자산"* 이다. hover 는 여기를 지나지 않는다.
    RecordRecent(path);
    return true;
}

void ContentsBrowserWindow::ClearSelection()
{
    selectedFileName.clear();
    selectedMetaFilePath.clear();
    selectedFileMetaNode.reset();
    m_selectedPath.clear();
}

// ── 최근 항목 ────────────────────────────────────────────────────────────────
//
// 프로젝트마다 다르고 사람마다 다르다. 그래서 워크스페이스 파일(배치)이 아니라
// 프로젝트의 `Library/` 에 둔다 — 이미 git 이 무시하는 파생·개인 상태 자리다.
// 뿌리 기준 상대 경로로 적어 프로젝트를 옮겨도 산다. 쓰는 것은 목록이 **바뀐
// 순간**뿐이다(프레임마다 쓰지 않는다).

void ContentsBrowserWindow::RecordRecent(const file::path& path)
{
    const auto existing = std::find(m_recents.begin(), m_recents.end(), path);
    if (existing == m_recents.begin() && !m_recents.empty()) return;
    if (existing != m_recents.end()) m_recents.erase(existing);
    m_recents.insert(m_recents.begin(), path);
    if (m_recents.size() > kRecentCapacity) m_recents.resize(kRecentCapacity);
    SaveRecents();
}

void ContentsBrowserWindow::LoadRecents()
{
    m_recents.clear();
    m_recentsFile = m_rootDirectory.parent_path() / "Library" / "EditorState" / "ContentBrowserRecents.txt";
    std::ifstream input(m_recentsFile, std::ios::binary);
    std::string line;
    while (input && std::getline(input, line) && m_recents.size() < kRecentCapacity)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const file::path relative = file::u8path(line);
        // 뿌리 밖을 가리키는 줄은 받지 않는다 — 손으로 고친 파일이 두 번째 자산
        // 뿌리를 만들지 못하게 한다(계약 3).
        if (relative.is_absolute() || (!relative.empty() && *relative.begin() == "..")) continue;
        m_recents.push_back((m_rootDirectory / relative).lexically_normal());
    }
}

void ContentsBrowserWindow::SaveRecents() const
{
    if (m_recentsFile.empty()) return;
    std::error_code ec;
    file::create_directories(m_recentsFile.parent_path(), ec);
    std::ofstream output(m_recentsFile, std::ios::binary | std::ios::trunc);
    if (!output) return;
    for (const auto& path : m_recents) output << RelativeUtf8(path) << '\n';
}

// ── 결과 모으기 ──────────────────────────────────────────────────────────────
//
// 세 범위가 **같은 목록 캐시**에서 모은다(계약 5 — *"전체 검색·정렬·필터는 W7 의
// 같은 목록 스냅샷에서 적용한다"*). 매 프레임 재귀 파일 순회를 하지 않고, 별도
// 자산 DB 도 두지 않는다. 캐시에 없는 폴더는 프레임당 `kWarmBudget` 개만 훑으므로
// 전체 자산은 처음 몇 프레임에 걸쳐 **점진적으로 채워진다** — 설계 귀결이다.
//
// ★ 돌려주는 포인터는 캐시 항목을 가리킨다. 캐시가 그 폴더를 다시 훑으면
//   무효가 되므로 이 프레임 안에서만 쓴다.

void ContentsBrowserWindow::CollectResults(std::vector<const editor::browser_directory_entry*>& out, size_t& supported)
{
    out.clear();
    supported = 0;
    const auto accept = [&](const editor::browser_directory_entry& entry)
    {
        if (!entry.isDirectory && !EditorAssetDatabase::Get().IsSupportExtension(entry.extension)) return;
        ++supported;
        if (!m_filter.PassFilter(entry.nameUtf8.c_str())) return;
        if (!entry.isDirectory && m_typeFilter >= 0 &&
            static_cast<int>(DeduceFileType(entry.path)) != m_typeFilter) return;
        out.push_back(&entry);
    };
    int budget = kWarmBudget;
    m_everythingPending = 0;
    m_everythingFolderCount = 0;

    if (m_scope == Scope::folder)
    {
        const auto& listing = editor::browser_cache_listing(m_currentDirectory);
        for (const auto& entry : listing.entries) accept(entry);
    }
    else if (m_scope == Scope::everything)
    {
        std::vector<file::path> queue{ m_rootDirectory };
        for (size_t head = 0; head < queue.size(); ++head)
        {
            const auto* listing = browser_warm_listing(queue[head], budget);
            if (!listing) { ++m_everythingPending; continue; }
            ++m_everythingFolderCount;
            if (!listing->valid) continue;
            for (const auto& entry : listing->entries)
            {
                if (entry.isDirectory) { if (!entry.isSymlink) queue.push_back(entry.path); }
                else accept(entry);
            }
        }
    }
    else
    {
        bool pruned = false;
        for (auto it = m_recents.begin(); it != m_recents.end();)
        {
            const auto* listing = browser_warm_listing(it->parent_path(), budget);
            if (!listing) { ++m_everythingPending; ++it; continue; }
            const std::string name = browser_utf8(it->filename());
            const editor::browser_directory_entry* found = nullptr;
            if (listing->valid)
                for (const auto& entry : listing->entries)
                    if (!entry.isDirectory && entry.nameUtf8 == name) { found = &entry; break; }
            // 지워진 최근 항목은 정리한다(계약 검증 — *"삭제된 최근 항목 정리"*).
            if (!found) { it = m_recents.erase(it); pruned = true; continue; }
            accept(*found);
            ++it;
        }
        if (pruned) SaveRecents();
        return;   // 최근 항목은 최근 순이 곧 정렬이다
    }

    // 스냅샷은 폴더마다 폴더 먼저·이름 오름차순이다. 전체 자산은 폴더를 넘어
    // 모았으므로 다시 이름순으로 세우고, 같은 이름은 경로로 가른다 — 동명 자산이
    // 프레임마다 자리를 바꾸지 않게 한다.
    if (m_scope == Scope::everything || m_sortDescending)
    {
        const bool descending = m_sortDescending;
        std::stable_sort(out.begin(), out.end(),
            [descending](const editor::browser_directory_entry* a, const editor::browser_directory_entry* b)
            {
                if (a->isDirectory != b->isDirectory) return a->isDirectory;
                if (a->nameUtf8 != b->nameUtf8) return descending ? a->nameUtf8 > b->nameUtf8 : a->nameUtf8 < b->nameUtf8;
                return a->pathUtf8 < b->pathUtf8;
            });
    }
}

// ── 요청함과 게시 ────────────────────────────────────────────────────────────

void ContentsBrowserWindow::ApplyRequests()
{
    using Kind = editor::windows::content_browser_request_kind;
    std::vector<editor::windows::content_browser_request> requests;
    {
        std::lock_guard lock(g_browserMailboxMutex);
        requests.swap(g_browserRequests);
    }
    for (auto& request : requests)
    {
        std::string rejection;
        switch (request.kind)
        {
        case Kind::go_folder:
        {
            const file::path relative = file::u8path(request.text);
            const file::path target = request.text.empty() || request.text == "." ? m_rootDirectory
                : relative.is_absolute() ? relative : m_rootDirectory / relative;
            if (!Navigate(target)) rejection = m_error;
            break;
        }
        case Kind::go_recent:     NavigateScope(Scope::recent); break;
        case Kind::go_everything: NavigateScope(Scope::everything); break;
        case Kind::back:
            if (m_historyIndex > 0) JumpHistory(m_historyIndex - 1);
            else rejection = "no back history";
            break;
        case Kind::forward:
            if (m_historyIndex + 1 < m_history.size()) JumpHistory(m_historyIndex + 1);
            else rejection = "no forward history";
            break;
        case Kind::up:
            if (m_scope == Scope::folder && m_currentDirectory != m_rootDirectory)
                Navigate(m_currentDirectory.parent_path());
            else rejection = "already at the top";
            break;
        case Kind::search:
        {
            // 타이핑과 같은 길이다 — 이력을 만들지 않는다(계약 4).
            const size_t length = std::min(request.text.size(), sizeof(m_filter.InputBuf) - 1);
            std::memcpy(m_filter.InputBuf, request.text.data(), length);
            m_filter.InputBuf[length] = 0;
            m_filter.Build();
            break;
        }
        case Kind::select:
            // 사람의 클릭을 흉내 낸다 — 그래서 **지난 프레임에 보인 결과** 안에
            // 있어야 한다. 보이지 않는 것을 고르는 창구는 사람에게 없다.
            if (std::find(m_publishedResults.begin(), m_publishedResults.end(), request.text)
                == m_publishedResults.end())
                rejection = "not among the visible results: " + request.text;
            else
                SelectAsset((m_rootDirectory / file::u8path(request.text)).lexically_normal());
            break;
        case Kind::scroll:
            m_pendingScrollY = std::max(0.f, request.value);
            break;
        case Kind::create_folder:
        case Kind::create_volume_profile:
            if (m_scope != Scope::folder)
                rejection = "open a folder to create assets in it";
            else if (!CreateNamedAsset(Kind::create_folder == request.kind ? CreateKind::folder
                    : CreateKind::volume_profile, m_currentDirectory, request.text))
                rejection = m_error;
            break;
        }
        if (rejection.empty()) ++m_requestsApplied;
        else { ++m_requestsRejected; m_lastRejection = std::move(rejection); }
    }
}

void ContentsBrowserWindow::PublishSnapshot()
{
    editor::windows::content_browser_snapshot snapshot;
    snapshot.frames = m_frames;
    snapshot.requestsApplied = m_requestsApplied;
    snapshot.requestsRejected = m_requestsRejected;
    snapshot.lastRejection = m_lastRejection;
    snapshot.scope = m_scope;
    snapshot.directory = m_scope == Scope::folder ? RelativeUtf8(m_currentDirectory) : std::string{};
    snapshot.search = m_filter.InputBuf;
    snapshot.selected = m_selectedPath.empty() ? std::string{} : RelativeUtf8(m_selectedPath);
    snapshot.error = m_error;
    snapshot.historyIndex = m_historyIndex;
    snapshot.historySize = m_history.size();
    for (const auto& visit : m_history)
        snapshot.history.push_back(visit.scope == Scope::folder
            ? "folder:" + RelativeUtf8(visit.directory)
            : std::string(editor::windows::content_browser_scope_name(visit.scope)));
    snapshot.canBack = m_historyIndex > 0;
    snapshot.canForward = m_historyIndex + 1 < m_history.size();
    snapshot.canUp = m_scope == Scope::folder && m_currentDirectory != m_rootDirectory;
    snapshot.canCreate = m_scope == Scope::folder;
    snapshot.canCreateVolumeProfile = m_scope == Scope::folder && CanCreateVolumeProfileIn(m_currentDirectory);
    snapshot.resultCount = m_publishedResultCount;
    snapshot.results = m_publishedResults;
    snapshot.scrollY = m_fileListScrollY;
    snapshot.scrollMaxY = m_fileListScrollMaxY;
    snapshot.everythingFolders = m_everythingFolderCount;
    snapshot.everythingPending = m_everythingPending;
    snapshot.everythingComplete = m_scope == Scope::everything && 0 == m_everythingPending;
    snapshot.recentCount = m_recents.size();
    snapshot.resultRebuilds = m_resultRebuilds;
    snapshot.layout = m_layout;
    std::lock_guard lock(g_browserMailboxMutex);
    g_browserSnapshot = std::move(snapshot);
}

void ContentsBrowserWindow::DrawFolderMenu(const file::path& directory)
{
    if (ImGui::MenuItem("New Folder..."))
        OpenCreateDialog(CreateKind::folder, directory);
    // ★ W7-5 가 지나가다 잡았다 — 여기 중괄호가 없어서 `browser_cache_invalidate()`
    //   가 `if` **밖**에 있었다(W7-1, e790b678). 들여쓰기는 안에 있는 것처럼 보이고
    //   컴파일러는 /W0 라 아무 말도 하지 않는다. 그래서 이 메뉴가 그려지는 **모든
    //   프레임**에 캐시가 통째로 버려졌다 — 폴더 우클릭 메뉴를 열어 둔 동안 브라우저가
    //   매 프레임 전부 다시 훑었다는 뜻이다. 아래 타일 쪽(`:521`)은 같은 일을 중괄호와
    //   함께 적어 두어 멀쩡했다 — 복제된 코드의 한쪽만 틀린 모양이다.
    if (CanCreateVolumeProfileIn(directory)
        && ImGui::MenuItem("Create Volume Profile..."))
    {
        OpenCreateDialog(CreateKind::volume_profile, directory);
    }
    if (ImGui::MenuItem("Open in File Explorer"))
        EditorPlatform::Get().RevealInFileExplorer(directory);
    if (::editor::popup_host_has_items(::editor::popup_host::content_browser_folder))
    {
        ImGui::Separator();
        ::editor::draw_popup_menu_items<::editor::popup_host::content_browser_folder>(
            ::editor::folder_target{ directory });
    }
}

void ContentsBrowserWindow::OpenCreateDialog(CreateKind kind, const file::path& directory)
{
    m_createKind = kind;
    m_folderTarget = directory;
    m_folderName[0] = 0;
    m_openFolderDialog = true;
    m_error.clear();
}

bool ContentsBrowserWindow::CanCreateVolumeProfileIn(const file::path& directory) const
{
    return !m_volumeProfileDirectory.empty() && browser_same_path(directory, m_volumeProfileDirectory);
}

bool ContentsBrowserWindow::CreateNamedAsset(CreateKind kind, const file::path& directory, std::string_view name)
{
    // W2-B: 예전 Volume Profile 은 OS 저장 대화상자를 열고 반환값을 버렸다 — 취소도
    // 실패도 같은 침묵이었고 같은 이름이면 말없이 숫자가 붙었다. 이제 폴더 만들기와
    // 같은 길이다: 이름을 받고, 실패하면 이유를 대화상자에 남기고, 성공하면 보인다.
    file::path created;
    editor::browser_cache_invalidate();   // W7-1: 성공·실패 어느 쪽이든 디스크가 움직였을 수 있다
    if (CreateKind::folder == kind)
    {
        if (!EditorAssetDatabase::Get().CreateFolder(directory, name, created, m_error)) return false;
        Navigate(created);
        return true;
    }
    if (!CanCreateVolumeProfileIn(directory))
    {
        m_error = "Volume profiles are created in the VolumeProfile folder.";
        return false;
    }
    if (!EditorAssetDatabase::Get().CreateVolumeProfile(directory, name, created, m_error)) return false;
    // 만든 것을 고른 채로 보여 준다. 이미 그 폴더에 있으면 Navigate 는 이력을 늘리지 않는다.
    if (!browser_same_path(m_currentDirectory, directory)) Navigate(directory);
    SelectAsset(created);
    m_error.clear();
    return true;
}

void ContentsBrowserWindow::DrawFolderDialog()
{
    if (m_openFolderDialog)
    {
        ImGui::OpenPopup("###BrowserCreateAsset");
        m_openFolderDialog = false;
    }
    const char* const title = CreateKind::folder == m_createKind
        ? "New Folder###BrowserCreateAsset" : "New Volume Profile###BrowserCreateAsset";
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(browser_utf8(m_folderTarget).c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", m_folderName, sizeof(m_folderName),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
        if (ImGui::Button("Create") || enter)
        {
            if (CreateNamedAsset(m_createKind, m_folderTarget, m_folderName))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { m_error.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void ContentsBrowserWindow::ShowDirectoryTree(const file::path& directory)
{
    // W7-0: 이 함수는 재귀한다. 시간 구간은 바깥(DrawDirectoryPanel)이 잡고
    // 여기서는 **일의 수**만 센다. 스캔은 캐시가 실제로 디스크를 만졌을
    // 때만 늘어야 하므로 바깥에서 캐시 계수의 차이로 센다(W7-1).
    editor::windows::add_panel_units(editor::windows::panel_cost_slot::browser_tree, 1);
    const std::string id = browser_utf8(directory);
    const std::string name = directory == m_rootDirectory ? "Assets" : browser_utf8(directory.filename());
    // W7-1: 프레임마다 directory_iterator 를 돌리지 않는다. 목록은 스냅샷이
    // 주고, 정렬도 스캔할 때 한 번 해 둔 것이다.
    const editor::browser_directory_listing& listing = editor::browser_cache_listing(directory);
    std::vector<file::path> children;
    for (const auto& entry : listing.entries)
    {
        // ★ 목록은 폴더가 먼저다(스캔이 그렇게 세운다). 첫 파일에서 멈춘다 — 파일
        //   5 만 개 폴더의 노드가 프레임마다 5 만 번 돌고 그만큼 `reserve` 했다.
        if (!entry.isDirectory) break;
        // 심볼릭 링크를 타지 않는 것은 옛 동작 그대로다 — 순환을 막는다.
        if (!entry.isSymlink) children.push_back(entry.path);
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_FramePadding;
    if (m_scope == Scope::folder && m_currentDirectory == directory) flags |= ImGuiTreeNodeFlags_Selected;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    const auto currentRelative = m_currentDirectory.lexically_relative(directory);
    if (m_revealDirectory && !currentRelative.empty() && *currentRelative.begin() != "..")
        ImGui::SetNextItemOpen(true);
    const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", "");
    browser_tree_label(name, EditorAssetPresentation::Get().GetDirectoryIcon(open && !children.empty()),
        EditorIcon::Folder);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", id.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Navigate(directory);
    // W7-5: 관문을 앞으로 옮긴다. 이 검사는 **드래그 중에만** 뜻이 있는데 `&&`
    // 왼쪽에 있어 평상시에도 노드마다 돌았다. `BeginDragDropTarget()` 은 드래그가
    // 없으면 즉시 거짓을 돌려주므로, 순서를 바꾸면 평상시 비용이 0 이 된다.
    // ★ 순서만 바꿔도 되는 이유: 둘 다 부수 효과가 없고, `BeginDragDropTarget` 이
    //   참일 때만 `EndDragDropTarget` 을 부르는 짝은 그대로다.
    if (ImGui::BeginDragDropTarget())
    {
        if (browser_same_path(directory, m_prefabDirectory))
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) HandleSceneObjectDrop(payload->Data);
        }
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

// 가상 위치 둘을 폴더 트리 맨 위에 둔다. 폴더 트리와 **같은 행 모양**을 써서
// "여기를 누르면 목록이 바뀐다" 가 한눈에 같은 종류로 읽히게 한다. 새 글리프를
// 들이지 않는다 — 아이콘 서브셋에 없는 글리프는 조용히 네모가 된다.
void ContentsBrowserWindow::DrawScopeEntries()
{
    const auto entry = [&](const char* id, const char* label, const char* icon, Scope scope)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (m_scope == scope) flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::TreeNodeEx(id, flags, "%s", "");
        browser_tree_label(label, nullptr, icon);
        if (ImGui::IsItemClicked()) NavigateScope(scope);
    };
    entry("##ScopeRecent", "Recent", EditorIcon::Timing, Scope::recent);
    entry("##ScopeEverything", "All Assets", EditorIcon::Layers, Scope::everything);
}

void ContentsBrowserWindow::DrawDirectoryPanel()
{
    const editor::windows::panel_cost_scope cost{ editor::windows::panel_cost_slot::browser_tree };
    // W7-1: **실제로 디스크를 만진 횟수**만 센다. 이 수가 0 에 수렴하는 것이
    // 캐시가 섰다는 증거이고, 시간은 OS 파일 캐시 온도로 흔들려 증거가 못 된다.
    const std::uint64_t scansBefore = editor::browser_cache_get_stats().scans;
    const std::uint64_t probesBefore = editor::browser_cache_get_stats().probes;
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, editor::ThemePixels(14.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.f, editor::ThemePixels(3.f) });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { editor::ThemePixels(3.f), editor::ThemePixels(1.f) });
    DrawScopeEntries();
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
    const auto treeAfter = editor::browser_cache_get_stats();
    editor::windows::add_panel_scans(editor::windows::panel_cost_slot::browser_tree,
        treeAfter.scans - scansBefore);
    editor::windows::add_panel_probes(editor::windows::panel_cost_slot::browser_tree,
        treeAfter.probes - probesBefore);
}

void ContentsBrowserWindow::ShowCurrentDirectoryFiles()
{
    const editor::windows::panel_cost_scope cost{ editor::windows::panel_cost_slot::browser_files };
    const std::uint64_t scansBefore = editor::browser_cache_get_stats().scans;
    const std::uint64_t probesBefore = editor::browser_cache_get_stats().probes;
    // W7-1: 목록은 스냅샷이 준다. 프레임마다 하던 것 셋이 여기서 사라졌다 —
    // directory_iterator · 항목마다의 is_directory(stat) · 비교마다 stat 하던
    // 정렬. 남은 것은 **정책**뿐이다(지원 확장자·검색·유형 필터·정렬 방향).
    // 그 정책은 W2-B 의 소유라 캐시로 내리지 않는다.
    // W2-B: 목록 캐시에서 **지금 범위**의 결과를 모은다. 정책(지원 확장자·검색·
    // 유형 필터·정렬)은 `CollectResults` 한 자리에 있다 — 세 범위가 같은 규칙을 탄다.
    if (m_pendingScrollY >= 0.f)
    {
        ImGui::SetScrollY(m_pendingScrollY);
        m_pendingScrollY = -1.f;
    }
    // folder 범위의 목록 요청이 **먼저** 와야 한다 — 낡은 목록을 다시 훑어 세대가
    // 오르면 아래 열쇠 비교가 그것을 본다.
    const bool folderUnreadable = m_scope == Scope::folder
        && !editor::browser_cache_listing(m_currentDirectory).valid;
    // W2-B: 모으기는 목록·범위·검색·유형·정렬이 바뀔 때만 한다. Release 실측으로 전체
    // 자산 4,274 개에서 **아무것도 안 바뀐 프레임**마다 순회·지원 판정·검색·정렬·게시
    // 문자열 256 개를 새로 만드느라 p95 3.25ms 였다.
    //
    // 매번 모으는 두 경우 — 전체 자산이 **아직 차오르는 중**(데우기는 모으는 순회가
    // 한다)이고, 최근 항목(최대 32 개이고, 모으면서 사라진 것을 정리한다).
    ResultKey key;
    key.generation = editor::browser_cache_generation();
    key.scope = m_scope;
    key.directory = m_scope == Scope::folder ? m_currentDirectory : file::path{};
    key.filter = m_filter.InputBuf;
    key.typeFilter = m_typeFilter;
    key.descending = m_sortDescending;
    const bool rebuild = !m_resultsValid || !(key == m_resultKey) || m_scope == Scope::recent
        || (m_scope == Scope::everything && 0 != m_everythingPending);
    if (folderUnreadable)
    {
        m_results.clear();
        m_resultSupported = 0;
        m_resultsValid = false;
        m_publishedResultCount = 0;
        m_publishedResults.clear();
    }
    else if (rebuild)
    {
        CollectResults(m_results, m_resultSupported);
        ++m_resultRebuilds;
        // 모으는 동안 데우기가 폴더를 넣어 세대가 올랐을 수 있다 — 결과는 그 뒤의
        // 상태를 담았으므로 **모은 뒤의** 세대로 적는다.
        key.generation = editor::browser_cache_generation();
        m_resultKey = std::move(key);
        m_resultsValid = true;
        m_publishedResultCount = m_results.size();
        m_publishedResults.clear();
        for (size_t i = 0; i < m_results.size() && i < kPublishedResultCapacity; ++i)
            m_publishedResults.push_back(RelativeUtf8(m_results[i]->path));
    }
    const std::vector<const editor::browser_directory_entry*>& entries = m_results;
    const size_t supported = m_resultSupported;
    const auto filesAfter = editor::browser_cache_get_stats();
    editor::windows::add_panel_scans(editor::windows::panel_cost_slot::browser_files,
        filesAfter.scans - scansBefore);
    editor::windows::add_panel_probes(editor::windows::panel_cost_slot::browser_files,
        filesAfter.probes - probesBefore);

    if (folderUnreadable)
    {
        ImGui::TextWrapped("Unable to read folder: %s",
            editor::browser_cache_listing(m_currentDirectory).error.message().c_str());
        return;
    }
    // 빈 목록의 이유를 가른다 — 검색 결과 0 과 실제로 비어 있는 것은 다른 말이다.
    if (m_scope == Scope::everything && 0 != m_everythingPending)
        ImGui::TextDisabled("Indexing folders... (%zu remaining)", m_everythingPending);
    if (entries.empty())
    {
        if (m_scope == Scope::recent && supported == 0)
            ImGui::TextDisabled("No recent assets. Assets you select or open appear here.");
        else
            ImGui::TextDisabled(supported == 0 ? "No supported assets or folders." : "No matching assets or folders.");
    }
    const float cell = editor::ThemePixels(m_tileSize + 12.f);
    const int columns = m_listView ? 1 : std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    if (ImGui::BeginTable("AssetGrid", columns, ImGuiTableFlags_SizingStretchSame))
    {
        // W2-B: 전체 자산은 프로젝트 전부다. Release 실측으로 결과 274 개에 p95 2.75ms,
        // 4,274 개에 15.9ms 였고 units 가 결과 수와 같았다 — 안 보이는 타일까지 전부
        // 그리고(썸네일 조회 포함) 있었다. 줄 단위로 잘라 보이는 줄만 그린다.
        // units 는 이제 **그린 타일 수**다 — 결과 수는 `editor.browser` 가 낸다.
        //
        // ★ clipper 는 줄 높이가 모두 같다고 본다. 파일 타일은 폴더 타일보다 선 하나와
        //   3px 여백만큼 높아서, 폴더 줄로 높이를 재면 파일 줄마다 어긋나 스크롤할 때
        //   흔들린다. 그래서 그린 칸 가운데 가장 높은 것을 기억해 모든 줄의 최소
        //   높이로 준다. 타일 크기나 목록 보기가 바뀌면 다시 잰다.
        const float rowKey = m_listView ? -1.f : m_tileSize;
        if (rowKey != m_gridRowKey) { m_gridRowKey = rowKey; m_gridRowHeight = 0.f; }
        const float cellPaddingY = ImGui::GetStyle().CellPadding.y;
        const int count = static_cast<int>(entries.size());
        const int rows = (count + columns - 1) / columns;
        std::uint64_t drawn = 0;
        ImGuiListClipper clipper;
        clipper.Begin(rows, m_gridRowHeight > 0.f ? m_gridRowHeight : -1.f);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, m_gridRowHeight);
                for (int column = 0; column < columns && row * columns + column < count; ++column)
                {
                    const editor::browser_directory_entry* entry = entries[static_cast<size_t>(row * columns + column)];
                    ImGui::TableSetColumnIndex(column);
                    ++drawn;
                    const float cellTop = ImGui::GetCursorScreenPos().y;
                    const std::string& name = entry->nameUtf8;
                    const std::string& pathId = entry->pathUtf8;
                    ImGui::PushID(pathId.c_str());
                    if (entry->isDirectory)
                    {
                        const ImVec2 size = m_listView ? ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight())
                            : ImVec2(editor::ThemePixels(m_tileSize), editor::ThemePixels(m_tileSize));
                        ImGui::Button("##Folder", size);
                        browser_item_artwork(EditorAssetPresentation::Get().GetDirectoryIcon(false), EditorIcon::Folder, name, m_listView);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pathId.c_str());
                        if ((ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                            || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) Navigate(entry->path);
                        if (ImGui::BeginPopupContextItem()) { DrawFolderMenu(entry->path); ImGui::EndPopup(); }
                        if (!m_listView)
                        {
                            ImGui::TextUnformatted(browser_ellipsis(name, size.x).c_str());
                            ImGui::TextDisabled("Folder");
                        }
                    }
                    else
                    {
                        auto presentation = EditorAssetPresentation::Get().ResolveFilePresentation(entry->extension);
                        DrawFileTile(presentation, entry->path, name, entry->revision,
                            { editor::ThemePixels(m_tileSize), editor::ThemePixels(m_tileSize) });
                    }
                    ImGui::PopID();
                    m_gridRowHeight = std::max(m_gridRowHeight,
                        ImGui::GetItemRectMax().y - cellTop + cellPaddingY * 2.f);
                }
            }
        }
        clipper.End();
        editor::windows::add_panel_units(editor::windows::panel_cost_slot::browser_files, drawn);
        ImGui::EndTable();
    }
    // 가상 위치에는 "이 폴더" 가 없다 — 빈 곳 우클릭 생성 메뉴를 열지 않는다(계약 5).
    if (m_scope == Scope::folder
        && ImGui::BeginPopupContextWindow("ContentFolderAreaMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        DrawFolderMenu(m_currentDirectory);
        ImGui::EndPopup();
    }
    // Submit a real remaining-area item; never grow the window with cursor movement.
    const ImVec2 remaining = ImGui::GetContentRegionAvail();
    ImGui::Dummy({ std::max(1.f, remaining.x), std::max(1.f, remaining.y) });
    m_fileListScrollY = ImGui::GetScrollY();
    m_fileListScrollMaxY = ImGui::GetScrollMaxY();
    // W7-5: 트리와 같은 순서 교정. 여기는 프레임당 1 회뿐이라 이득이 작지만,
    // 두 자리가 같은 규칙을 따라야 다음 사람이 한쪽만 보고 옛 모양을 베끼지 않는다.
    if (m_scope == Scope::folder && ImGui::BeginDragDropTarget())
    {
        if (browser_same_path(m_currentDirectory, m_prefabDirectory))
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) HandleSceneObjectDrop(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }
}
void ContentsBrowserWindow::DrawFileTile(const EditorAssetPresentation::FilePresentation& presentation,
										 const file::path& directory,
										 const std::string& fileName,
										 std::uint64_t revision,
										 const ImVec2& tileSize)
{
	const auto fileType = presentation.type;
	// W7 썸네일: 준비된 것이 있으면 그것을, 아니면 nullptr 이라 유형 아이콘이
	// 그대로 간다. 조회는 디스크를 만지지 않는다 — `revision` 은 목록 스캔이
	// 담아 둔 값이고, 캐시는 표 하나를 볼 뿐이다.
	Texture* thumbnail = nullptr;
	if (0 != revision)
	{
		thumbnail = editor::thumbnail_acquire(
			editor::thumbnail_make_key(directory, revision, kThumbnailResolution),
			directory, true);
	}
	ImGui::PushID(fileName.c_str());
	ImGui::BeginGroup();
    ImU32 color{};
    // Keep one item identity and hit rectangle when W7 replaces this icon with a thumbnail.
    const ImVec2 itemSize = m_listView
        ? ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()) : tileSize;
    const bool pressed = ImGui::InvisibleButton(fileName.c_str(), itemSize, ImGuiButtonFlags_EnableNav);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const bool selected = m_selectedPath == directory;
    const ImGuiCol background = selected ? ImGuiCol_Header : ImGui::IsItemActive() ? ImGuiCol_ButtonActive
        : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button;
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(background),
                        ImGui::GetStyle().FrameRounding);
    if (ImGui::IsItemFocused())
        draw->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_NavCursor),
                      ImGui::GetStyle().FrameRounding);
    browser_item_artwork(thumbnail ? thumbnail : presentation.type_image,
        presentation.type_icon, fileName, m_listView, 40.f,
        thumbnail ? presentation.type_icon : nullptr);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", browser_utf8(directory).c_str());
    if (pressed) SelectAsset(directory);

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
	{
		// 열기에 **성공한** 것만 최근 항목이다(계약 5).
		if (EditorPlatform::Get().OpenFile(directory)) RecordRecent(directory);
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
		// 전체·최근 결과는 여러 폴더에서 온다. 원래 폴더를 열어 같은 자산을
		// 고른 채로 보여 준다(계약 5 — *"해당 위치 열기"*).
		if (m_scope != Scope::folder && ImGui::MenuItem("Show in Folder"))
		{
			const file::path asset = directory;
			if (Navigate(asset.parent_path())) SelectAsset(asset);
		}
		if (m_scope == Scope::folder && CanCreateVolumeProfileIn(m_currentDirectory)
			&& ImGui::MenuItem("Create Volume Profile..."))
			OpenCreateDialog(CreateKind::volume_profile, m_currentDirectory);

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
		// W2-B: payload 는 이름이 아니라 전체 경로다 — 받는 쪽이 유형 폴더에서
		// 이름으로 다시 찾으면 같은 이름의 다른 파일이 열린다(EditorAssetDragPayload.h).
		const char* payloadType = FileTypeToString(fileType);
		if (directory.parent_path() == PathFinder::Relative("SpriteSheets"))
			payloadType = "SPRITESHEET";
		else if (directory.parent_path() == PathFinder::Relative("UI"))
			payloadType = "UI_TEXTURE";
		editor::asset_drag::set_payload(payloadType, directory);
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
    if (m_scope != Scope::folder)
    {
        // 가상 위치는 경로가 아니다. 뿌리로 돌아가는 단추와 지금 범위의 이름만 둔다 —
        // 경로처럼 보이는 표기를 지어내면 "그 폴더가 어디 있나" 를 찾게 된다.
        if (ImGui::Button("Assets", { 0.f, height })) Navigate(m_rootDirectory);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", EditorIcon::Collapse);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(m_scope == Scope::recent ? "Recent" : "All Assets");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
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
    // 입력칸이 **지금 범위**를 말한다(계약 5 — *"범위·검색어·유형 필터를 화면에 표시"*).
    const char* hint = m_scope == Scope::recent ? "Search recent assets"
        : m_scope == Scope::everything ? "Search all assets" : "Search this folder";
    if (ImGui::InputTextWithHint("##AssetSearch", hint, m_filter.InputBuf, sizeof(m_filter.InputBuf))) m_filter.Build();
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
    // 가상 위치에서는 만들 폴더가 없다 — 눌리지 않게 두고 이유를 말한다(계약 5).
    const bool canCreate = m_scope == Scope::folder;
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button(EditorIcon::Label<EditorIcon::Add, " New">)) ImGui::OpenPopup("ContentNew");
    ImGui::EndDisabled();
    if (!canCreate && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Open a folder to create assets in it");
    if (ImGui::BeginPopup("ContentNew")) { DrawFolderMenu(m_currentDirectory); ImGui::EndPopup(); }
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Back, "Back", m_historyIndex > 0)) JumpHistory(m_historyIndex - 1);
    if (ImGui::BeginPopupContextItem("NavigationHistory"))
    {
        for (size_t i = 0; i < m_history.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            const Visit& visit = m_history[i];
            const std::string label = visit.scope == Scope::recent ? std::string("Recent")
                : visit.scope == Scope::everything ? std::string("All Assets")
                : browser_utf8(visit.directory);
            if (ImGui::MenuItem(label.c_str(), nullptr, i == m_historyIndex)) JumpHistory(i);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Forward, "Forward", m_historyIndex + 1 < m_history.size())) JumpHistory(m_historyIndex + 1);
    ImGui::SameLine();
    if (browser_icon_button(EditorIcon::Up, "Parent folder", m_scope == Scope::folder && m_currentDirectory != m_rootDirectory))
        Navigate(m_currentDirectory.parent_path());
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
    // W7-1: 이 프레임에 다시 훑을 수 있는 폴더 수를 되돌린다. 낡은 것을
    // 한꺼번에 훑지 않게 막는 자리다.
    editor::browser_cache_begin_frame();
    // W7 썸네일: 완료를 반영하고 올라간 것을 Ready 로 승격한다. 프레임 **머리**
    // 에서만 바꾼다 — 프레임 안에서 승격하면 같은 프레임의 앞 타일과 뒤 타일이
    // 서로 다른 그림을 받는다.
    //
    // ★ 이 창이 안 그려지면 이 자리도 안 돈다(뒤 탭이면 ImGui 가 본문을 건너뛴다).
    //   그래도 맞다 — 보이지 않는 타일에는 바꿀 그림이 없다. 진행 중이던 작업은
    //   완료 큐에서 기다리고, 창이 다시 서면 그때 반영된다.
    editor::thumbnail_begin_frame();
    // W7-5: 뿌리 해석은 **프로젝트가 바뀔 때만** 한다. `weakly_canonical` 과
    // `is_directory` 는 둘 다 디스크를 만지므로 매 프레임 부르면 그것이 곧
    // 프레임당 파일시스템 호출이다 — 이 조각이 없애려는 바로 그 모양이고,
    // 게다가 여기는 슬롯 계측 **밖**이라 새 계수기에도 안 잡히고 있었다.
    //
    // 날것 경로가 그대로면 디스크에 다시 묻지 않는다(문자열 비교다). 그 값이
    // 바뀌는 것은 프로젝트를 여는 순간뿐이다.
    // W7-5: 이 블록의 디스크 접촉을 browser_tree 슬롯에 실어 보낸다. 여기는 슬롯
    // 계측 **밖**이라 그냥 두면 계수기가 이 자리를 못 본다 — 그러면 아래 캐시를
    // 걷어내는 변이가 게이트를 조용히 지나간다. 정상 상태의 델타는 0 이라
    // 슬롯 표본을 더럽히지 않는다(0 일 때는 신고조차 하지 않는다).
    const std::uint64_t rootProbesBefore = editor::browser_cache_get_stats().probes;
    const auto rootSource = PathFinder::Relative();
    if (rootSource != m_rootSource)
    {
        m_rootSource = rootSource;
        std::error_code rootError;
        const auto resolved = editor::browser_canonical(rootSource, rootError);
        m_rootUsable = !rootError && editor::browser_directory_exists(resolved);
        if (m_rootUsable)
        {
            m_rootDirectory = resolved;
            // 비교 대상도 **같은 정규형**으로 풀어 둔다. 한쪽만 정규형이면 어휘
            // 비교가 조용히 안 맞는다 — RelativeToPrefab("") 은 끝에 구분자가
            // 붙은 날것이라 정확히 그 함정이다.
            std::error_code prefabError;
            m_prefabDirectory = editor::browser_canonical(PathFinder::PrefabSourcePath(), prefabError);
            std::error_code volumeError;
            m_volumeProfileDirectory = editor::browser_canonical(PathFinder::VolumeProfilePath(), volumeError);
            // 프로젝트가 바뀌면 이전 프로젝트의 이력·선택·범위를 쓰지 않는다(계약 6).
            m_currentDirectory.clear();
            m_scope = Scope::folder;
            m_history.clear();
            m_historyIndex = 0;
            ClearSelection();
            m_filter.Clear();
            LoadRecents();
            Navigate(m_rootDirectory);
        }
    }
    const std::uint64_t rootProbes = editor::browser_cache_get_stats().probes - rootProbesBefore;
    if (0 != rootProbes)
        editor::windows::add_panel_probes(editor::windows::panel_cost_slot::browser_tree, rootProbes);
    ++m_frames;
    if (!m_rootUsable)
    {
        ImGui::TextDisabled("Assets folder is unavailable.");
        PublishSnapshot();
        return;
    }
    // W2-B: CLI 요청은 뿌리가 선 뒤, 그리기 전에 비운다 — 요청이 만든 이동이 이
    // 프레임의 그림과 게시본에 함께 실린다.
    ApplyRequests();
    // 현재 폴더가 사라졌는지는 **캐시가 안다** — 목록을 읽지 못했으면 뿌리로
    // 돌아간다. 여기서 is_directory 를 다시 부르면 프레임당 호출이 되살아난다.
    //
    // ★ 맞바꾼 것: 에디터가 떠 있는 동안 Assets 폴더가 밖에서 지워지면 이제
    //   "unavailable" 문구 대신 빈 트리가 보인다. 그 대가로 idle 프레임의 디스크
    //   접촉이 0 이 된다. 밖의 변경을 알아채는 일은 감시자(EditorDirectoryWatcher)
    //   의 몫이고 이 조각의 범위가 아니다.
    if (m_scope == Scope::folder && !editor::browser_cache_listing(m_currentDirectory).valid)
        Navigate(m_rootDirectory);

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
    m_layout = {};
    m_layout.treeVisible = treeVisible;
    m_layout.uiScale = scale;
    m_layout.availableWidth = size.x;
    m_layout.preferredTreeWidth = preferences.GetContentTreeWidth();
    m_layout.viewportX = ImGui::GetMainViewport()->Pos.x;
    m_layout.viewportY = ImGui::GetMainViewport()->Pos.y;
    m_layout.mouseX = ImGui::GetIO().MousePos.x;
    m_layout.mouseY = ImGui::GetIO().MousePos.y;
    m_layout.mouseDown = ImGui::GetIO().MouseDown[0];
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
        m_layout.appliedTreeWidth = treeWidth;
        m_layout.treeMinX = ImGui::GetItemRectMin().x;
        m_layout.treeMaxX = ImGui::GetItemRectMax().x;
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
        m_layout.splitterMinX = ImGui::GetItemRectMin().x;
        m_layout.splitterMaxX = ImGui::GetItemRectMax().x;
        m_layout.splitterMinY = ImGui::GetItemRectMin().y;
        m_layout.splitterMaxY = ImGui::GetItemRectMax().y;
        m_layout.splitterHovered = ImGui::IsItemHovered();
        m_layout.splitterActive = ImGui::IsItemActive();
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
        m_layout.preferredTreeWidth = preferred;
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
    m_layout.bodyMinX = ImGui::GetWindowPos().x;
    m_layout.bodyMaxX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
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
    PublishSnapshot();
}
