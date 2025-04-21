#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class RenderPassWindow
{
public:
	RenderPassWindow(SceneRenderer* ptr);
	~RenderPassWindow() = default;

	SceneRenderer* m_sceneRenderer{ nullptr };
};