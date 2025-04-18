#include "BT_Editor.h"
#include "InputManager.h"
#include "PathFinder.h"
#include <regex>
#include "Benchmark.hpp"
#include "widgets.h"
#include "Core.Minimal.h"
#include <ranges>

int BT_Editor::g_NextId = 1;

ImColor GetIconColor(NodeType type)
{
    switch (type)
    {
    default:
	case NodeType::Root:      return ImColor(255, 255, 255);
	case NodeType::Condition: return ImColor(255, 147, 48);
	case NodeType::Action:    return ImColor(220, 48, 48);
	case NodeType::Selector:  return ImColor(68, 201, 156);
	case NodeType::Sequence:  return ImColor(147, 226, 74);
	case NodeType::Parallel:  return ImColor(124, 21, 153);
    }
};
using ax::Widgets::IconType;
const int m_PinIconSize = 24;

void DrawPinIcon(const NodeType& pin, bool connected, int alpha)
{
    IconType iconType;
    ImColor  color = GetIconColor(pin);
    color.Value.w = alpha / 255.0f;
    switch (pin)
    {
	case NodeType::Root:      iconType = IconType::Flow; break;
	case NodeType::Condition: iconType = IconType::Circle; break;
	case NodeType::Action:    iconType = IconType::Circle; break;
	case NodeType::Selector:  iconType = IconType::Circle; break;
	case NodeType::Sequence:  iconType = IconType::Circle; break;
	case NodeType::Parallel:  iconType = IconType::Circle; break;
    default:
        return;
    }

    ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
};

// Helper: 노드 타입을 문자열로 변환
std::string NodeTypeToString(NodeType type)
{
    switch (type)
    {
    case NodeType::Root: return "Root";
    case NodeType::Condition: return "Condition";
    case NodeType::Action: return "Action";
    case NodeType::Selector: return "Selector";
    case NodeType::Sequence: return "Sequence";
    case NodeType::Parallel: return "Parallel";
    default: return "Unknown";
    }
}

void BT_Editor::AddNode(NodeType type, const std::string& name)
{
    int nodeId = g_NextId++;
    NodeData node = { nodeId, type, name, {}, {} };

    // 핀 추가
    if (type == NodeType::Action) 
    {
        node.inputPins.push_back(g_NextId++);
    }
	else if (type == NodeType::Root)
	{
		node.outputPins.push_back(g_NextId++);
	}
    else 
    {
        node.inputPins.push_back(g_NextId++);
        node.outputPins.push_back(g_NextId++);
    }

    g_Nodes[nodeId] = node;
}

