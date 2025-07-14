#include "NewBehaviourScript.h"
#include "BehaviorTreeComponent.h"
#include "pch.h"

//nodecreate -> tree->CreateNode(key, data);
//key => "Action", "Selector", "Sequence", "Condition", "Decorator", etc.
//funtion ->
// Action => SetAction(std::function<NodeStatus(float, BlackBoard&)func >
// Condition => SetCondition(std::function<bool(BlackBoard&)func >

void NewBehaviourScript::Start()
{

	//static_cast<BT::SequenceNode*>(rootNode.get())->AddChild(behaviorTree->CreateNode("ActionNode"), {name});
}

void NewBehaviourScript::Update(float tick)
{
	//m_pOwner;
	
	m_pTransform->AddPosition({ 0.0f, 0.0f, 1.0f * tick });
	

}
