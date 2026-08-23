#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class NodeStatus { Success, Failure, Aborted, Running };

enum class BehaviorNodeType { Composite, Decorator, Sequence, Selector, WeightedSelector, Inverter, ConditionDecorator , Condition, Parallel, Action };

enum class ParallelPolicy
{
	RequiredAll, // All children must succeed
	RequiredOne, // At least one child must succeed
};

namespace BT
{
	static inline bool IsCompositeNode(BehaviorNodeType type)
	{
		return type == BehaviorNodeType::Composite || 
				type == BehaviorNodeType::Selector || 
				type == BehaviorNodeType::Sequence || 
				type == BehaviorNodeType::WeightedSelector ||
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

	static inline BehaviorNodeType StringToNodeType(const std::string& str)
	{
		if (str == "Sequence")			 return BehaviorNodeType::Sequence;
		if (str == "Selector")			 return BehaviorNodeType::Selector;
		if (str == "WeightedSelector") return BehaviorNodeType::WeightedSelector;
		if (str == "Parallel")			 return BehaviorNodeType::Parallel;
		if (str == "Inverter")			 return BehaviorNodeType::Inverter;
		if (str == "ConditionDecorator") return BehaviorNodeType::ConditionDecorator;
		if (str == "Action")			 return BehaviorNodeType::Action;
		if (str == "Condition")			 return BehaviorNodeType::Condition;
		return BehaviorNodeType::Composite; // Should not happen with GetRegisteredKey
	}
}
