#pragma once
#include <cstdint>

// 씬 그래프 노드의 런타임 정체성 (SceneGraphRedesignPlan §2.3, 트랙 E1).
//
// ScriptObjectHandle(ScriptObjectRegistry.h)과 배치가 같다 — uint32 두 개,
// generation==0이 무효다. 값(세대 시퀀스)까지 하나로 합치는 안은 트랙 E4에서
// 검토 후 기각했다 — EntityHandle은 씬 스코프(서로 다른 씬의 같은 index가
// 존재)인데 ScriptObjectHandle은 전역이어야 하고, 무엇보다 DontDestroyOnLoad
// 이송 창(Scene::DetachGameObjectHierarchy가 슬롯을 놓고 AttachExistingGameObject*
// 가 다시 잡기 전 구간)에서 그 오브젝트는 어느 씬의 EntityHandle도 갖지 않는데
// 그 창에서 실제로 관리 코드(BehaviourRegistry.SweepOrphans)가 도는 것을 코드
// 추적으로 확인했다(ScriptObjectRegistry.h 상단 주석에 전말). 그래서 두 핸들은
// 배치만 같고 세대 시퀀스는 계속 별개다 — ScriptObjectRegistry가 자기 세대를
// GameObject::Destroy() 한 지점에서만 올린다.
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
