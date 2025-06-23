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


	// Composite Node --> 합성 노드 -> 자식 개체 실행 하여 자신의 상태를 결정하는 노드
	class CompositeNode : public BTNode
	{
	public:
		CompositeNode(const std::string& name) : BTNode(name) {}
		virtual ~CompositeNode() = default;

		void AddChild(Ptr child) { m_children.push_back(child); }
	protected:
		std::vector<Ptr> m_children; // 자식 노드들
	};

	// Sequence Node --> 시퀀스 노드 -> 자식 노드를 순차적으로 실행하며, 하나라도 실패하면 실패로 간주하는 노드
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
					return NodeStatus::Running; // 현재 노드가 실행 중이면 Running 반환
				}
				else if (status == NodeStatus::Failure)
				{
					m_currentIndex = 0; // 실패 시 인덱스 초기화
					return NodeStatus::Failure; // 실패 반환
				}
				m_currentIndex++; // 다음 자식 노드로 이동
			}
			m_currentIndex = 0; // 모든 자식 노드가 성공했으므로 인덱스 초기화
			return NodeStatus::Success; // 모든 자식 노드가 성공했으므로 성공 반환
		}

	private:
		size_t m_currentIndex = 0; // 현재 실행 중인 자식 노드 인덱스
	};

	// Selector Node --> 선택 노드 -> 자식 노드를 순차적으로 실행하며, 하나라도 성공하면 성공으로 간주하는 노드
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
					return NodeStatus::Running; // 현재 노드가 실행 중이면 Running 반환
				}
				else if (status == NodeStatus::Success)
				{
					m_currentIndex = 0; // 성공 시 인덱스 초기화
					return NodeStatus::Success; // 성공 반환
				}
				m_currentIndex++; // 다음 자식 노드로 이동
			}
			m_currentIndex = 0; // 모든 자식 노드가 실패했으므로 인덱스 초기화
			return NodeStatus::Failure; // 모든 자식 노드가 실패했으므로 실패 반환

		}
	private:
		size_t m_currentIndex = 0; // 현재 실행 중인 자식 노드 인덱스
	};
	
	
	// Parallel Node --> 병렬 노드 -> 자식 노드를 병렬로 실행하며, 모든 자식 노드가 성공해야 성공으로 간주하는 노드
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
					anyRunning = true; // 하나라도 실행 중인 노드가 있으면 Running 상태
				}
				
				if (m_policy == ParallePolicy::RequireAll && status == NodeStatus::Failure)
				{
					return NodeStatus::Failure; // 하나라도 실패하면 전체 실패
				}

				if (m_policy == ParallePolicy::RequireOne && status == NodeStatus::Success)
				{
					return NodeStatus::Success; // 하나라도 성공하면 전체 성공
				}
			}

			if (m_policy == ParallePolicy::RequireAll)
			{
				return anyRunning ? NodeStatus::Running : NodeStatus::Success; // 모든 자식 노드가 성공해야 성공으로 간주
			}else {
				return anyRunning ? NodeStatus::Running : NodeStatus::Failure; // 하나라도 성공하면 성공, 실패는 하나라도 실패하면 실패로 간주
			}
		}
	private:
		ParallePolicy m_policy;
	};
	
	// Decorator Node --> 데코레이터 노드 -> 자식 노드를 감싸고, 자식 노드의 상태를 변경하거나 추가적인 로직을 수행하는 노드
	
	class DecoratorNode : public BTNode
	{
	public:
		DecoratorNode(const std::string& name, BTNode::Ptr child)
			: BTNode(name), m_child(child) {
		}
	protected:
		BTNode::Ptr m_child; // 감싸는 자식 노드
	};

	//일단 inverter 노드만 구현
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
			return status; // Running 상태는 그대로 반환
		}
	};
	
	// Condition Node --> 조건 노드 -> 특정 조건을 검사하고, 조건이 참이면 성공, 거짓이면 실패로 간주하는 노드
	class ConditionNode : public BTNode
	{
	public:
		using ConditionFunc = std::function<bool(Blackboard&)>;
		ConditionNode(const std::string& name, ConditionFunc condition)
			: BTNode(name), m_condition(condition) {
		}
		NodeStatus Tick(Blackboard& blackboard) override {
			return m_condition(blackboard) ? NodeStatus::Success : NodeStatus::Failure; // 조건이 참이면 성공, 거짓이면 실패
		}
	private:
		ConditionFunc m_condition; // 조건 함수
	};

	// Action Node --> 액션 노드 -> 실제 행동을 수행하는 노드, 예를 들어 이동, 공격 등의 행동을 구현하는 노드	
	class ActionNode : public BTNode
	{
	public:
		using ActionFunc = std::function<NodeStatus(Blackboard&)>;
		ActionNode(const std::string& name, ActionFunc action)
			: BTNode(name), m_action(action) {
		}
		NodeStatus Tick(Blackboard& blackboard) override {
			return m_action(blackboard); // 행동 수행
		}
	private:
		ActionFunc m_action; // 행동 함수
	};
	
}// namespace BT