#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

class Entity;
class HierarchyWindow
{
public:
	HierarchyWindow();
	void DrawSceneObject(Entity* obj);
	~HierarchyWindow() = default;

	bool IsMatchedRecursive(Entity* obj);

	ImGuiTextFilter m_searchFilter{};
	std::vector<Entity*> m_clipboard{};
	bool m_requestScrollToSelection = false;
};
#endif // !DYNAMICCPP_EXPORTS
