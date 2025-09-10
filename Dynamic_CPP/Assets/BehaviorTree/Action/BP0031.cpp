#include "BP0031.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0031::Tick(float deltatime, BlackBoard& blackBoard)
{
	TBoss1* script = m_owner->GetComponent<TBoss1>();

	//todo : animation motion play

	script->BP0031();


	return NodeStatus::Success;
}
