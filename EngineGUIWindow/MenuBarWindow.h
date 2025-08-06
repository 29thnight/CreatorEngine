#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

class SceneRenderer;
class MenuBarWindow
{
public:
	MenuBarWindow(SceneRenderer* ptr);
	~MenuBarWindow() = default;
	void RenderMenuBar();
    void ShowLogWindow();
	void ShowLightMapWindow();
	void ShowBehaviorTreeWindow();
	void ShowBlackBoardWindow();
	void SHowInputActionMap();
	void ShowBuildSceneSettingWindow();
private:
    ImFont* m_koreanFont{ nullptr };
	SceneRenderer* m_sceneRenderer{ nullptr };
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
	std::vector<std::vector<uint8_t>> collisionMatrix; //32 x 32 행렬을 사용하여 충돌 매트릭스를 표시합니다.
};
#endif // !DYNAMICCPP_EXPORTS