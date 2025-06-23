#pragma once
#include "Blackboard.h"

namespace BT 
{
	enum class NodeStatus
	{
		Success,
		Failure,
		Running
	};


	class BTNode
	{
	public:
		using Ptr = std::shared_ptr<BTNode>;
		BTNode(const std::string& name) : m_name(name){}
		virtual ~BTNode() = default;

		virtual NodeStatus Tick(Blackboard& blackboard) = 0;
		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& name) { m_name = name; }
	protected :
		std::string m_name;
	};


	// Composite Node --> �ռ� ��� -> �ڽ� ��ü ���� �Ͽ� �ڽ��� ���¸� �����ϴ� ���
	class CompositeNode : public BTNode
	{
	public:
		CompositeNode(const std::string& name) : BTNode(name) {}
		virtual ~CompositeNode() = default;

		void AddChild(Ptr child) { m_children.push_back(child); }
	protected:
		std::vector<Ptr> m_children; // �ڽ� ����
	};

	// Sequence Node --> ������ ��� -> �ڽ� ��带 ���������� �����ϸ�, �ϳ��� �����ϸ� ���з� �����ϴ� ���
	class SequenceNode : public CompositeNode
	{
	public:
		SequenceNode(const std::string& name) : CompositeNode(name), m_currentIndex(0) {}
		NodeStatus Tick(Blackboard& blackboard) override
		{
			while (m_currentIndex < m_children.size())
			{
				NodeStatus status = m_children[m_currentIndex]->Tick(blackboard);
				if (status == NodeStatus::Running)
				{
					return NodeStatus::Running; // ���� ��尡 ���� ���̸� Running ��ȯ
				}
				else if (status == NodeStatus::Failure)
				{
					m_currentIndex = 0; // ���� �� �ε��� �ʱ�ȭ
					return NodeStatus::Failure; // ���� ��ȯ
				}
				m_currentIndex++; // ���� �ڽ� ���� �̵�
			}
			m_currentIndex = 0; // ��� �ڽ� ��尡 ���������Ƿ� �ε��� �ʱ�ȭ
			return NodeStatus::Success; // ��� �ڽ� ��尡 ���������Ƿ� ���� ��ȯ
		}

	private:
		size_t m_currentIndex = 0; // ���� ���� ���� �ڽ� ��� �ε���
	};

	// Selector Node --> ���� ��� -> �ڽ� ��带 ���������� �����ϸ�, �ϳ��� �����ϸ� �������� �����ϴ� ���
	class SelectorNode : public CompositeNode
	{
	public:
		SelectorNode(const std::string& name) : CompositeNode(name), m_currentIndex(0) {}
		NodeStatus Tick(Blackboard& blackboard) override 
		{
			while (m_currentIndex < m_children.size())
			{
				NodeStatus status = m_children[m_currentIndex]->Tick(blackboard);
				if (status == NodeStatus::Running) 
				{
					return NodeStatus::Running; // ���� ��尡 ���� ���̸� Running ��ȯ
				}
				else if (status == NodeStatus::Success)
				{
					m_currentIndex = 0; // ���� �� �ε��� �ʱ�ȭ
					return NodeStatus::Success; // ���� ��ȯ
				}
				m_currentIndex++; // ���� �ڽ� ���� �̵�
			}
			m_currentIndex = 0; // ��� �ڽ� ��尡 ���������Ƿ� �ε��� �ʱ�ȭ
			return NodeStatus::Failure; // ��� �ڽ� ��尡 ���������Ƿ� ���� ��ȯ

		}
	private:
		size_t m_currentIndex = 0; // ���� ���� ���� �ڽ� ��� �ε���
	};
	
	
	// Parallel Node --> ���� ��� -> �ڽ� ��带 ���ķ� �����ϸ�, ��� �ڽ� ��尡 �����ؾ� �������� �����ϴ� ���
	enum class ParallePolicy { RequireAll, RequireOne};
	class ParallelNode : public CompositeNode
	{
	public:
		ParallelNode(const std::string& name, ParallePolicy policy = ParallePolicy::RequireAll)
			: CompositeNode(name), m_policy(policy) {}
		NodeStatus Tick(Blackboard& blackboard) override
		{
			bool anyRunning = false;
			for (auto& child : m_children)
			{
				NodeStatus status = child->Tick(blackboard);
				if (status == NodeStatus::Running)
				{
					anyRunning = true; // �ϳ��� ���� ���� ��尡 ������ Running ����
				}
				
				if (m_policy == ParallePolicy::RequireAll && status == NodeStatus::Failure)
				{
					return NodeStatus::Failure; // �ϳ��� �����ϸ� ��ü ����
				}

				if (m_policy == ParallePolicy::RequireOne && status == NodeStatus::Success)
				{
					return NodeStatus::Success; // �ϳ��� �����ϸ� ��ü ����
				}
			}

			if (m_policy == ParallePolicy::RequireAll)
			{
				return anyRunning ? NodeStatus::Running : NodeStatus::Success; // ��� �ڽ� ��尡 �����ؾ� �������� ����
			}else {
				return anyRunning ? NodeStatus::Running : NodeStatus::Failure; // �ϳ��� �����ϸ� ����, ���д� �ϳ��� �����ϸ� ���з� ����
			}
		}
	private:
		ParallePolicy m_policy;
	};
	
	// Decorator Node --> ���ڷ����� ��� -> �ڽ� ��带 ���ΰ�, �ڽ� ����� ���¸� �����ϰų� �߰����� ������ �����ϴ� ���
	
	class DecoratorNode : public BTNode
	{
	public:
		DecoratorNode(const std::string& name, BTNode::Ptr child)
			: BTNode(name), m_child(child) {
		}
	protected:
		BTNode::Ptr m_child; // ���δ� �ڽ� ���
	};

	//�ϴ� inverter ��常 ����
	class InverterNode : public DecoratorNode
	{
	public:
		InverterNode(const std::string& name, BTNode::Ptr child)
			: DecoratorNode(name, child) {
		}
		NodeStatus Tick(Blackboard& blackboard) override {
			NodeStatus status = m_child->Tick(blackboard);
			if (status == NodeStatus::Success) {
				return NodeStatus::Failure;
			}
			else if (status == NodeStatus::Failure) {
				return NodeStatus::Success;
			}
			return status; // Running ���´� �״�� ��ȯ
		}
	};
	
	// Condition Node --> ���� ��� -> Ư�� ������ �˻��ϰ�, ������ ���̸� ����, �����̸� ���з� �����ϴ� ���
	class ConditionNode : public BTNode
	{
	public:
		using ConditionFunc = std::function<bool(Blackboard&)>;
		ConditionNode(const std::string& name, ConditionFunc condition)
			: BTNode(name), m_condition(condition) {
		}
		NodeStatus Tick(Blackboard& blackboard) override {
			return m_condition(blackboard) ? NodeStatus::Success : NodeStatus::Failure; // ������ ���̸� ����, �����̸� ����
		}
	private:
		ConditionFunc m_condition; // ���� �Լ�
	};

	// Action Node --> �׼� ��� -> ���� �ൿ�� �����ϴ� ���, ���� ��� �̵�, ���� ���� �ൿ�� �����ϴ� ���	
	class ActionNode : public BTNode
	{
	public:
		using ActionFunc = std::function<NodeStatus(Blackboard&)>;
		ActionNode(const std::string& name, ActionFunc action)
			: BTNode(name), m_action(action) {
		}
		NodeStatus Tick(Blackboard& blackboard) override {
			return m_action(blackboard); // �ൿ ����
		}
	private:
		ActionFunc m_action; // �ൿ �Լ�
	};
	
}// namespace BT