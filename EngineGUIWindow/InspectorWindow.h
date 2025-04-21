#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class InspectorWindow
{
public:
	InspectorWindow(SceneRenderer* ptr);
	~InspectorWindow() = default;

	SceneRenderer* m_sceneRenderer{ nullptr };
};