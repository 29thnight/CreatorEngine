#pragma once
#include "ImGui.h"
#include "HierarchyFlatten.h"

class Entity;
class HierarchyWindow
{
public:
	void Draw();
	// 한 줄. 자식으로 내려가지 않는다 — 내려갈 자리는 평탄 목록이 대신한다
	// (PHASE 21 W7-2, HierarchyFlatten.h).
	void DrawSceneObjectRow(Entity* obj, const editor::hierarchy_flat_row& row);
	~HierarchyWindow() = default;

	ImGuiTextFilter m_searchFilter{};
	std::vector<Entity*> m_clipboard{};
	bool m_requestScrollToSelection = false;
	// 한 판에서 **실제로 그린** 엔티티 줄 수. W7-3 의 clipping 이 서면 이 값이
	// 보이는 행 수로 떨어진다. 홀짝 띠의 번호는 여기가 아니라 목록이 준다 —
	// 건너뛴 줄이 있어도 띠가 어긋나지 않게 하려는 것이다.
	int m_rowIndex = 0;
	editor::HierarchyFlatView m_flat{};
};
