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
	void MakeEdit(std::string filePath);//저장할  or 불러올 json이름 없으면 만들어짐
	void EndEdit();
	void MakeNode(std::string nodeName);
	void MakeLink(std::string fromNodeName, std::string toNodeName, std::string LineName);
	void DrawNode(int* selectedNodeIndex = nullptr);
	void DrawLink(int* selectedLinkIndex = nullptr);

	void Update();


	void MakeNewLink(int* returnIndex);


	bool IsMouseNearLink(const ImVec2& p1, const ImVec2& cp1, const ImVec2& cp2, const ImVec2& p2, float threshold = 5.0f);
	ImVec2 ImBezierCubicCalcDerivative(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t);

	int seletedCurNodeIndex = -1;
	ax::NodeEditor::EditorContext* m_nodeContext = nullptr;
	std::string m_filePath;
	std::vector<Node*> Nodes;
	std::vector<Link*> Links;
	
	int* m_retrunIndex = nullptr; 
	bool needMakeLink = false;
};

