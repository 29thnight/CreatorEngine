#pragma once
#include <cstdint>

// 씬 그래프 노드의 런타임 정체성 (SceneGraphRedesignPlan §2.3, 트랙 E1).
//
// ScriptObjectHandle(ScriptObjectRegistry.h)과 배치가 같다 — uint32 두 개,
// generation==0이 무효다. 지금은 Scene 자체 슬롯 테이블(m_SceneObjects +
// m_generations)의 정체성이고, ScriptObjectRegistry의 핸들과는 아직 별개
// 테이블이다 — 두 체계의 통합은 트랙 E4 소관이라 여기서는 배치만 맞춰 둔다.
struct EntityHandle
{
	uint32_t index{ 0 };
	uint32_t generation{ 0 };   // 0 = 무효

	bool IsValid() const { return generation != 0; }

	bool operator==(const EntityHandle& other) const
	{
		return index == other.index && generation == other.generation;
	}
	bool operator!=(const EntityHandle& other) const
	{
		return !(*this == other);
	}
};
