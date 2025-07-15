#pragma once
#include "BTEnum.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "BTBuildNode.generated.h"

namespace ed = ax::NodeEditor;

struct BTBuildNode
{
   ReflectBTBuildNode
	[[Serializable]]
	BTBuildNode() = default;
	~BTBuildNode() = default;

	[[Property]]
	HashedGuid				ID; // 유니크 (uuid 등), ex) "node_001"
	[[Property]]
	BehaviorNodeType		Type; // 예: "Selector", "Wait", "MoveTo"
	[[Property]]
	std::string				Name; // UI 표시용
	[[Property]]
	HashedGuid				ParentID; // 연결 정보 (하위에 있는 노드가 부모를 참조)
	[[Property]]
	bool 					IsRoot; // 루트 노드 여부
	[[Property]]
	bool 					HasScript; // 스크립트 노드 여부("Action", "Condition")
	[[Property]]
	std::string 			ScriptName; // 스크립트 이름
	[[Property]]
	ParallelPolicy			Policy; // 병렬 실행 정책

	[[Property]]
	std::vector<HashedGuid> Children; // 자식 노드들

	[[Property]]
	Mathf::Vector2	Position; // 노드 위치 (에디터용)
	ed::PinId		InputPinId{};
	ed::PinId		OutputPinId{};
	ImVec2          PositionEditor{}; // 에디터에서의 위치

	std::string State;
};
