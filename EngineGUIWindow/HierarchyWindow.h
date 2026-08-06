#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

class GameObject;
class HierarchyWindow
{
public:
	HierarchyWindow();
	void DrawSceneObject(const std::shared_ptr<GameObject>& obj);
	~HierarchyWindow() = default;

	//void DrawSceneObject(const std::shared_ptr<GameObject>& obj, GameObject* selected, bool forceOpenPath, bool& scrolledOnce);
	bool IsMatchedRecursive(const std::shared_ptr<GameObject>& obj);

	ImGuiTextFilter m_searchFilter{};
	std::vector<GameObject*> m_clipboard{};
	bool m_requestScrollToSelection = false;
};
#endif // !DYNAMICCPP_EXPORTS
