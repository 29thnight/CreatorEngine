#include "HierarchyFlatten.h"

#include "Scene.h"
#include "Entity.h"

namespace
{
	// 재귀 대신 명시적 스택을 쓴다. 옛 `DrawSceneObject` 는 자기 자신을 불러
	// 내려갔고, 계층이 깊으면 그 깊이가 그대로 호출 스택이었다. 여기서는 깊이가
	// 벡터의 길이일 뿐이다.
	struct visit
	{
		int index;
		int depth;
	};
}

const std::vector<editor::hierarchy_flat_row>& editor::HierarchyFlatView::Rows(
	Scene* scene, const hierarchy_flat_key& key, const ImGuiTextFilter& filter)
{
	// revision 0 은 "아직 아무것도 못 봤다" 다 — 근거가 없으므로 믿지 않는다.
	const bool sameGround =
		nullptr != scene &&
		scene == m_builtScene &&
		0 != key.revision &&
		key.revision == m_built.revision &&
		key.entityCount == m_built.entityCount &&
		key.dontDestroyCount == m_built.dontDestroyCount &&
		key.searchActive == m_built.searchActive &&
		m_toggleStamp == m_builtToggleStamp;

	// 검색이 켜져 있는 동안에는 근거가 없다(헤더 ④) — 매 프레임 다시 만든다.
	if (sameGround && !key.searchActive)
	{
		m_stats.rows = m_rows.size();
		return m_rows;
	}

	Rebuild(scene, filter, key.searchActive);

	m_built = key;
	m_builtScene = scene;
	m_builtToggleStamp = m_toggleStamp;
	m_stats.rows = m_rows.size();
	return m_rows;
}

void editor::HierarchyFlatView::Toggle(int index)
{
	if (const auto found = m_toggled.find(index); found != m_toggled.end())
		m_toggled.erase(found);
	else
		m_toggled.insert(index);

	++m_toggleStamp;
}

