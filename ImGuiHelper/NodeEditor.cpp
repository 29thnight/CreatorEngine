#include "NodeEditor.h"

namespace ed = ax::NodeEditor;
void NodeEditor::MakeEdit(std::string filePath)
{
    std::string _filepath = "NodeEditor\\" + filePath;
    if (!m_nodeContext || m_filePath != _filepath)
    {
        if (m_nodeContext)
        {
            ed::DestroyEditor(m_nodeContext);
            m_nodeContext = nullptr;
        }

        ed::Config config;
        m_filePath = _filepath;
        config.SettingsFile = m_filePath.c_str();

        m_nodeContext = ed::CreateEditor(&config);
    }

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
	ed::SetCurrentEditor(m_nodeContext);
	ed::Begin("Node Editor");
    startNodeId = 1000;
}

void NodeEditor::EndEdit()
{
	ed::End();
	ed::SetCurrentEditor(nullptr);
}

void NodeEditor::MakeNode(std::string nodeName)
{
	Node* newNode = new Node();
	newNode->name = nodeName;
    newNode->id = startNodeId++;
	Nodes.push_back(newNode);
}

void NodeEditor::MakeLink(std::string fromNodeName, std::string toNodeName, std::string LineName)
{
	Link* newLink = new Link();
	newLink->fromNode = FindNode(fromNodeName);
	newLink->toNode = FindNode(toNodeName);


	for (auto& link : Links)
	{
		if (link->toNode == FindNode(fromNodeName) && link->fromNode == FindNode(toNodeName))
		{
			newLink->haveReverse = true;
			break;
		}
	}

	Links.push_back(newLink);
}



void NodeEditor::DrawNode(int* selectedNodeIndex)
{
	ed::Style& style = ed::GetStyle();
	style.LinkStrength = 0.0f;

    
	for (size_t i = 0; i < Nodes.size(); ++i)
	{
        auto& node = Nodes[i];
        ed::NodeId nodeID = node->id;
		ed::BeginNode(nodeID);
		ImGui::Text(node->name.c_str());
		ed::EndNode();


        if (needMakeLink == false &&  ed::GetHoveredNode() == nodeID && ImGui::IsMouseClicked(1))
        {
            if (selectedNodeIndex)
            {
                *selectedNodeIndex = i;  // 선택한 링크 인덱스 기록
            }
            seletedCurNodeIndex = i;
            m_selectedType = SelectedType::Node;
        }
        else if (needMakeLink == false &&  ed::GetHoveredNode() == nodeID && ImGui::IsMouseClicked(0))
        {
            seletedCurNodeIndex = i;
            m_selectedType = SelectedType::Node;
        }
	}

}


void NodeEditor::DrawLink(int* selectedLinkIndex)
{
    for (size_t i = 0; i < Links.size(); ++i)
    {
        auto& link = Links[i];

        ed::NodeId nodeID = link->fromNode->id;
        ed::NodeId nextnodeID = link->toNode->id;

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
        //ImU32 color = link->haveReverse ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
        ImU32 color = IM_COL32(255, 255, 255, 255);
        
        if (selectedLinkIndex != nullptr && *selectedLinkIndex == i)
        {
            color = IM_COL32(255, 255, 0, 255);
        }
        float thickness = 3.0f;
        drawList->AddBezierCubic(p1_offset, cp1, cp2, p2_offset, color, thickness);

        float t = 0.5f;
        ImVec2 midPos = ImBezierCubicCalc(p1_offset, cp1, cp2, p2_offset, t);
        ImVec2 tangent =
            ImBezierCubicCalcDerivative(p1_offset, cp1, cp2, p2_offset, t);
        float tangentLength = sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        if (tangentLength > 0.0001f)
            tangent = ImVec2(tangent.x / tangentLength, tangent.y / tangentLength);
        float triSize = 10.0f;
        ImVec2 triP1 = midPos + tangent * triSize; // 앞쪽 꼭짓점
        ImVec2 triP2 = midPos - tangent * triSize * 0.5f + ImVec2(-tangent.y, tangent.x) * triSize * 0.5f; // 좌측 뒤쪽
        ImVec2 triP3 = midPos - tangent * triSize * 0.5f + ImVec2(tangent.y, -tangent.x) * triSize * 0.5f; // 우측 뒤쪽
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 triP1_screen = triP1 + windowPos;
        ImVec2 triP2_screen = triP2 + windowPos;
        ImVec2 triP3_screen = triP3 + windowPos;

        drawList->AddTriangleFilled(triP1_screen, triP2_screen, triP3_screen, color);
        bool hovered = IsMouseNearLink(p1_offset, cp1, cp2, p2_offset,5.0f);
        if (hovered && ImGui::IsMouseClicked(0))
        {
            if (selectedLinkIndex)
                *selectedLinkIndex = i;  
            m_selectedType = SelectedType::Link;
        }
 
    }
}

