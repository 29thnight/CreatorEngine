#include "NewBehaviourScript.h"
#include "BehaviorTreeComponent.h"
#include "pch.h"

void NewBehaviourScript::Start()
{
	auto behaviorTree = m_pOwner->GetComponent<BehaviorTreeComponent>();
	behaviorTree->Initialize();
	auto rootNode = behaviorTree->GetRoot();
	json data = {
		{"name", "Action"}
	};
	auto actionNode = behaviorTree->CreateNode("ActionNode", data);
	auto actionNodePtr = std::dynamic_pointer_cast<ActionNode>(actionNode);
	if (!actionNodePtr) {
		std::cout << "Failed to create ActionNode." << std::endl;
		return;
	}
	else {
		actionNodePtr->SetAction([this](float deltatime, BlackBoard& blackBoard) -> NodeStatus {
			std::cout << "ActionNode executed with delta time: " << deltatime << std::endl;
			return NodeStatus::Success; // Always return success for this example
		});
	}
	static_cast<BT::SequenceNode*>(rootNode.get())->AddChild(actionNode);
}

void NewBehaviourScript::Update(float tick)
{
	//m_pOwner;
	
	m_pTransform->AddPosition({ 0.0f, 0.0f, 1.0f * tick });
	auto behaviorTree = m_pOwner->GetComponent<BehaviorTreeComponent>();
	if (behaviorTree) {
		behaviorTree->Update(tick);
	}
	else {
		std::cout << "BehaviorTreeComponent not found!" << std::endl;
	}
}
