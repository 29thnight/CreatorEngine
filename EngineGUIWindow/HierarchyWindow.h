#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

class Entity;
class HierarchyWindow
{
public:
	HierarchyWindow();
	void DrawSceneObject(const std::shared_ptr<Entity>& obj);
	~HierarchyWindow() = default;

	//void DrawSceneObject(const std::shared_ptr<Entity>& obj, Entity* selected, bool forceOpenPath, bool& scrolledOnce);
	bool IsMatchedRecursive(const std::shared_ptr<Entity>& obj);

	ImGuiTextFilter m_searchFilter{};
	std::vector<Entity*> m_clipboard{};
	bool m_requestScrollToSelection = false;
};
#endif // !DYNAMICCPP_EXPORTS
