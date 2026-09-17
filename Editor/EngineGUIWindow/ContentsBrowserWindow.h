#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "ImGui.h"
#include "EditorAssetPresentation.h"
#include "EditorSettingsStore.h"
#include "AuthoringWriteNode.h"
#include "ContentBrowserControl.h"
#include "BrowserDirectorySnapshot.h"
#include <cstdint>
#include <string>
#include <vector>

// ── 콘텐츠 브라우저 (PHASE 4-3 슬라이스 2) ──
//
// 이 창은 DataSystem 안에 있었다. 창 등록 71줄 + 그리기 함수 일곱이
// 자산 시스템의 절반 가까이를 차지했고, 그 때문에 렌더 계층이
// GameObject·Prefab·PrefabUtility 같은 게임플레이 개념까지 알아야 했다.
//
// 창은 에디터 것이므로 에디터 층(6)으로 왔다. 여기서는 그 헤더들을
// 정당하게 들 수 있어서, 슬라이스 1이 임시로 세웠던 드롭 훅이 필요 없다.
//
	// 파일 분류·아이콘·폰트는 EditorAssetPresentation이 소유하고, runtime
	// asset cache인 DataSystem과 이 창 사이에는 표시 상태가 없다.
class ContentsBrowserWindow
{
public:
	void Draw();
	~ContentsBrowserWindow() = default;

	// 인스펙터가 읽는 '지금 고른 자산'.
	//
	// 정적으로 둔 것은 의도다. 인스펙터는 이 창의 인스턴스를 모르고,
	// 창은 EditorMain이 하나만 만든다. 원래도 DataSystem의 공개 멤버라
	// 사실상 전역이었으므로 수명 관계는 달라지지 않는다.
	static std::string					selectedFileName;
	static std::string					selectedMetaFilePath;
	static std::optional<Authoring::WriteDocument> selectedFileMetaNode;

private:
	using Scope = editor::windows::content_browser_scope;
	/// 이름을 받아 만드는 것. 폴더 만들기와 Volume Profile 이 같은 대화상자를 쓴다.
	enum class CreateKind : std::uint8_t { folder, volume_profile };

	/// PHASE 21 W2-B — 이력의 한 칸은 **경로가 아니라 방문**이다.
	///
	/// 계획서 §W2-B 계약 4: *"뒤로/앞으로는 해당 방문의 검색어·스크롤·선택을
	/// 복원한다."* 이력이 경로만 들고 있으면 돌아간 자리에서 무엇을 찾고 있었는지가
	/// 사라진다. 떠나는 순간 지금 방문에 셋을 적어 두고(`CaptureVisit`), 돌아올 때
	/// 되살린다(`RestoreVisit`).
	struct Visit
	{
		Scope scope{ Scope::folder };
		file::path directory{};   ///< folder 범위에서만 뜻이 있다(정규형)
		std::string search{};
		file::path selected{};    ///< 떠날 때 고른 자산의 전체 경로
		float scrollY{};
	};

	void ShowDirectoryTree(const file::path& directory);
	void ShowCurrentDirectoryFiles();
	void DrawScopeEntries();
    /// 모든 **폴더** 이동의 단일 입구(계약 3). 정상 이동은 검색어를 비운다.
    /// 실패하면 `m_error` 에 이유를 두고 거짓 — 현재 위치는 그대로다.
    bool Navigate(const file::path& directory);
    /// 가상 위치(최근·전체)로 간다. 폴더 이동과 같은 이력 규칙을 탄다.
    void NavigateScope(Scope scope);
    /// 이력의 다른 칸으로 간다 — 뒤로·앞으로·이력 메뉴가 전부 여기를 지난다.
    void JumpHistory(size_t index);
    void PushVisit(Visit visit);
    void CaptureVisit();
    void RestoreVisit(const Visit& visit);
    /// 자산을 고른다. 클릭·복원·CLI 가 같은 길을 쓴다. 메타를 읽는 데 성공하면
    /// 최근 항목에 올린다(인스펙터에 편집할 수 있게 올라갔다는 뜻이다).
    bool SelectAsset(const file::path& path);
    void ClearSelection();
    void RecordRecent(const file::path& path);
    void LoadRecents();
    void SaveRecents() const;
    void CollectResults(std::vector<const editor::browser_directory_entry*>& out, size_t& supported);
    std::string RelativeUtf8(const file::path& path) const;
    void ApplyRequests();
    void PublishSnapshot();
    void DrawToolbar(bool collapsedTree);
    void DrawBreadcrumb(float width);
    void DrawDirectoryPanel();
    void DrawFolderMenu(const file::path& directory);
    void DrawFolderDialog();
    void OpenCreateDialog(CreateKind kind, const file::path& directory);
    /// Volume Profile 을 만들 수 있는 자리인가 — 폴더 메뉴·타일 메뉴·CLI 가 같은 술어를 쓴다.
    bool CanCreateVolumeProfileIn(const file::path& directory) const;
    /// 대화상자의 Create 와 CLI 가 지나는 유일한 생성 경로. 실패하면 `m_error` 에 이유를 둔다.
    bool CreateNamedAsset(CreateKind kind, const file::path& directory, std::string_view name);
    void DrawSearch(float width);
	/// `revision` 은 목록 스캔이 담아 둔 원본 변경 표식이다(W7 썸네일 키).
	/// 0 이면 썸네일을 묻지 않고 유형 아이콘에 머문다.
	void DrawFileTile(const EditorAssetPresentation::FilePresentation& presentation,
					  const file::path& directory,
					  const std::string& fileName,
					  std::uint64_t revision,
					  const ImVec2& tileSize = ImVec2(160, 160));

