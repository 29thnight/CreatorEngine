#pragma once
#include "Core.Minimal.h"
#include "BlackBoard.h"
#include <memory>

namespace BT
{
	enum class NodeStatus { Success, Failure, Running };
	
	class BTNode
	{
	public:
		using NodePtr = std::shared_ptr<BTNode>;
		BTNode(const std::string& name) : m_name(name) {}
		virtual ~BTNode() = default;

		virtual NodeStatus Tick(float deltatime,BlackBoard& blackBoard) = 0;

		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& name) { m_name = name; }

	protected:
		std::string m_name;
	};

	//class RootNode : public BTNode
	//{
	//public:
	//	RootNode() : BTNode("Root") {}
	//	NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
	//	{
	//		if (m_child)
	//		{
	//			return m_child->Tick(deltatime, blackBoard);
	//		}
	//		return NodeStatus::Failure; // No child to tick
	//	}
	//	void SetChild(NodePtr child) { m_child = child; }
	//	NodePtr GetChild() const { return m_child; }
	//private:
	//	NodePtr m_child; // Single child for the root node
	//};


	class CompositeNode : public BTNode
	{
	public:
		CompositeNode(const std::string& name) : BTNode(name) {}
		void AddChild(NodePtr child) { m_children.push_back(child); }

		std::vector<NodePtr>& GetChildren() { return m_children; } 
	protected:
		std::vector<NodePtr> m_children;
	};

	class SequenceNode : public CompositeNode
	{
	public:
		SequenceNode(const std::string& name) : CompositeNode(name), m_currentIndex(0) {}
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
	private:
		size_t m_currentIndex = 0;
	};


	class SelectorNode : public CompositeNode
	{
	public:
		SelectorNode(const std::string& name) : CompositeNode(name), m_currentIndex(0) {}
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

	private:
		size_t m_currentIndex = 0;
	};

	class DecoratorNode : public BTNode
	{
	public:
		DecoratorNode(const std::string& name, NodePtr child)
			: BTNode(name), m_child(child) {}

		NodePtr GetChild() const { return m_child; }
		void SetChild(NodePtr child) { m_child = child; }
	protected:
		NodePtr m_child;
	};


	class InverterNode : public DecoratorNode
	{
	public:
		InverterNode(const std::string& name, NodePtr child)
			: DecoratorNode(name, child) {
		}
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			auto status = m_child->Tick(deltatime, blackBoard);
			if (status == NodeStatus::Success)
				return NodeStatus::Failure;
			else if (status == NodeStatus::Failure)
				return NodeStatus::Success;
			return status; // Running state remains unchanged
		}
	};

	class ConditionNode : public BTNode
	{
	public:
		ConditionNode(const std::string& name, ConditionFunc condition)
			: BTNode(name), cond(condition) {}
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override {
			return cond(blackBoard) ? NodeStatus::Success : NodeStatus::Failure;
		}
	private:
		ConditionFunc cond;//==>
	};

	class ConditionScriptNode : public BTNode
	{
	public:
		ConditionScriptNode(const std::string& name, const std::string& typeName, const std::string& methodName, void* scriptPtr)
			: BTNode(name), scriptInstance(scriptPtr), m_typeName(typeName), m_name(methodName) {
		}
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			auto type = Meta::Find(m_typeName);
			if (!scriptInstance || !type) {
				Debug->LogError("Type not found: " + m_typeName);
				return NodeStatus::Failure;
			}
			if (Meta::InvokeMethodByMetaName(scriptInstance, *type, m_name, { deltatime, blackBoard }))
			{
				return NodeStatus::Success;
			}
			return NodeStatus::Failure;
		}
	private:
		void* scriptInstance{ nullptr }; // 스크립트 인스턴스 포인터
		std::string m_typeName; // 스크립트 컴포넌트의 타입이름
		std::string m_name; // 메서드 이름
	};


	enum class ParallelPolicy
	{
		RequiredAll, // All children must succeed
		RequiredOne, // At least one child must succeed
	};

	class ParallelNode : public CompositeNode
	{
		ParallelNode(const std::string& name, ParallelPolicy policy)
			: CompositeNode(name), m_policy(policy) {
		}
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			bool anyRunning = false;

			for (auto& child : m_children)
			{
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

	private:
		ParallelPolicy m_policy;
	};

	class ActionNode : public BTNode
	{
	public:
		using ActionFunc = std::function<NodeStatus(float, BlackBoard&)>;
		ActionNode(const std::string& name, ActionFunc action)
			: BTNode(name), m_action(action) {
		}
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			m_action(deltatime, blackBoard);
		}
	private:
		ActionFunc m_action;//==>

		
	};

	class ActionScriptNode : public BTNode
	{
	public:
		ActionScriptNode(const std::string& name, const std::string& typeName, const std::string& methodName,void* scriptPtr)
			: BTNode(name), scriptInstance(scriptPtr), m_typeName(typeName), m_name(methodName) {			
		}
		NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override
		{
			auto type = Meta::Find(m_typeName);
			if (!scriptInstance||!type) {
				Debug->LogError("Type not found: " + m_typeName);
				return NodeStatus::Failure;
			}
			if (Meta::InvokeMethodByMetaName(scriptInstance, *type, m_name, { deltatime, blackBoard }))
			{
				return NodeStatus::Success;
			}
			return NodeStatus::Failure;
		}
	private:
		void* scriptInstance{ nullptr }; // 스크립트 인스턴스 포인터
		std::string m_typeName; // 스크립트 컴포넌트의 타입이름
		std::string m_name; // 메서드 이름
	};

	inline void DFS(const BTNode::NodePtr& node, std::function<void(const BTNode::NodePtr)> visit)
	{
		if (!node) return;
		visit(node);
		auto composite = std::dynamic_pointer_cast<CompositeNode>(node);
		if (composite)
		{
			for (const auto& child : composite->GetChildren())
			{
				DFS(child, visit);
			}
		}
		else if (auto decorator = std::dynamic_pointer_cast<DecoratorNode>(node))
		{
			auto child = decorator->GetChild();
			DFS(child, visit);
		}
	}

	
}