void BT_Editor::NodeEditor()
{
    ed::SetCurrentEditor(_context);
    ed::Begin("BehaviorTreeEditor");

    // 노드 렌더링
    for (auto& [id, node] : g_Nodes) 
    {
        ed::BeginNode(node.id);
        ImGui::Text("%s", node.name.c_str());

        // 입력 핀
        for (auto& pin : node.inputPins) 
        {
            ed::BeginPin(pin, ed::PinKind::Input);
            bool found = false;
			for (auto& [outPin, inPin] : g_Links)
			{
				std::ranges::for_each(inPin, [&](int Ipin)
				{
					if (Ipin == pin)
					{
						found = true;
					}
				});
			}

			DrawPinIcon(node.type, found, 255);
            if (ImGui::IsItemHovered())
            {
                if (InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
                {
                    if (g_SelectedOutputPin != -1)
                    {
                        g_SelectedInputPin = pin; // 연결할 입력 핀 선택
                        HandlePinConnection();
                    }
                }
            }
            ed::EndPin();
            ImGui::SameLine();
            ImGui::Text("In");
        }

        // 출력 핀
        for (auto& pin : node.outputPins) 
        {
            ImGui::Text("Out");
            ImGui::SameLine();
            ed::BeginPin(pin, ed::PinKind::Output);
			bool hasLink = g_Links.find(pin) != g_Links.end();
            DrawPinIcon(node.type, hasLink, 255);
            // 핀 클릭 감지
            if (ImGui::IsItemHovered())
            {
                if (InputManagement->IsMouseButtonPressed(MouseKey::LEFT))
                {
                    g_SelectedOutputPin = pin; // 연결할 출력 핀 선택
                }
            }
            ed::EndPin();
        }

        ed::EndNode();
    }

    int linkID = 0;
    // 링크 렌더링
    for (auto& [outPin, inPin] : g_Links) 
    {
        std::ranges::for_each(inPin, [&](int pin)
        {
			ed::Link(linkID++, outPin, pin);
        });
    }

    ed::End();
}

void BT_Editor::ShowMainUI()
{
    return;

  //  if (!_world->IsCreateBT())
  //  {
		//if(!g_Nodes.empty() && !g_Links.empty())
  //      {
  //          g_Nodes.clear();
  //          g_Links.clear();
		//	g_NextId = 1;
		//	AddNode(NodeType::Root, "Root");
  //      }
  //      return;
  //  }

  //  if (ImGui::Button("Add Node"))
  //  {
  //      ImGui::OpenPopup("Add Node");
  //  }

  //  ImGui::SameLine();

  //  if (ImGui::Button("Build BT Script"))
  //  {
  //      ImGui::OpenPopup("BuildSetting Script");
  //  }

  //  ImGui::SameLine();

  //  if (ImGui::Button("Load BT Script"))
  //  {
  //      ImGui::OpenPopup("Load Script");
  //  }

  //  if (!_isLoadBt)
  //  {
  //      ImGui::Text("Loading Behavior Tree...");
  //      return;
  //  }

  //  ShowAddNodePopup();

  //  ShowBuildSettingLuaPopup();

  //  NodeEditor();
}

void BT_Editor::HandlePinConnection()
{
    if (g_SelectedOutputPin != -1 && g_SelectedInputPin != -1) 
    {
        // 링크를 생성
        g_Links[g_SelectedOutputPin].push_back(g_SelectedInputPin);
        // 연결된 상태 초기화
        g_SelectedOutputPin = -1;
        g_SelectedInputPin = -1;
    }
}

void BT_Editor::ShowAddNodePopup()
{
    static NodeType selectedNodeType = NodeType::Condition;
    static char nodeName[64] = "";

    if (ImGui::BeginPopup("Add Node")) 
    {
        // 노드 타입 선택 (드롭다운)
        const char* nodeTypeNames[] = { "RootNode", "Condition", "Action", "Selector", "Sequence", "Parallel" };
        const int nodeTypeCount = sizeof(nodeTypeNames) / sizeof(nodeTypeNames[0]);
        const char* currentNodeTypeName = nodeTypeNames[static_cast<int>(selectedNodeType)];

        if (ImGui::BeginCombo("Node Type", currentNodeTypeName)) 
        {
            for (int i = 0; i < nodeTypeCount; ++i) 
            {
                bool isSelected = (selectedNodeType == static_cast<NodeType>(i));
                if (ImGui::Selectable(nodeTypeNames[i], isSelected)) 
                {
                    selectedNodeType = static_cast<NodeType>(i);
                }
            }
            ImGui::EndCombo();
        }

        // 노드 추가 버튼
        if (ImGui::Button("Add Node")) 
        {
            AddNode(selectedNodeType, currentNodeTypeName);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void BT_Editor::ShowBuildSettingLuaPopup()
{
	static char luaPath[256] = "BehaviorTree.lua";
	if (ImGui::BeginPopup("BuildSetting Script"))
	{
		ImGui::InputText("Output Path", luaPath, sizeof(luaPath));
		if (ImGui::Button("Build"))
		{
			BuildTreeToLua(PathFinder::Relative("BT\\").string() + luaPath);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::Button("Close"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	static char luaInPath[256] = "BehaviorTree.lua";
	if (ImGui::BeginPopup("Load Script"))
	{
		ImGui::InputText("Input Path", luaInPath, sizeof(luaInPath));
		if (ImGui::Button("Load"))
        {
            if(_isLoadBt)
            {
                _loadBtThread = std::thread(
                    &BT_Editor::LoadTreeFromLua,
                    this,
                    PathFinder::Relative("BT\\").string() + luaInPath
                );
                _loadBtThread.detach();
            }
			//LoadTreeFromLua(PathFinder::Relative("BT\\").string() + luaInPath);
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button("Close"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void BT_Editor::BuildTreeToLua(const std::string& outPath)
{
    std::ostringstream luaScript;
    luaScript << "-- Auto-generated Lua script for Behavior Tree\n\n";

    // Lua 함수 선언부 생성
    luaScript << "-- Define Conditions and Actions\n";
    for (const auto& [id, node] : g_Nodes)
    {
        if (node.type == NodeType::Condition)
        {
            luaScript << "function Condition_" << id << "(tick)\n";
            luaScript << "    -- Implement condition logic here\n";
            luaScript << "    return true -- Placeholder\n";
            luaScript << "end\n\n";
        }
        else if (node.type == NodeType::Action)
        {
            luaScript << "function Action_" << id << "(tick)\n";
            luaScript << "    -- Implement action logic here\n";
            luaScript << "    print(\"Executing Action " << node.name << "\")\n";
            luaScript << "end\n\n";
        }
    }

    // 트리 노드 Lua 함수 생성
    std::unordered_map<int, std::string> nodeFunctions;
    for (const auto& [id, node] : g_Nodes)
    {
        std::string nodeFunc = "Node_" + std::to_string(id);

        luaScript << "-- Node: " << node.name << "\n";
        luaScript << "function " << nodeFunc << "(tick)\n";

        if (node.type == NodeType::Selector || node.type == NodeType::Sequence)
        {
            luaScript << "    for i, child in ipairs(Children_" << id << ") do\n";
            if (node.type == NodeType::Selector)
            {
                luaScript << "        if child(tick) then return true end\n";
            }
            else // Sequence
            {
                luaScript << "        if not child(tick) then return false end\n";
            }
            luaScript << "    end\n";
            luaScript << "    return " << (node.type == NodeType::Selector ? "false" : "true") << "\n";
        }
        else if (node.type == NodeType::Condition)
        {
            luaScript << "    return Condition_" << id << "(tick)\n";
        }
        else if (node.type == NodeType::Action)
        {
            luaScript << "    Action_" << id << "(tick)\n";
            luaScript << "    return true\n";
        }

        luaScript << "end\n\n";
        nodeFunctions[id] = nodeFunc;
    }

    // 자식 연결 정보 생성
    luaScript << "-- Define children connections\n";
    for (const auto& [id, node] : g_Nodes)
    {
        if (node.outputPins.empty()) continue;

        luaScript << "Children_" << id << " = {\n";
        for (int outputPin : node.outputPins)
        {
            for (const auto& [outPin, inPins] : g_Links)
            {
                if (outPin == outputPin)
                {
                    for (int inPin : inPins)
                    {
                        int connectedNodeId = -1;
                        for (const auto& [childId, childNode] : g_Nodes)
                        {
                            if (std::find(childNode.inputPins.begin(), childNode.inputPins.end(), inPin) != childNode.inputPins.end())
                            {
                                connectedNodeId = childId;
                                break;
                            }
                        }
                        if (connectedNodeId != -1)
                        {
                            luaScript << "    Node_" << connectedNodeId << ",\n";
                        }
                    }
                }
            }
        }
        luaScript << "}\n\n";
    }

    // 루트 노드 실행 함수 생성
    luaScript << "-- Entry point for Behavior Tree\n";
    luaScript << "function RunBehaviorTree(tick)\n";

	for (const auto& [id, node] : g_Nodes)
	{
		if (node.type == NodeType::Root)
		{
			luaScript << "    return Node_" << id << "(tick)\n";
			break;
		}
	}

    luaScript << "end\n";

    // Lua 스크립트를 파일로 저장
    std::ofstream outFile(outPath);
    if (outFile.is_open())
    {
        outFile << luaScript.str();
        outFile.close();
		Debug->Log("Behavior Tree successfully built to " + outPath);
    }
    else
    {
		Debug->LogError("Failed to write Lua script to " + outPath);
    }
}

void BT_Editor::LoadTreeFromLua(const std::string& inPath)
{
    _isLoadBt = false;
	std::ifstream inFile(inPath);
	if (!inFile.is_open())
	{
		Debug->LogError("Failed to open Lua file: " + inPath);
		_isLoadBt = true;
		return;
	}

	std::string line;
	std::string input((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
	std::regex nodeFuncRegex("--\\s*Node:\\s*(\\S+)\\s*\\n\\s*function\\s+Node_(\\d+)\\(tick\\)");
	std::regex childrenRegex("Children_(\\d+)\\s*=\\s*\\{([^}]*)\\}");
	std::smatch match;
	std::unordered_map<int, NodeType> nodeTypes;
	std::unordered_map<int, std::string> nodeNames;
	std::unordered_map<int, std::vector<int>> nodeLinks;


    auto begin = input.cbegin();
    auto end = input.cend();

    {
	Benchmark banchmark;
        while (std::regex_search(begin, end, match, nodeFuncRegex))
        {
            std::string nodeTypeStr = match[1].str();
            int nodeId = std::stoi(match[2].str());
            NodeType nodeType = NodeType::Root;
            if (nodeTypeStr == "Condition")
            {
                nodeType = NodeType::Condition;
            }
            else if (nodeTypeStr == "Action")
            {
                nodeType = NodeType::Action;
            }
            else if (nodeTypeStr == "Selector")
            {
                nodeType = NodeType::Selector;
            }
            else if (nodeTypeStr == "Sequence")
            {
                nodeType = NodeType::Sequence;
            }
            else if (nodeTypeStr == "Parallel")
            {
                nodeType = NodeType::Parallel;
            }
            nodeTypes[nodeId] = nodeType;
            nodeNames[nodeId] = nodeTypeStr;
            begin = match.suffix().first;
        }
    }

	auto childrenBegin = input.cbegin();
	auto childrenEnd = input.cend();
    {
    Benchmark banchmark; //졸라 느려 3000ms
        while (std::regex_search(childrenBegin, childrenEnd, match, childrenRegex))
        {
            int parentId = std::stoi(match[1].str());
            std::vector<int> children;
            std::string childrenStr = match[2].str();
            std::regex childRegex("Node_(\\d+),\n");
            auto childBegin = childrenStr.cbegin();
            auto childEnd = childrenStr.cend();
            while (std::regex_search(childBegin, childEnd, match, childRegex))
            {
                int num = std::stoi(match[1].str());
                children.push_back(++num);
                childBegin = match.suffix().first;
            }
            if (nodeTypes[parentId] == NodeType::Root)
            {
                nodeLinks[++parentId] = children;
            }
            else
            {
                nodeLinks[parentId += 2] = children;
            }
            childrenBegin++;
        }
    }
    Benchmark banchmark;
    int inputPin{};
	g_Nodes.clear();

	for (const auto& [id, name] : nodeNames)
	{
		NodeData node;
		node.id = id;
        g_NextId = id;
        g_NextId++;
		node.type = nodeTypes[id];
		node.name = name;
        // 핀 추가
        if (node.type == NodeType::Action)
        {
            node.inputPins.push_back(g_NextId++);
        }
        else if (node.type == NodeType::Root)
        {
            node.outputPins.push_back(g_NextId++);
        }
        else
        {
            node.inputPins.push_back(g_NextId++);
            node.outputPins.push_back(g_NextId++);
        }
		g_Nodes[id] = node;
	}

	for (const auto& [id, children] : nodeLinks)
	{
		for (int childId : children)
		{
			g_Links[id].push_back(childId);
		}
	}

    //node links restore

	inFile.close();

	Debug->Log("Behavior tree successfully loaded from Lua file with comments.");
	_isLoadBt = true;
}
