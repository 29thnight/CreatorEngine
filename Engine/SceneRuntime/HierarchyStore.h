#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

// Scene 슬롯과 평행한 계층 정본 store.
//
// H3부터 Entity에는 parent/root/children 복사본이 없다. 슬롯 점유와 모든 계층
// 변경은 이 컨테이너에 직접 기록되고, 직렬화 어댑터도 같은 값을 읽는다.
class HierarchyStore
{
public:
	using Index = int;
	static constexpr Index kInvalidIndex = -1;

	void Reserve(size_t count)
	{
		m_parentIndices.reserve(count);
		m_rootIndices.reserve(count);
		m_childrenIndices.reserve(count);
		m_occupied.reserve(count);
	}

	void GrowOne()
	{
		m_parentIndices.push_back(kInvalidIndex);
		m_rootIndices.push_back(0);
		m_childrenIndices.emplace_back();
		m_occupied.push_back(false);
	}

	void Clear()
	{
		m_parentIndices.clear();
		m_rootIndices.clear();
		m_childrenIndices.clear();
		m_occupied.clear();
	}

	void ResetSlot(size_t index)
	{
		if (index >= m_parentIndices.size()) return;
		m_parentIndices[index] = kInvalidIndex;
		m_rootIndices[index] = 0;
		m_childrenIndices[index].clear();
		m_occupied[index] = false;
	}

	void OccupySlot(size_t index, Index parentIndex = kInvalidIndex, Index rootIndex = 0)
	{
		if (index >= m_parentIndices.size()) return;
		m_parentIndices[index] = parentIndex;
		m_rootIndices[index] = rootIndex;
		m_childrenIndices[index].clear();
		m_occupied[index] = true;
	}

	void SetParent(size_t index, Index parentIndex)
	{
		if (!IsOccupied(index)) return;
		m_parentIndices[index] = parentIndex;
	}

	void SetRoot(size_t index, Index rootIndex)
	{
		if (!IsOccupied(index)) return;
		m_rootIndices[index] = rootIndex;
	}

	void AttachChild(size_t index, Index childIndex)
	{
		if (!IsOccupied(index)) return;
		auto& children = m_childrenIndices[index];
		if (std::find(children.begin(), children.end(), childIndex) == children.end())
			children.push_back(childIndex);
	}

	void DetachChild(size_t index, Index childIndex)
	{
		if (!IsOccupied(index)) return;
		std::erase(m_childrenIndices[index], childIndex);
	}

	void ClearChildren(size_t index)
	{
		if (!IsOccupied(index)) return;
		m_childrenIndices[index].clear();
	}

	void SetChildren(size_t index, std::vector<Index> children)
	{
		if (!IsOccupied(index)) return;
		m_childrenIndices[index] = std::move(children);
	}

	bool IsOccupied(size_t index) const
	{
		return index < m_occupied.size() && m_occupied[index];
	}

	size_t Size() const { return m_parentIndices.size(); }
	Index ParentOf(size_t index) const { return m_parentIndices.at(index); }
	Index RootOf(size_t index) const { return m_rootIndices.at(index); }
	const std::vector<Index>& ChildrenOf(size_t index) const { return m_childrenIndices.at(index); }

private:
	std::vector<Index> m_parentIndices;
	std::vector<Index> m_rootIndices;
	std::vector<std::vector<Index>> m_childrenIndices;
	std::vector<bool> m_occupied;
};