void NodeEditor::Update()
{
    if (needMakeLink == true && seletedCurNodeIndex != -1)
    {
        ed::NodeId nodeID = Nodes[seletedCurNodeIndex]->id;
        ImVec2 pos = ed::GetNodePosition(nodeID);
        ImVec2 size = ed::GetNodeSize(nodeID);
        ImVec2 p1 = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImVec2 p2 = ImGui::GetMousePos();
        ImVec2 dir = p2 - p1;
        float length = sqrt(dir.x * dir.x + dir.y * dir.y);
        ImVec2 normDir = ImVec2(dir.x / length, dir.y / length);
        ImVec2 perpendicular = ImVec2(-normDir.y, normDir.x);
        float offsetAmount = 5.0f;
        ImVec2 offset = perpendicular * offsetAmount;

        ImVec2 p1_offset = p1 + offset;
        ImVec2 p2_offset = p2 + offset;
        ImVec2 cp1 = p1_offset + normDir * (length * 0.3f);
        ImVec2 cp2 = p2_offset - normDir * (length * 0.3f);
        ImU32 color = IM_COL32(255, 255, 255, 255);
        float thickness = 3.0f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddBezierCubic(p1_offset, cp1, cp2, p2_offset, color, thickness);

     
        if(ImGui::IsMouseClicked(0))
        {
            ed::NodeId hovered = ed::GetHoveredNode();

            if (hovered)
            {
                for (size_t i = 0; i < Nodes.size(); ++i)
                {
                    auto& node = Nodes[i];
                    ed::NodeId nodeID = node->id;
                    if (nodeID == hovered)
                    {
                        if (i != seletedCurNodeIndex)
                        {
                            if (m_retrunIndex)
                                *m_retrunIndex = i;

                            needMakeLink = false;
                            m_retrunIndex = nullptr;
                        }
                        break;
                    }
                }
            }
            else
            {
                // 어떤 노드에도 마우스가 올라가 있지 않음
                needMakeLink = false;
                m_retrunIndex = nullptr;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            needMakeLink = false;
        }
    }




}



void NodeEditor::MakeNewLink(int* returnIndex)
{
    needMakeLink = true;
    m_retrunIndex = returnIndex;
}



void NodeEditor::ReNameJson(std::string filepath)
{
    std::string _filepath = "NodeEditor\\" + filepath;

    // 이전 파일이 존재하면 새 경로로 복사 or 이동
    if (std::filesystem::exists(m_filePath))
    {
        std::filesystem::rename(m_filePath, _filepath);
        m_filePath = _filepath;
    }

    // 파일 경로 업데이트
    //m_filePath = newFilePath;

    // NodeEditor가 이미 열려있다면 새 파일 경로로 저장 설정 변경
   /* if (m_nodeContext)
    {
        ed::Config config = ed::GetConfig(m_nodeContext);
        config.SettingsFile = m_filePath.c_str();
    }*/
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

ImVec2 NodeEditor::ImBezierCubicCalcDerivative(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t)
{
    float u = 1.0f - t;
    return
        ImVec2(3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x),
            3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y));
}

Node* NodeEditor::FindNode(std::string nodeName)
{
    for (auto& node : Nodes)
    {
        if (node->name == nodeName)
            return node;
    }
}




