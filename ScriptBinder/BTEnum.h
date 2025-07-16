#pragma once
#include "Core.Minimal.h"
#include "imgui-node-editor\imgui_node_editor.h"

enum class NodeStatus { Success, Failure, Running };
AUTO_REGISTER_ENUM(NodeStatus);

enum class BehaviorNodeType { Composite, Decorator, Sequence, Selector, Inverter, ConditionDecorator , Condition, Parallel, Action };
AUTO_REGISTER_ENUM(BehaviorNodeType);

enum class ParallelPolicy
{
	RequiredAll, // All children must succeed
	RequiredOne, // At least one child must succeed
};
AUTO_REGISTER_ENUM(ParallelPolicy);

namespace BT
{
	static inline bool IsCompositeNode(BehaviorNodeType type)
	{
		return type == BehaviorNodeType::Composite || 
				type == BehaviorNodeType::Selector || 
				type == BehaviorNodeType::Sequence || 
				type == BehaviorNodeType::Parallel;
	}

	static inline bool IsDecoratorNode(BehaviorNodeType type)
	{
		return type == BehaviorNodeType::Decorator			|| 
			type == BehaviorNodeType::ConditionDecorator	|| 
			type == BehaviorNodeType::Inverter;
	}

	static inline bool IsConditionNode(BehaviorNodeType type)
	{
		return type == BehaviorNodeType::Condition;
	}

	static inline bool IsActionNode(BehaviorNodeType type)
	{
		return type == BehaviorNodeType::Action;
	}

	inline ImVec2 ToImVec2(const Mathf::Vector2& vec)
	{
		return ImVec2(vec.x, vec.y);
	}

	inline Mathf::Vector2 ToMathfVec2(const ImVec2& vec)
	{
		return Mathf::Vector2(vec.x, vec.y);
	}

	static inline BehaviorNodeType StringToNodeType(const std::string& str)
	{
		if (str == "Sequence")			 return BehaviorNodeType::Sequence;
		if (str == "Selector")			 return BehaviorNodeType::Selector;
		if (str == "Parallel")			 return BehaviorNodeType::Parallel;
		if (str == "Inverter")			 return BehaviorNodeType::Inverter;
		if (str == "ConditionDecorator") return BehaviorNodeType::ConditionDecorator;
		if (str == "Action")			 return BehaviorNodeType::Action;
		if (str == "Condition")			 return BehaviorNodeType::Condition;
		return BehaviorNodeType::Composite; // Should not happen with GetRegisteredKey
	}

	namespace ed = ax::NodeEditor;
	inline ed::NodeId GetTreeNodeIdFromPin(ed::PinId pin)
	{
		return ed::NodeId(reinterpret_cast<void*>((uintptr_t)pin.Get() >> 1));
	}
}
