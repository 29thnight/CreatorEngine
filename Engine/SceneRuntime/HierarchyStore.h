#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
		++m_revision;
	}

	void Clear()
	{
		m_parentIndices.clear();
		m_rootIndices.clear();
		m_childrenIndices.clear();
		m_occupied.clear();
		++m_revision;
	}

	void ResetSlot(size_t index)
	{
		if (index >= m_parentIndices.size()) return;
		m_parentIndices[index] = kInvalidIndex;
		m_rootIndices[index] = 0;
		m_childrenIndices[index].clear();
		m_occupied[index] = false;
		++m_revision;
	}

	void OccupySlot(size_t index, Index parentIndex = kInvalidIndex, Index rootIndex = 0)
	{
		if (index >= m_parentIndices.size()) return;
		m_parentIndices[index] = parentIndex;
		m_rootIndices[index] = rootIndex;
		m_childrenIndices[index].clear();
		m_occupied[index] = true;
		++m_revision;
	}

	void SetParent(size_t index, Index parentIndex)
	{
		if (!IsOccupied(index)) return;
		m_parentIndices[index] = parentIndex;
		++m_revision;
	}

	void SetRoot(size_t index, Index rootIndex)
	{
		if (!IsOccupied(index)) return;
		m_rootIndices[index] = rootIndex;
		++m_revision;
	}

	void AttachChild(size_t index, Index childIndex)
	{
		if (!IsOccupied(index)) return;
		auto& children = m_childrenIndices[index];
		if (std::find(children.begin(), children.end(), childIndex) == children.end())
		{
			children.push_back(childIndex);
			++m_revision;
		}
	}

	void DetachChild(size_t index, Index childIndex)
	{
		if (!IsOccupied(index)) return;
		if (std::erase(m_childrenIndices[index], childIndex) > 0) ++m_revision;
	}

	void ClearChildren(size_t index)
	{
		if (!IsOccupied(index)) return;
		m_childrenIndices[index].clear();
		++m_revision;
	}

	void SetChildren(size_t index, std::vector<Index> children)
	{
		if (!IsOccupied(index)) return;
		m_childrenIndices[index] = std::move(children);
		++m_revision;
	}

	bool IsOccupied(size_t index) const
	{
		return index < m_occupied.size() && m_occupied[index];
	}

	size_t Size() const { return m_parentIndices.size(); }

	/// 계층이 바뀔 때마다 오르는 수. **표시 캐시의 무효화 근거**다(PHASE 21 W7-2).
	///
	/// 파생 표시 목록(평탄화·깊이·접힘)을 매 프레임 다시 만들지 않으려면 "언제
	/// 다시 만들어야 하는가" 를 알 근거가 필요한데, 그 값이 없었다. 계획서 §8.3 은
	/// "HierarchyStore mutation/revision 또는 명시적 scene event로 cache를
	/// 무효화한다" 고 적어 두고 이 값을 전제했으나 실물에는 없었다.
	///
	/// 정본을 복제하지 않는 것이 규약이므로, 캐시는 parent/children 을 베끼는 대신
	/// **이 수가 달라졌을 때만** 다시 순회한다. 값이 같다는 것은 계층이 그대로라는
	/// 뜻이고, 다르다는 것은 무엇이 달라졌는지 모른다는 뜻이다 — 그래서 전량
	/// 재구축이다(부분 무효화는 근거가 더 정밀해진 뒤의 일이다).
	std::uint64_t Revision() const noexcept { return m_revision; }
	Index ParentOf(size_t index) const { return m_parentIndices.at(index); }
	Index RootOf(size_t index) const { return m_rootIndices.at(index); }
	const std::vector<Index>& ChildrenOf(size_t index) const { return m_childrenIndices.at(index); }

private:
	std::vector<Index> m_parentIndices;
	std::vector<Index> m_rootIndices;
	std::vector<std::vector<Index>> m_childrenIndices;
	std::vector<bool> m_occupied;
	std::uint64_t m_revision{ 1 };   ///< 0 은 "아직 아무것도 못 봤다" 로 남겨 둔다
};
