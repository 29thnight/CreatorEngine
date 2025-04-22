#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class GameObject;
class HierarchyWindow
{
public:
	HierarchyWindow(SceneRenderer* ptr);
	~HierarchyWindow() = default;

	void DrawSceneObject(const std::shared_ptr<GameObject>& obj);

	SceneRenderer* m_sceneRenderer{ nullptr };
};