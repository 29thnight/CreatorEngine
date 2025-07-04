#pragma once
#include <string>
#include "imgui-node-editor/imgui_node_editor.h"
#include "EffectEventUnit.generated.h"

namespace MetaNode = ax::NodeEditor;

struct ParticleLink {
	MetaNode::LinkId id;
	MetaNode::PinId from;
	MetaNode::PinId to;
};

class EffectEventUnit
{
public:
   ReflectEffectEventUnit
	[[Serializable]]
	EffectEventUnit() = default;
	~EffectEventUnit() = default;

	[[Property]]
	std::string eventName;  // 이벤트 이름
};

class IEffectImGuiRender
{
public:
	virtual void DrawNodeGUI() abstract;
};
