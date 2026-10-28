#pragma once
#include "../../Engine/Containers/ContainerAPI.h"
#include "ImGui.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "imgui-node-editor/imgui_node_editor.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
namespace ed = ax::NodeEditor;

enum class SelectedType
{
	None,
	Node,
	Link,
};

struct Node
{
	int id;
	ce::utf8_string name;
};

struct NodeLink
{
	Node* fromNode;
	Node* toNode;
	bool haveReverse = false;
};

class NodeEditor
{
public:
	NodeEditor() = default;
	~NodeEditor();
	NodeEditor(const NodeEditor&) = delete;
	NodeEditor& operator=(const NodeEditor&) = delete;
	NodeEditor(NodeEditor&&) = delete;
	NodeEditor& operator=(NodeEditor&&) = delete;
	static std::optional<std::filesystem::path> SettingsPath(
		const std::filesystem::path& directory, ce::utf8_string_view filename);
	bool MakeEdit(const std::filesystem::path& filePath);
	void EndEdit();
	void MakeNode(ce::utf8_string_view nodeName);
	void MakeLink(ce::utf8_string_view fromNodeName, ce::utf8_string_view toNodeName);
	void DrawNode(int* selectedNodeIndex = nullptr);
	void DrawLink(int* selectedLinkIndex = nullptr);

	void Update();


	void MakeNewLink(int* returnIndex);

	bool ReNameJson(const std::filesystem::path& filePath);
	bool IsMouseNearLink(const ImVec2& p1, const ImVec2& cp1, const ImVec2& cp2, const ImVec2& p2, float threshold = 5.0f);
	ImVec2 ImBezierCubicCalcDerivative(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t);
	int startNodeId = 1000;
	Node* FindNode(ce::utf8_string_view nodeName);
	SelectedType m_selectedType = SelectedType::None;
	int seletedCurNodeIndex = -1;
	int seletedCurLinkIndex = -1;
	ax::NodeEditor::EditorContext* m_nodeContext = nullptr;
	std::filesystem::path m_filePath;
	ce::dynamic_array<Node*> Nodes;
	ce::dynamic_array<NodeLink*> Links;
	
	int* m_retrunIndex = nullptr; 
	bool needMakeLink = false;

private:
	static std::size_t LoadSettings(char* data, void* userPointer);
	static bool SaveSettings(const char* data, std::size_t size,
		ax::NodeEditor::SaveReasonFlags reason, void* userPointer);
	void ReleaseContext();
	// The library first asks for a length, then provides exactly that much space.
	// Keep one owned snapshot so an intervening file change cannot overrun it.
	ce::utf8_string m_loadedSettings;
};

