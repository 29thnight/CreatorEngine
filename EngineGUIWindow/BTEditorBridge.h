#pragma once
#include "BTEnum.h"
#include <imgui.h>
#include "imgui-node-editor/imgui_node_editor.h"

// BT 저작 그래프의 node-editor 접합부 (E3-5).
//
// PinId는 저장하지 않는다 — 노드 ID에서 순수 유도된다(input = ID<<1,
// output = ID<<1 | 1). 저작 데이터(BTBuildNode)는 편집기 세션을 모른 채
// ID·구조만 들고, 핀·화면 좌표 같은 편집기 상태는 이 접합부와 편집기 창이
// 소유한다.
namespace ed = ax::NodeEditor;

namespace BTEd
{
	inline ed::PinId InputPin(const HashedGuid& id)
	{
		return ed::PinId(id.m_ID_Data << 1);
	}

	inline ed::PinId OutputPin(const HashedGuid& id)
	{
		return ed::PinId((id.m_ID_Data << 1) | 1);
	}

	inline ed::NodeId GetTreeNodeIdFromPin(ed::PinId pin)
	{
		return ed::NodeId(reinterpret_cast<void*>((uintptr_t)pin.Get() >> 1));
	}

	inline bool IsInputPin(ed::PinId pin)
	{
		return (pin.Get() & 1) == 0; // Input pins have even IDs
	}

	inline ImVec2 ToImVec2(const Mathf::Vector2& vec)
	{
		return ImVec2(vec.x, vec.y);
	}

	inline Mathf::Vector2 ToMathfVec2(const ImVec2& vec)
	{
		return Mathf::Vector2(vec.x, vec.y);
	}
}
