#include "NodeEditor.h"
namespace ed = ax::NodeEditor;
std::optional<std::filesystem::path> NodeEditor::SettingsPath(
    const std::filesystem::path& directory, ce::utf8_string_view filename)
{
    if (filename.empty() || filename.find(ce::utf8_string_view("\0", 1)) != ce::utf8_string_view::npos)
        return std::nullopt;
    ce::string validated;
    if (!ce::to_utf16(filename, validated)) return std::nullopt;
    return directory / std::filesystem::path(
        std::u8string(filename.data(), filename.data() + filename.size()));
}

NodeEditor::~NodeEditor()
{
    ReleaseContext();
    for (auto link : Links) delete link;
    for (auto node : Nodes) delete node;
}

void NodeEditor::ReleaseContext()
{
    if (!m_nodeContext) return;
    if (ed::GetCurrentEditor() == m_nodeContext) ed::SetCurrentEditor(nullptr);
    ed::DestroyEditor(m_nodeContext);
    m_nodeContext = nullptr;
}

std::size_t NodeEditor::LoadSettings(char* data, void* userPointer)
{
    auto& editor = *static_cast<NodeEditor*>(userPointer);
    if (data == nullptr)
    {
        editor.m_loadedSettings.clear();
        std::ifstream file(editor.m_filePath, std::ios::binary | std::ios::ate);
        if (!file) return 0;
        const auto length = file.tellg();
        if (length <= 0 || static_cast<std::uintmax_t>(length) >
            static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)())) return 0;
        file.seekg(0, std::ios::beg);
        editor.m_loadedSettings.resize_and_overwrite(static_cast<std::size_t>(length),
            [&](char* destination, std::size_t size) {
                file.read(destination, static_cast<std::streamsize>(size));
                return file ? size : std::size_t{};
            });
    }
    else if (!editor.m_loadedSettings.empty())
    {
        std::memcpy(data, editor.m_loadedSettings.data(), editor.m_loadedSettings.size());
    }
    return editor.m_loadedSettings.size();
}

bool NodeEditor::SaveSettings(const char* data, std::size_t size,
    ed::SaveReasonFlags, void* userPointer)
{
    if ((data == nullptr && size != 0) || size >
        static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) return false;
    auto& editor = *static_cast<NodeEditor*>(userPointer);
    std::ofstream file(editor.m_filePath, std::ios::binary | std::ios::trunc);
    if (size != 0) file.write(data, static_cast<std::streamsize>(size));
    file.flush();
    return static_cast<bool>(file);
}

bool NodeEditor::MakeEdit(const std::filesystem::path& filePath)
{
    if (filePath.empty()) return false;
    if (!m_nodeContext || m_filePath != filePath)
    {
        // Flush the old context while its native path is still the active one.
        ReleaseContext();
        m_filePath = filePath;
        m_loadedSettings.clear();
        m_selectedType = SelectedType::None;
        seletedCurNodeIndex = -1;
        seletedCurLinkIndex = -1;
        needMakeLink = false;
        m_retrunIndex = nullptr;
        ed::Config config;
        config.SettingsFile = nullptr;
        config.LoadSettings = &NodeEditor::LoadSettings;
        config.SaveSettings = &NodeEditor::SaveSettings;
        config.UserPointer = this;
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
    return true;
}

void NodeEditor::EndEdit()
{
	ed::End();
	ed::SetCurrentEditor(nullptr);
}

void NodeEditor::MakeNode(ce::utf8_string_view nodeName)
{
	Node* newNode = new Node();
	newNode->name.assign(nodeName);
    newNode->id = startNodeId++;
	Nodes.push_back(newNode);
}

void NodeEditor::MakeLink(ce::utf8_string_view fromNodeName, ce::utf8_string_view toNodeName)
{
    Node* from = FindNode(fromNodeName);
    Node* to = FindNode(toNodeName);
    if (!from || !to) return;
    auto* newLink = new NodeLink{from, to};
    for (const auto* link : Links)
    {
        if (link->toNode == from && link->fromNode == to)
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
		ImGui::TextUnformatted(node->name.data(), node->name.data() + node->name.size());
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
    if (needMakeLink && (seletedCurNodeIndex < 0 ||
        static_cast<std::size_t>(seletedCurNodeIndex) >= Nodes.size()))
    {
        needMakeLink = false;
        m_retrunIndex = nullptr;
    }
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



bool NodeEditor::ReNameJson(const std::filesystem::path& filePath)
{
    if (filePath.empty()) return false;
    if (m_filePath == filePath) return true;
    std::error_code error;
    if (std::filesystem::exists(filePath, error) || error) return false;
    if (!m_filePath.empty())
    {
        const bool exists = std::filesystem::exists(m_filePath, error);
        if (error) return false;
        if (exists)
        {
            std::filesystem::rename(m_filePath, filePath, error);
            if (error) return false;
        }
    }
    // The callbacks consult this owner instead of retaining a pointer into a
    // reallocated UTF-8 filename. Renaming preserves the live graph layout.
    m_filePath = filePath;
    return true;
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

Node* NodeEditor::FindNode(ce::utf8_string_view nodeName)
{
    for (auto& node : Nodes)
    {
        if (node->name.view() == nodeName)
        {
            return node;
        }
    }
    return nullptr;
}




