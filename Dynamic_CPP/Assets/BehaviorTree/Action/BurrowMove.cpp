#include "BurrowMove.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BurrowMove::Tick(float deltatime, BlackBoard& blackBoard)
{
	TBoss1* script = m_owner->GetComponent<TBoss1>();	
	
	script->BurrowMove(deltatime);

	if (script->isMoved)
	{
		script->isMoved = false;
		return NodeStatus::Success;
	}
	else
	{
		return NodeStatus::Running;
	}
}
