#pragma once
#include "ImGui.h"

class Entity;
class HierarchyWindow
{
public:
	void Draw();
	void DrawSceneObject(Entity* obj);
	~HierarchyWindow() = default;

	bool IsMatchedRecursive(Entity* obj);

	ImGuiTextFilter m_searchFilter{};
	std::vector<Entity*> m_clipboard{};
	bool m_requestScrollToSelection = false;
};
