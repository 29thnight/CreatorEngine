#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

class SceneRenderer;
class GameObject;
class HierarchyWindow
{
public:
	HierarchyWindow(SceneRenderer* ptr);
	void DrawSceneObject(const std::shared_ptr<GameObject>& obj);
	~HierarchyWindow() = default;

	//void DrawSceneObject(const std::shared_ptr<GameObject>& obj, GameObject* selected, bool forceOpenPath, bool& scrolledOnce);

	SceneRenderer* m_sceneRenderer{ nullptr };
	ImGuiTextFilter m_searchFilter{};
	std::vector<GameObject*> m_clipboard{};
	bool m_requestScrollToSelection = false;
};
#endif // !DYNAMICCPP_EXPORTS