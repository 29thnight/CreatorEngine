#pragma once
#include "imgui-node-editor/imgui_node_editor.h"
#include <atomic>
#include <thread>
#include <iostream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ed = ax::NodeEditor;
// 노드 유형
enum class NodeType { Root, Condition, Action, Selector, Sequence, Parallel };
class World;
// 노드 데이터 구조
struct NodeData 
{
	int id;
	NodeType type;
	std::string name;
	std::vector<int> inputPins;
	std::vector<int> outputPins;
};

class BT_Editor
{
public:
	BT_Editor() = default;
	~BT_Editor() = default;

	void SetEditorMenu(World* pWorld)
	{
		//_world = pWorld;
	}

	void Initialize()
	{
		//_context = ed::CreateEditor();
		//AddNode(NodeType::Root, "Root_Node");
	}

	void Finalize()
	{
		//ed::DestroyEditor(_context);
	}

	void AddNode(NodeType type, const std::string& name);
	void BuildTreeToLua(const std::string& outPath);
	void LoadTreeFromLua(const std::string& inPath);
	void NodeEditor();
	void ShowMainUI();
	void HandlePinConnection();
	void ShowAddNodePopup();
	void ShowBuildSettingLuaPopup();

private:
	World* _world;
	ed::EditorContext* _context;
	static int g_NextId;
	std::unordered_map<int, NodeData> g_Nodes;
	std::unordered_map<int, std::vector<int>> g_Links; // 링크 매핑: outputPin -> inputPin
	// 핀 연결 상태
	int g_SelectedOutputPin = -1; // 선택된 출력 핀
	int g_SelectedInputPin = -1;  // 선택된 입력 핀
	std::atomic_bool _isLoadBt{ true };
	std::thread _loadBtThread;
};