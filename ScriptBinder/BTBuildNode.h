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
	HashedGuid				ID; // ����ũ (uuid ��), ex) "node_001"
	[[Property]]
	BehaviorNodeType		Type; // ��: "Selector", "Wait", "MoveTo"
	[[Property]]
	std::string				Name; // UI ǥ�ÿ�
	[[Property]]
	HashedGuid				ParentID; // ���� ���� (������ �ִ� ��尡 �θ� ����)
	[[Property]]
	bool 					IsRoot; // ��Ʈ ��� ����
	[[Property]]
	bool 					HasScript; // ��ũ��Ʈ ��� ����("Action", "Condition")
	[[Property]]
	std::string 			ScriptName; // ��ũ��Ʈ �̸�
	[[Property]]
	ParallelPolicy			Policy; // ���� ���� ��å

	[[Property]]
	std::vector<HashedGuid> Children; // �ڽ� ����

	[[Property]]
	Mathf::Vector2	Position; // ��� ��ġ (�����Ϳ�)
	ed::PinId		InputPinId{};
	ed::PinId		OutputPinId{};
	ImVec2          PositionEditor{}; // �����Ϳ����� ��ġ

	std::string State;
};
