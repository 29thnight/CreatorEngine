#pragma once
#include "ImGuiRegister.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "imgui-node-editor/imgui_node_editor.h"
#include <string>
#include <vector>
namespace ed = ax::NodeEditor;

struct Link
{
	std::string fromNode;
	std::string toNode;
	bool haveReverse = false;
};
struct Node
{
	std::string name;

};


class NodeEditor
{
public:
	NodeEditor() = default;
	~NodeEditor() = default;
	void MakeEdit(std::string filePath);//������  or �ҷ��� json�̸� ������ �������
	void EndEdit();
	void DrawNode(std::string nodeName); 
	void DrawLink(std::string fromNodeName,std::string toNodeName,std::string LineName);


	ax::NodeEditor::EditorContext* m_nodeContext;

	std::vector<Node*> Nodes;
	std::vector<Link*> Links;
};

