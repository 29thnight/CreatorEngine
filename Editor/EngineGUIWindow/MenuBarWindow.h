#pragma once
#include "ImGui.h"
#include "Windows/EditorWindowBody.h"

#include <vector>

class MenuBarWindow
{
public:
	MenuBarWindow();
	~MenuBarWindow() = default;
	void RenderMenuBar();
	/// 제목표시줄 아래 한 줄. s&box 배치에서 재생 컨트롤은 메뉴 행이 아니라
	/// 이 툴바에 있다 — 메뉴 행 가운데는 창 제목이 쓴다.
	void RenderToolBar();
	void ShowAboutWindow();
	/// 프레임 루프 안에 인라인으로 있던 본문. 셸이 창 본문으로 부른다.
	void ShowProfilerWindow();
	/// 아래 둘은 셸이 본문으로 부르고, Show* 짝은 창이 닫혀 있을 때만
	/// 도는 정리 경로다. 함수 지역 static을 나눠 쓰므로 몸통은 하나로 둔다.
	void DrawBehaviorTreeWindow();
	void DrawBlackBoardWindow();
    void ShowLogWindow();
	void ShowBehaviorTreeWindow();
	void ShowBlackBoardWindow();
	void SHowInputActionMap();
	void ShowBuildSceneSettingWindow();
	void ShowRenderDebugWindow();

private:
	void BehaviorTreeWindow(bool drawing);
	void BlackBoardWindow(bool drawing);

    ImFont* m_koreanFont{ nullptr };
    int  m_selectedLogIndex{};
	bool m_bShowNewScenePopup{ false };
	std::vector<std::vector<uint8_t>> collisionMatrix; //32 x 32 행렬을 사용하여 충돌 매트릭스를 표시합니다.

	// 이 창이 건 본문 열 개의 수명(PHASE 21 W3).
	//
	// 전에는 푸는 자리가 **하나도 없었다** — 소멸자가 `= default` 였고
	// 열 개의 본문은 모두 `this` 를 물고 있다. 이 객체가 죽으면 보관소에
	// 죽은 `this` 를 부르는 함수 열 개가 남는 구조였다. 지금 그 수명은
	// 이 벡터가 들고, 소멸자는 그대로 `= default` 로 둔다 — 할 일이
	// 없어서가 아니라 멤버가 하기 때문이다.
	std::vector<::editor::windows::window_body_binding> m_windowBodies;
};
