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

void NodeEditor::MakeNode(std::string nodeName)
{
	Node* newNode = new Node();
	newNode->name = nodeName;
	Nodes.push_back(newNode);
}

void NodeEditor::MakeLink(std::string fromNodeName, std::string toNodeName, std::string LineName)
{
	Link* newLink = new Link();
	newLink->fromNode = fromNodeName;
	newLink->toNode = toNodeName;


	for (auto& link : Links)
	{
		if (link->toNode == fromNodeName && link->fromNode == toNodeName)
		{
			newLink->haveReverse = true;
			break;
		}
	}

	Links.push_back(newLink);
}

void NodeEditor::DrawNode()
{
	ed::Style& style = ed::GetStyle();
	style.LinkStrength = 0.0f;

	for (auto& node : Nodes)
	{
		ed::NodeId nodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(node->name));
		ed::BeginNode(nodeID);
		ImGui::Text(node->name.c_str());
		ed::EndNode();
	}

}


void NodeEditor::DrawLink(int* selectedLinkIndex)
{
    for (size_t i = 0; i < Links.size(); ++i)
    {
        auto& link = Links[i];

        ed::NodeId nodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(link->fromNode));
        ed::NodeId nextnodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(link->toNode));

        ImVec2 pos = ed::GetNodePosition(nodeID);
        ImVec2 size = ed::GetNodeSize(nodeID);
        ImVec2 p1 = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

        ImVec2 pos2 = ed::GetNodePosition(nextnodeID);
        ImVec2 size2 = ed::GetNodeSize(nextnodeID);
        ImVec2 p2 = ImVec2(pos2.x + size2.x * 0.5f, pos2.y + size2.y * 0.5f);

        ImVec2 dir = p2 - p1;
        float length = sqrt(dir.x * dir.x + dir.y * dir.y);
        if (length < 1.0f)
            continue;  

        ImVec2 normDir = ImVec2(dir.x / length, dir.y / length);
        ImVec2 perpendicular = ImVec2(-normDir.y, normDir.x);

        float offsetAmount = 5.0f;
        ImVec2 offset = perpendicular * offsetAmount;

        // Bezier 제어점
        ImVec2 p1_offset = p1 + offset;
        ImVec2 p2_offset = p2 + offset;
        ImVec2 cp1 = p1_offset + normDir * (length * 0.3f);
        ImVec2 cp2 = p2_offset - normDir * (length * 0.3f);

        // 그리기
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 color = link->haveReverse ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 255, 255);

        if (*selectedLinkIndex == i)
        {
            color = IM_COL32(255, 255, 0, 255);
        }
        float thickness = 3.0f;
        drawList->AddBezierCubic(p1_offset, cp1, cp2, p2_offset, color, thickness);

        bool hovered = IsMouseNearLink(p1_offset, cp1, cp2, p2_offset,5.0f);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (selectedLinkIndex)
                *selectedLinkIndex = i;  // 선택한 링크 인덱스 기록
        }
 
    }
}

bool NodeEditor::IsMouseNearLink(const ImVec2& p1, const ImVec2& cp1, const ImVec2& cp2, const ImVec2& p2, float threshold)
{
    ImVec2 mousePos = ImGui::GetMousePos();
    const int segments = 50;

    ImVec2 last = p1;
    for (int i = 1; i <= segments; ++i)
    {
        float t = (float)i / segments;
        // 베지어 곡선 점 계산
        ImVec2 a = ImLerp(p1, cp1, t);
        ImVec2 b = ImLerp(cp1, cp2, t);
        ImVec2 c = ImLerp(cp2, p2, t);
        ImVec2 d = ImLerp(a, b, t);
        ImVec2 e = ImLerp(b, c, t);
        ImVec2 point = ImLerp(d, e, t);

        // 마우스에서 현재 구간까지 거리 계산
        float dx = mousePos.x - point.x;
        float dy = mousePos.y - point.y;
        float distSq = dx * dx + dy * dy;

        if (distSq < threshold * threshold)
            return true;

        last = point;
    }
    return false;
}



