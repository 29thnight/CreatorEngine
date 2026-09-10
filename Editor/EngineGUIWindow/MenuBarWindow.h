#pragma once
#include "ImGuiRegister.h"

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
    void ShowLogWindow();
	void ShowLightMapWindow();
	void ShowBehaviorTreeWindow();
	void ShowBlackBoardWindow();
	void SHowInputActionMap();
	void ShowBuildSceneSettingWindow();
	void ShowRenderDebugWindow();
private:
    ImFont* m_koreanFont{ nullptr };
    int  m_selectedLogIndex{};
    bool m_bShowLogWindow{ false };
	bool m_bShowProfileWindow{ false };
	bool m_bShowNewScenePopup{ false };
	bool m_bShowLightMapWindow{ false };
	bool m_bCollisionMatrixWindow{ false };
	bool m_bShowBehaviorTreeWindow{ false };
	bool m_bShowBlackBoardWindow{ false };
	bool m_bShowInputActionMapWindow{ false };
	bool m_bShowBuildSceneSettingWindow{ false };
	bool m_bShowRenderDebugWindow{ false };
	bool m_bShowAboutWindow{ false };
	std::vector<std::vector<uint8_t>> collisionMatrix; //32 x 32 행렬을 사용하여 충돌 매트릭스를 표시합니다.
};
