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

void aniFSM::SetCurState(std::string stateName)
{
	auto it = States.find(stateName);
	if (it != States.end())
	{
		CurState = it->second.get();
	}
}

std::shared_ptr<AniTransition> aniFSM::CheckTransition()
{
	if (Transitions.empty()) return nullptr;
	for (auto& iter : Transitions[CurState->Name])
	{
		if (true == iter->CheckTransiton())
		{
			return iter;
		}
	}
	return nullptr;
}

void aniFSM::UpdateState()
{
	auto trans = CheckTransition();

	if (nullptr != trans)
	{
		NextState = States[trans->GetNextState()].get();
		CurState->Exit();
		NextState->Enter();
		CurState = NextState;
		NextState = nullptr;
	}
	
}
void aniFSM::Update(float tick)
{
	UpdateState();

	if (CurState == nullptr) return;
		CurState->Update(tick);
}
