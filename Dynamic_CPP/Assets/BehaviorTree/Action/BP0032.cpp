#include "BP0032.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0032::Tick(float deltatime, BlackBoard& blackBoard)
{
	TBoss1* script = m_owner->GetComponent<TBoss1>();

	//todo : animation motion play

	script->BP0032();

	return NodeStatus::Success;
}
