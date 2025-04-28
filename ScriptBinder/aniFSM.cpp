#include "aniFSM.h"
#include "aniState.h"

void aniFSM::SetNextState(std::string stateName)
{
	/*auto it = States.find(stateName);
	if (it != States.end())
	{
		NextState = StateVec[States[stateName]].get();
	}*/
	NextState = StateVec[States[stateName]].get();
}

void aniFSM::SetCurState(std::string stateName)
{
	//auto it = States.find(stateName);
	//if (it != States.end())
	//{
	//	//CurState = it->second.get();
	//	CurState = StateVec[States[stateName]].get();
	//}
	CurState = StateVec[States[stateName]].get();
}

std::shared_ptr<AniTransition> aniFSM::CheckTransition()
{
	if (!CurState)
	{

		CurState = StateVec[States["Idle"]].get();
	}
	if (CurState->Transitions.empty()) return nullptr;
	else
	{
		
	}
	for (auto& iter : CurState->Transitions)
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
		NextState = StateVec[States[trans->GetNextState()]].get();
		CurState->Exit();
		NextState->Enter();
		CurState = NextState;
		NextState = nullptr;
	}
	
}
void aniFSM::Update(float tick)
{
	UpdateState();
	States.size();
	//curName = CurState->Name;
	if (CurState == nullptr) return;
		CurState->Update(tick);
}
