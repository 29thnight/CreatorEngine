#include "AniTransition.h"
#include "AnimationController.h"
#include "AnimationState.h"



AniTransition::AniTransition(AnimationState* _curState, AnimationState* _nextState)
{
	curState = _curState;
	nextState = _nextState;
}
AniTransition::~AniTransition()
{


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

void AniTransition::SetCurState(std::string curStateName)
{
	curState = m_ownerController->FindState(curStateName);
}

void AniTransition::SetCurState(AnimationState* _curState)
{
	curState = _curState;
}

void AniTransition::SetNextState(std::string nextStateName)
{
	nextState = m_ownerController->FindState(nextStateName);
}

void AniTransition::SetNextState(AnimationState* _nextStat)
{
	nextState = _nextStat;
}

std::string AniTransition::GetCurState()
{
	return curState->m_name;
}
std::string AniTransition::GetNextState()
{
	return nextState->m_name;
}