	// 씬 오브젝트를 프리팹 폴더에 떨어뜨렸을 때. payload는 ImGui가
	// 실어 온 Entity::Index의 주소다.
	void HandleSceneObjectDrop(const void* payload);

	ImGuiTextFilter m_filter{};
	file::path m_rootDirectory{};
	// W7-5: 뿌리를 만든 **날것 경로**. 이것이 그대로면 디스크에 다시 묻지 않는다.
	file::path m_rootSource{};
	bool m_rootUsable{};
	// W7-5: 정규형으로 **미리 풀어 둔** 특수 폴더. 트리 노드마다 equivalent 로
	// 디스크에 묻던 것을 이 값과의 어휘 비교로 대신한다. 양쪽이 같은 정규형이라야
	// 비교가 성립하므로 여기 담을 때 browser_canonical 을 거친다.
	file::path m_prefabDirectory{};
	file::path m_volumeProfileDirectory{};
	file::path		m_currentDirectory{};
	Scope m_scope{ Scope::folder };
    std::vector<Visit> m_history;
    size_t m_historyIndex{};
    /// 목록 child 의 스크롤. child 안에서만 읽고 쓸 수 있어 한 프레임 늦게 적용한다.
    float m_fileListScrollY{};
    float m_fileListScrollMaxY{};
    float m_pendingScrollY{ -1.f };
    /// 목록 표의 줄 높이 — clipper 가 쓰는 값. 그린 칸 가운데 가장 높은 것이다.
    float m_gridRowHeight{};
    float m_gridRowKey{ -2.f };

    // 최근 항목 — 프로젝트별, 개인 상태라 `Library/` 에 둔다(git 이 무시한다).
    std::vector<file::path> m_recents;
    file::path m_recentsFile{};

    /// 지금 고른 자산. 정적 `selectedMetaFilePath` 는 인스펙터가 쓰는 표기라
    /// 경로 비교용으로 따로 든다.
    file::path m_selectedPath{};

    // 전체 자산·최근 항목이 이번 프레임에 확보한 폴더 수와 못 한 수. 순회
    // 상태를 프레임을 넘겨 들지 않는다 — 매 프레임 캐시에서 다시 모으므로
    // 폴더가 생기거나 사라져도 낡은 순회 목록이 남지 않는다.
    size_t m_everythingFolderCount{};
    size_t m_everythingPending{};

    /// W2-B — 결과 기억. 이 열쇠가 그대로면 모으기·정렬·게시 문자열을 다시 만들지
    /// 않는다. 포인터는 캐시 세대가 같은 동안만 유효하다(`browser_cache_generation`).
    struct ResultKey
    {
        std::uint64_t generation{};
        Scope scope{ Scope::folder };
        file::path directory{};
        std::string filter{};
        int typeFilter{ -1 };
        bool descending{};
        bool operator==(const ResultKey&) const = default;
    };
    bool m_resultsValid{};
    ResultKey m_resultKey{};
    std::vector<const editor::browser_directory_entry*> m_results;
    size_t m_resultSupported{};
    std::uint64_t m_resultRebuilds{};

    // 게시용 — 이번 프레임에 그린 결과.
    std::vector<std::string> m_publishedResults;
    size_t m_publishedResultCount{};
    std::uint64_t m_frames{};
    std::uint64_t m_requestsApplied{};
    std::uint64_t m_requestsRejected{};
    std::string m_lastRejection;
    bool m_revealDirectory{ true };
    bool m_showTree{ true };
    bool m_listView{};
    float m_tileSize{ 88.f };
    float m_treeDragStart{ 220.f };
    editor::windows::content_browser_layout m_layout{};
    std::vector<editor::windows::content_browser_tile> m_visibleTiles;
    int m_typeFilter{ -1 };
    bool m_sortDescending{};
    std::string m_error;
    file::path m_folderTarget;
    CreateKind m_createKind{ CreateKind::folder };
    bool m_openFolderDialog{};
    char m_folderName[256]{};
    char m_pathInput[1024]{};
};
