#include "AniTransition.h"
#include "AnimationController.h"
#include "AnimationState.h"


//AniTransition::AniTransition(std::string curStatename, std::string nextStatename, AnimationController* owner)
//{
//	m_ownerController = owner;
//	SetCurState(curStatename);
//	SetNextState(nextStatename);
//}
AniTransition::AniTransition(AnimationState* _curState, AnimationState* _nextState, AnimationController* owner)
{
	m_ownerController = owner;
	curState = _curState;
	nextState = _nextState;
}
AniTransition::~AniTransition()
{


}




void AniTransition::SetCurState(std::string curStatename)
{
	 curState = m_ownerController->FindState(curStatename); 
}

void AniTransition::SetNextState(std::string nextStatename)
{
	 nextState = m_ownerController->FindState(nextStatename); 
}

bool AniTransition::CheckTransiton()
{
	/*for (auto& condition : conditions)
	{
		bool result = std::visit([](auto& cond) {
			return cond.CheckTrans();
		}, condition);

		if (!result)
			return false; 
	}*/
	for (auto& condition : conditions)
	{
		if (true == condition.CheckTrans())
			return true;
	}

	return false;
}

std::vector<TransCondition> AniTransition::GetConditions()
{
	return conditions;
}

std::string AniTransition::GetCurState()
{
	return curState->m_name;
}
std::string AniTransition::GetNextState()
{
	return nextState->m_name;
}