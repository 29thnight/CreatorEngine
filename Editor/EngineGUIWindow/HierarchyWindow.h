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
	// 한 판에서 그린 엔티티 줄 수. 홀짝 띠의 기준이며 Draw가 0부터 다시 센다.
	int m_rowIndex = 0;
};
