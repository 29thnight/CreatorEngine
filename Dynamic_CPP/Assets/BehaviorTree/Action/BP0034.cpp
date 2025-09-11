#include "BP0034.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus BP0034::Tick(float deltatime, BlackBoard& blackBoard)
{
	TBoss1* script = m_owner->GetComponent<TBoss1>();
	//todo : animation motion play
	script->BP0034();
	return NodeStatus::Success;
}
