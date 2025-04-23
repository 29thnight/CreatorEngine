#include "aniFSM.h"
#include "aniState.h"
void aniFSM::SetNextState(std::string stateName)
{
	auto it = States.find(stateName);
	if (it != States.end())
	{
		NextState = it->second.get();
	}
}

void aniFSM::Update(float tick)
{
	if (CurState != NextState)
	{
		if (CurState != nullptr)
		{
			CurState->Exit();
		}
		CurState = NextState;
		CurState->Enter();
	}
	else
	{
		CurState->Update(DeltaTime);
	}
}