void editor::HierarchyFlatView::Rebuild(Scene* scene, const ImGuiTextFilter& filter, bool searching)
{
	m_rows.clear();
	m_matched.clear();
	++m_stats.rebuilds;
	m_stats.visited = 0;

	if (!scene) return;

	// 씬이 갈리면 접힘도 버린다. 슬롯 인덱스는 씬마다 다시 매겨지므로, 들고
	// 있으면 옛 씬에서 접었던 번호가 새 씬의 엉뚱한 가지를 접는다.
	if (scene != m_builtScene && nullptr != m_builtScene)
	{
		m_toggled.clear();
		++m_toggleStamp;
	}

	const auto& entities = scene->m_Entities;
	const int count = static_cast<int>(entities.size());
	if (0 == count) return;

	const auto entityAt = [&entities, count](int index) -> Entity*
	{
		return (index >= 0 && index < count) ? entities[index].get() : nullptr;
	};

	// 슬롯 0 은 씬 자신이다. 머리 행은 늘 펼쳐져 있다(옛 SetNextItemOpen(true, Always)).
	m_rows.push_back({ 0, 0, 0, hierarchy_row_kind::scene_group, true, true });

	// 루트를 슬롯 순서로 모은다 — 옛 `for (int i = 1; i < sceneObjects.size(); ++i)`
	// 두 벌과 같은 차례이고, 그래서 화면에 줄이 놓이는 순서가 달라지지 않는다.
	std::vector<int> roots;
	std::vector<int> dontDestroyRoots;
	for (int index = 1; index < count; ++index)
	{
		Entity* obj = entityAt(index);
		if (!obj) continue;

		if (obj->IsDontDestroyOnLoad())
		{
			// DDOL 묶음에 세우는 것은 **서브트리의 머리**뿐이다. 옛 경로는 부모를
			// 보지 않고 DDOL 을 전부 다시 모았고, 그래서 같은 엔티티가 자기 부모
			// 밑과 묶음 밑에 두 번 나올 수 있었다.
			Entity* parent = entityAt(static_cast<int>(obj->GetParentIndex()));
			if (!parent || !parent->IsDontDestroyOnLoad()) dontDestroyRoots.push_back(index);
			continue;
		}
		if (obj->GetParentIndex() > 0) continue;
		roots.push_back(index);
	}

	// ── 검색: 한 번의 순회로 "자신 또는 자손이 걸린다" 를 미리 센다 ──────────
	//
	// 옛 `IsMatchedRecursive` 는 **행마다** 자기 서브트리를 통째로 다시 훑었다.
	// 같은 노드를 조상 수만큼 반복해 방문하므로 검색을 켜는 순간 O(n·깊이) 였다.
	// 여기서는 전위 순회 한 번을 세우고 그것을 거꾸로 훑어 부모로 올린다 —
	// 전위에서 부모는 언제나 자손보다 앞이므로, 뒤에서부터 보면 자손이 먼저다.
	if (searching)
	{
		m_matched.assign(static_cast<std::size_t>(count), 0);

		std::vector<std::uint8_t> seen(static_cast<std::size_t>(count), 0);
		std::vector<int> order;
		order.reserve(static_cast<std::size_t>(count));

		std::vector<int> stack;
		for (auto it = dontDestroyRoots.rbegin(); it != dontDestroyRoots.rend(); ++it) stack.push_back(*it);
		for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back(*it);

		while (!stack.empty())
		{
			const int index = stack.back();
			stack.pop_back();
			if (index < 0 || index >= count || seen[index]) continue;

			Entity* obj = entityAt(index);
			if (!obj) continue;

			seen[index] = 1;
			order.push_back(index);

			const auto& children = obj->GetChildrenIndices();
			for (auto it = children.rbegin(); it != children.rend(); ++it)
				stack.push_back(static_cast<int>(*it));
		}

		for (auto it = order.rbegin(); it != order.rend(); ++it)
		{
			const int index = *it;
			Entity* obj = entityAt(index);
			if (!obj) continue;

			if (!m_matched[index] && filter.PassFilter(obj->m_name.ToString().c_str()))
				m_matched[index] = 1;

			if (!m_matched[index]) continue;

			const int parent = static_cast<int>(obj->GetParentIndex());
			if (parent > 0 && parent < count) m_matched[parent] = 1;
		}

		m_stats.visited += static_cast<std::uint64_t>(order.size());
	}

	// ── 행 세우기 ────────────────────────────────────────────────────────────
	//
	// `emitted` 는 같은 슬롯을 두 번 담지 않기 위한 것이다. 옛 경로는 DDOL 묶음을
	// **부모와 무관하게** 모아 다시 그렸으므로, 일반 루트의 자손인 DDOL 엔티티가
	// 두 번 그려질 수 있었다. 평탄 목록에서는 그러면 ImGui 식별자가 겹친다 —
	// 처음 닿은 자리 하나만 남긴다.
	std::vector<std::uint8_t> emitted(static_cast<std::size_t>(count), 0);
	std::vector<visit> stack;
	int band = 0;

	const auto emit = [&](const std::vector<int>& source, int baseDepth)
	{
		for (auto it = source.rbegin(); it != source.rend(); ++it)
			stack.push_back({ *it, baseDepth });

		while (!stack.empty())
		{
			const visit here = stack.back();
			stack.pop_back();
			if (here.index < 0 || here.index >= count || emitted[here.index]) continue;
			if (searching && !m_matched[here.index]) continue;

			Entity* obj = entityAt(here.index);
			if (!obj) continue;

			emitted[here.index] = 1;
			++m_stats.visited;

			const auto& children = obj->GetChildrenIndices();
			const bool hasChildren = !children.empty();

			// 검색 중에는 걸린 것을 전부 펼친다(옛 SetNextItemOpen(true, Always)).
			// 기본값은 씬 루트의 직계만 펼침 — 옛 DefaultOpen 조건 그대로다.
			bool expanded = true;
			if (!searching)
			{
				const bool byDefault = (0 == obj->GetParentIndex());
				expanded = (m_toggled.find(here.index) != m_toggled.end()) ? !byDefault : byDefault;
			}
			expanded = expanded && hasChildren;

			m_rows.push_back({ here.index, here.depth, band++,
				hierarchy_row_kind::entity, hasChildren, expanded });

			if (!expanded) continue;

			for (auto it = children.rbegin(); it != children.rend(); ++it)
				stack.push_back({ static_cast<int>(*it), here.depth + 1 });
		}
	};

	// 씬 머리의 TreePush 안이므로 루트의 들여쓰기 단계는 1 이다.
	emit(roots, 1);

	if (!dontDestroyRoots.empty())
	{
		// 머리는 목록이 비어 있지 않으면 늘 나온다 — 검색이 자식을 전부 걸러도
		// 머리만 남는 옛 동작 그대로다.
		m_rows.push_back({ -1, 1, band, hierarchy_row_kind::ddol_group, true, true });
		emit(dontDestroyRoots, 2);
	}
}
