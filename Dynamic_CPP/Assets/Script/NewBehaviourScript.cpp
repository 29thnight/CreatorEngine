#include "NewBehaviourScript.h"
#include "BehaviorTreeComponent.h"
#include "pch.h"
void NewBehaviourScript::Start()
{
	auto behaviorTree = m_pOwner->GetComponent<BehaviorTreeComponent>();
	behaviorTree->Initialize();
	auto rootNode = behaviorTree->GetRoot();
	static_cast<BT::SequenceNode*>(rootNode.get())->AddChild(behaviorTree->CreateNode("ActionNode"), {name});
}

void NewBehaviourScript::Update(float tick)
{
	//m_pOwner;
	
	m_pTransform->AddPosition({ 0.0f, 0.0f, 1.0f * tick });
	

}
