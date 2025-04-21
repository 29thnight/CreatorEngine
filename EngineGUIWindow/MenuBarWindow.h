#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class MenuBarWindow
{
public:
	MenuBarWindow(SceneRenderer* ptr) : m_sceneRenderer(ptr) {}
	~MenuBarWindow() = default;
	void RenderMenuBar();

	SceneRenderer* m_sceneRenderer{ nullptr };
};