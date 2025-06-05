#pragma once
#include "ImGuiRegister.h"


class SceneRenderer;
class MenuBarWindow
{
public:
	MenuBarWindow(SceneRenderer* ptr);
	~MenuBarWindow() = default;
	void RenderMenuBar();
    void ShowLogWindow();

private:
    ImFont* m_koreanFont{ nullptr };
	SceneRenderer* m_sceneRenderer{ nullptr };
    int  m_selectedLogIndex{};
    bool m_bShowLogWindow{ false };
};
