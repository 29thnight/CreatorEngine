#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "ImGui.h"
#include "EditorAssetPresentation.h"
#include "EditorSettingsStore.h"
#include "AuthoringWriteNode.h"
#include <cstdint>
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
	void ShowDirectoryTree(const file::path& directory);
	void ShowCurrentDirectoryFiles();
    void Navigate(const file::path& directory, bool addHistory = true);
    void DrawToolbar(bool collapsedTree);
    void DrawBreadcrumb(float width);
    void DrawDirectoryPanel();
    void DrawFolderMenu(const file::path& directory);
    void DrawFolderDialog();
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
    std::vector<file::path> m_history;
    size_t m_historyIndex{};
    bool m_revealDirectory{ true };
    bool m_showTree{ true };
    bool m_listView{};
    float m_tileSize{ 88.f };
    float m_treeDragStart{ 220.f };
    int m_typeFilter{ -1 };
    bool m_sortDescending{};
    std::string m_error;
    file::path m_folderTarget;
    bool m_openFolderDialog{};
    char m_folderName[256]{};
    char m_pathInput[1024]{};
};
