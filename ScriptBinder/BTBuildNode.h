#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "BTEnum.h"
#include "imgui-node-editor/imgui_node_editor.h"

namespace ed = ax::NodeEditor;

struct BTBuildNode
{
   static consteval auto describe()
   {
       return meta::describe<BTBuildNode>(
           meta::member<&BTBuildNode::ID>(),
           meta::member<&BTBuildNode::Type>(),
           meta::member<&BTBuildNode::Name>(),
           meta::member<&BTBuildNode::ParentID>(),
           meta::member<&BTBuildNode::IsRoot>(),
           meta::member<&BTBuildNode::HasScript>(),
           meta::member<&BTBuildNode::ScriptName>(),
           meta::member<&BTBuildNode::Policy>(),
           meta::member<&BTBuildNode::Children>(),
           meta::member<&BTBuildNode::ChildWeights>(),
           meta::member<&BTBuildNode::Position>());
   }
	BTBuildNode() = default;
	~BTBuildNode() = default;

	HashedGuid				ID; // 유니크 (uuid 등), ex) "node_001"
	BehaviorNodeType		Type; // 예: "Selector", "Wait", "MoveTo"
	std::string				Name; // UI 표시용
	HashedGuid				ParentID; // 연결 정보 (하위에 있는 노드가 부모를 참조)
	bool 					IsRoot; // 루트 노드 여부
	bool 					HasScript; // 스크립트 노드 여부("Action", "Condition")
	std::string 			ScriptName; // 스크립트 이름
	ParallelPolicy			Policy; // 병렬 실행 정책

	std::vector<HashedGuid> Children; // 자식 노드들

	std::vector<float>	ChildWeights; // 가중치 (WeightedSelector용)

	Mathf::Vector2	Position; // 노드 위치 (에디터용)
	ed::PinId		InputPinId{};
	ed::PinId		OutputPinId{};
	ImVec2          PositionEditor{}; // 에디터에서의 위치

	std::string State;
};
