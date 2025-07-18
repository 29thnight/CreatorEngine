#pragma once
#include "BTEnum.h"
#include "BTBuildGraph.h"
#include "BlackBoard.h"
#include <memory>

namespace BT
{
	class BTNode
	{
	public:
		using NodePtr = std::shared_ptr<BTNode>;
		BTNode() = default;
		BTNode(const std::string& name) : m_name(name) {}
		virtual ~BTNode() = default;

		virtual NodeStatus Tick(float deltatime,BlackBoard& blackBoard) = 0;

		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& name) { m_name = name; }

		virtual bool IsOutpinConnected() const { return false; }

		virtual BehaviorNodeType GetNodeType() const = 0;
		void SetOwner(GameObject* owner) { m_owner = owner; }
		GameObject* GetOwner() const { return m_owner; }
	protected:
		std::string m_name;
		GameObject* m_owner{ nullptr }; // Node가 소속된 GameObject
		bool m_isOutpinConnected{ false };
	};

	class CompositeNode : public BTNode
	{
	public:
		CompositeNode() = default;
		CompositeNode(const std::string& name) : BTNode(name) {}
		~CompositeNode() override = default;

		void AddChild(NodePtr child) { m_children.push_back(child); }

		std::vector<NodePtr>& GetChildren() { return m_children; } 
		bool IsOutpinConnected() const override final
		{
			return !m_children.empty();
		}

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Composite; }

	protected:
		std::vector<NodePtr> m_decorator;
		std::vector<NodePtr> m_children;
	};

	class SequenceNode : public CompositeNode
	{
	public:
		SequenceNode() = default;
		SequenceNode(const std::string& name) : CompositeNode(name), m_currentIndex(0) {}
		~SequenceNode() override = default;

		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			while (m_currentIndex < m_children.size())
			{
				auto status = m_children[m_currentIndex]->Tick(deltatime, blackBoard);
				if (status == NodeStatus::Running)
				{
					return NodeStatus::Running;
				}

				if (status == NodeStatus::Failure) 
				{
					m_currentIndex = 0; // Reset index for next tick
					return NodeStatus::Failure;
				}
				++m_currentIndex;
			}
			m_currentIndex = 0; // Reset index for next tick
			return NodeStatus::Success; // All children failed
		}

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Sequence; }

	private:
		size_t m_currentIndex = 0;
	};

	class SelectorNode : public CompositeNode
	{
	public:
		SelectorNode() = default;
		SelectorNode(const std::string& name) : CompositeNode(name), m_currentIndex(0) {}
		~SelectorNode() override = default;
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			while (m_currentIndex < m_children.size())
			{
				auto status = m_children[m_currentIndex]->Tick(deltatime, blackBoard);
				if (status == NodeStatus::Running)
				{
					return NodeStatus::Running;
				}
				if (status == NodeStatus::Success)
				{
					m_currentIndex = 0; // Reset index for next tick
					return NodeStatus::Success; // One child succeeded
				}
				++m_currentIndex;
			}
			m_currentIndex = 0; // Reset index for next tick
			return NodeStatus::Failure; // All children failed
		}

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Selector; }

	private:
		size_t m_currentIndex = 0;
	};

	class DecoratorNode : public BTNode
	{
	public:
		DecoratorNode() = default;
		DecoratorNode(const std::string& name, NodePtr child)
			: BTNode(name), m_child(child) {}
		~DecoratorNode() override = default;

		NodePtr GetChild() const { return m_child; }
		void SetChild(NodePtr child) { m_child = child; }
		bool IsOutpinConnected() const override final
		{
			return m_child != nullptr; // Decorator nodes always have a single child
		}

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Decorator; }

	protected:
		NodePtr m_child;
	};

	class InverterNode : public DecoratorNode
	{
	public:
		InverterNode() = default;
		InverterNode(const std::string& name, NodePtr child)
			: DecoratorNode(name, child) {
		}
		~InverterNode() override = default;

		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			if (!m_child) return NodeStatus::Failure;

			auto status = m_child->Tick(deltatime, blackBoard);
			if (status == NodeStatus::Success)
				return NodeStatus::Failure;
			else if (status == NodeStatus::Failure)
				return NodeStatus::Success;
			return status; // Running state remains unchanged
		}

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Inverter; }
	};

	class ConditionDecoratorNode : public DecoratorNode
	{
	public:
		using ConditionFunc = std::function<bool(float, const BlackBoard&)>;
		ConditionDecoratorNode() = default;
		ConditionDecoratorNode(const std::string& name, NodePtr child, ConditionFunc condition)
			: DecoratorNode(name, child){

		}
		~ConditionDecoratorNode() override = default;
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			if (!m_child) return NodeStatus::Failure;
			if (ConditionCheck(deltatime, blackBoard))
			{
				return m_child->Tick(deltatime, blackBoard);
			}
			return NodeStatus::Failure; // Condition not met
		}

		virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) abstract;

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::ConditionDecorator; }

		HashedGuid m_typeID{};
		HashedGuid m_scriptTypeID{};
	};

	class ConditionNode : public BTNode
	{
	public:
		ConditionNode() = default;
		ConditionNode(const std::string& name, ConditionFunc condition)
			: BTNode(name) {
		}
		~ConditionNode() override = default;

		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override 
		{
			return ConditionCheck(deltatime, blackBoard) ? NodeStatus::Success : NodeStatus::Failure;
		}

		virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) abstract;

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Condition; }
	
		HashedGuid m_typeID{};
		HashedGuid m_scriptTypeID{};
	};

	class ParallelNode : public CompositeNode
	{
	public:
		ParallelNode() = default;
		ParallelNode(const std::string& name, ParallelPolicy policy)
			: CompositeNode(name), m_policy(policy) {
		}
		~ParallelNode() override = default;

		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			bool anyRunning = false;

			for (auto& child : m_children)
			{
				//TODO : async tick
				auto status = child->Tick(deltatime, blackBoard);
				if (status == NodeStatus::Running)
				{
					anyRunning = true;
				}
				
				if (status == NodeStatus::Failure && m_policy == ParallelPolicy::RequiredAll)
				{
					return NodeStatus::Failure; // If any child fails in RequiredAll policy
				}
				if (status == NodeStatus::Success && m_policy == ParallelPolicy::RequiredOne)
				{
					return NodeStatus::Success; // If any child succeeds in RequiredOne policy
				}
			}

			if (m_policy == ParallelPolicy::RequiredAll)
			{
				return anyRunning ? NodeStatus::Running : NodeStatus::Success; // If all children succeeded in RequiredAll policy
			}else {
				return anyRunning ? NodeStatus::Running : NodeStatus::Failure; // If no child succeeded in RequiredOne policy
			}
		}

		BehaviorNodeType GetNodeType() const override { return BehaviorNodeType::Composite; }

	private:
		ParallelPolicy m_policy;
	};

	class ActionNode : public BTNode
	{
	public:
		using ActionFunc = std::function<NodeStatus(float, BlackBoard&)>;
		ActionNode() = default;
		ActionNode(const std::string& name)
			: BTNode(name) {
		}
		~ActionNode() override = default;

		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override abstract;
		BehaviorNodeType GetNodeType() const override final { return BehaviorNodeType::Action; }
	
		HashedGuid m_typeID{};
		HashedGuid m_scriptTypeID{};
	};
}