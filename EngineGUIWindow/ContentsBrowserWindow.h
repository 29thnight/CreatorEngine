#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include "DataSystem.h"
#include "EngineSetting.h"

// ── 콘텐츠 브라우저 (PHASE 4-3 슬라이스 2) ──
//
// 이 창은 DataSystem 안에 있었다. 창 등록 71줄 + 그리기 함수 일곱이
// 자산 시스템의 절반 가까이를 차지했고, 그 때문에 렌더 계층이
// GameObject·Prefab·PrefabUtility 같은 게임플레이 개념까지 알아야 했다.
//
// 창은 에디터 것이므로 에디터 층(6)으로 왔다. 여기서는 그 헤더들을
// 정당하게 들 수 있어서, 슬라이스 1이 임시로 세웠던 드롭 훅이 필요 없다.
//
// 자산 자체(모델·텍스처·머티리얼 캐시, 확장자→아이콘 표, 폰트)는
// 여전히 DataSystem이 가진다. 이 창은 그것을 읽어 그릴 뿐이다.
class ContentsBrowserWindow
{
public:
	ContentsBrowserWindow();
	~ContentsBrowserWindow() = default;

	// 인스펙터가 읽는 '지금 고른 자산'.
	//
	// 정적으로 둔 것은 의도다. 인스펙터는 이 창의 인스턴스를 모르고,
	// 창은 EditorMain이 하나만 만든다. 원래도 DataSystem의 공개 멤버라
	// 사실상 전역이었으므로 수명 관계는 달라지지 않는다.
	static std::string					selectedFileName;
	static std::string					selectedMetaFilePath;
	static std::optional<MetaYml::Node>	selectedFileMetaNode;

private:
	void ShowDirectoryTree(const file::path& directory);
	void ShowCurrentDirectoryFiles();
	void ShowCurrentDirectoryFilesTile();
	void ShowCurrentDirectoryFilesTree(const file::path& directory);
	void DrawFileTile(ImTextureID iconTexture,
					  const file::path& directory,
					  const std::string& fileName,
					  DataSystem::FileType& fileType,
					  const ImVec2& tileSize = ImVec2(160, 160));

	// 씬 오브젝트를 프리팹 폴더에 떨어뜨렸을 때. payload는 ImGui가
	// 실어 온 GameObject::Index의 주소다.
	void HandleSceneObjectDrop(const void* payload);

	// 현재 스타일은 설정이 정본이다. DataSystem이 들고 있던 사본은
	// MenuBarWindow가 설정과 함께 갱신하던 이중 상태였다 — 걷었다.
	ContentsBrowserStyle Style() const
	{
		return EngineSettingInstance->GetContentsBrowserStyle();
	}

	ImGuiTextFilter m_filter{};
	file::path		m_currentDirectory{};
	ImVec2			m_overlayPos{};
};
#endif // !DYNAMICCPP_EXPORTS
