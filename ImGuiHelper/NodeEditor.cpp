#include "NodeEditor.h"

namespace ed = ax::NodeEditor;
void NodeEditor::MakeEdit(std::string filePath)
{
	if (!m_nodeContext)
	{
		ed::Config config;
		config.SettingsFile = filePath.c_str();
		m_nodeContext = ed::CreateEditor(&config);
	}
	ed::SetCurrentEditor(m_nodeContext);
	ed::Begin("Node Editor");
}

void NodeEditor::EndEdit()
{
	ed::End();
	ed::SetCurrentEditor(nullptr);

	for (auto node : Nodes)
	{
		delete node;
	}
	Nodes.clear();

	for (auto link : Links)
	{
		delete link;
	}
	Links.clear();
}

void NodeEditor::DrawNode(std::string nodeName)
{
	ed::Style& style = ed::GetStyle();
	style.LinkStrength = 0.0f;
	ed::NodeId nodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(nodeName));
	ed::BeginNode(nodeID);
	ImGui::Text(nodeName.c_str());


	ed::PinId inputPinID = ed::PinId((static_cast<uint64_t>(nodeID) << 1) | 0);
	ed::PinId outputPinID = ed::PinId((static_cast<uint64_t>(nodeID) << 1) | 1);
	//ed::BeginPin(inputPinID, ed::PinKind::Input);
	//ImGui::Dummy(ImVec2(1, 1));
	//ed::EndPin();

	//ed::BeginPin(outputPinID, ed::PinKind::Output);
	//ImGui::Dummy(ImVec2(1, 1));
	//ed::EndPin();

	ed::EndNode();


	Node* newNode = new Node();
	newNode->name = nodeName;
	Nodes.push_back(newNode);
	
}

void NodeEditor::DrawLink(std::string fromNodeName, std::string toNodeName, std::string LineName)
{
	Link* newLink = new Link();
	newLink->fromNode = fromNodeName;
	newLink->toNode = toNodeName;

	ed::NodeId nodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(fromNodeName));
	ed::NodeId nextnodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(toNodeName));

	ImVec2 p1 = ed::GetNodePosition(nodeID);
	ImVec2 p2 = ed::GetNodePosition(nextnodeID);

	ImVec2 dir = p2 - p1;
	float length = sqrt(dir.x * dir.x + dir.y * dir.y);
	if (length < 1.0f)
		return;

	ImVec2 normDir = ImVec2(dir.x / length, dir.y / length);
	ImVec2 perpendicular = ImVec2(-normDir.y, normDir.x);

	// 이미 역방향이 있는지 확인
	bool isReverse = false;
	for (auto& link : Links)
	{
		if (link->toNode == fromNodeName && link->fromNode == toNodeName)
		{
			isReverse = true;
			break;
		}
	}

	// 오프셋
	float offsetAmount = 5.0f;
	ImVec2 offset = isReverse ? perpendicular * offsetAmount : perpendicular * -offsetAmount;

	// 제어점 계산 (Bezier용)
	ImVec2 p1_offset = p1 + offset;
	ImVec2 p2_offset = p2 + offset;

	// 방향 따라 제어점 거리 설정 (0.3 곱은 보통 Bezier 표준 비율)
	ImVec2 cp1 = p1_offset + normDir * (length * 0.3f);
	ImVec2 cp2 = p2_offset - normDir * (length * 0.3f);

	// 그리기
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	//ImU32 color = IM_COL32(255, 255, 255, 255);
	ImU32 color = isReverse ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
	float thickness = 0.5f;

	drawList->AddBezierCubic(p1_offset, cp1, cp2, p2_offset, color, thickness);

	Links.push_back(newLink);
}



