#include "aniFSM.h"
#include "aniState.h"
#include "AniBehaviour.h"
void aniFSM::SetNextState(std::string stateName)
{
	
	NextState = StateVec[States[stateName]].get();
}

bool aniFSM::BlendingAnimation(float tick)
{
	blendingTime += tick;
	if (blendingTime >= curTrans->GetBlendTime())
	{
		blendingTime = 0;
		return true;
	}



	return false;
}

void aniFSM::SetCurState(std::string stateName)
{
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
	//���̰������� �ִϸ��̼� �������� //�������� ������ȭ������� �߰��ʿ�*****
	if (nullptr != trans)
	{
		NextState = StateVec[States[trans->GetNextState()]].get();
		CurState->behaviour->Exit();
		NextState->behaviour->Enter();
		CurState = NextState;
		NextState = nullptr;
		curTrans = trans.get();
		needBlend = true;
	}

	
}
void aniFSM::Update(float tick)
{
	UpdateState();
	if (needBlend)
	{
		if (BlendingAnimation(tick) == true)
			needBlend = false;
	}

	if (CurState == nullptr) return;
	CurState->behaviour->Update(tick);

	
}
