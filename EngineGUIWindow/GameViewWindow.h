#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

class SceneRenderer;
class GameViewWindow
{
public:
	GameViewWindow(SceneRenderer* ptr) : m_sceneRenderer(ptr) {};
	~GameViewWindow() = default;
	
	void RenderGameViewWindow();

	SceneRenderer* m_sceneRenderer{ nullptr };
};
#endif // !DYNAMICCPP_EXPORTS