#include "BehaviorTreeComponent.h"

void BehaviorTreeComponent::Update(float tick)
{
	_isSuccess = _behaviorTree->runTree(tick);
}

void BehaviorTreeComponent::EditorContext()
{
}

void BehaviorTreeComponent::Serialize(_inout json& out)
{
	
}

void BehaviorTreeComponent::DeSerialize(_in json& in)
{
}

void BehaviorTreeComponent::FixedUpdate(float fixedTick)
{
}

void BehaviorTreeComponent::LateUpdate(float tick)
{
}

void BehaviorTreeComponent::Initialize()
{
}
