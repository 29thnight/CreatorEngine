#include "AniTransition.h"
#include "AnimationController.h"
#include "AnimationState.h"
#include "TransCondition.h"


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
	if (hasExitTime) //최소 탈출시간 있을때
	{
		if (conditions.empty()) //탈출시간은 있는대 조건없으면 탈출시간되면 탈출
		{
			if (GetExitTime() <= m_ownerController->curAnimationProgress)
				return true;
		}
		else  //탈출시간 + 조건있으면 둘다만족해야 탈출
		{
			if (GetExitTime() <= m_ownerController->curAnimationProgress)
			{
				for (auto& condition : conditions)
				{
					if (true == condition.CheckTrans())
						return true;
				}
			}
		}
	}
	else   //없을떄
	{
		if (conditions.empty()) //탈출시간은 없고 조건없으면 애니메이션 끝나면탈출 loop면 탈출불가
		{
			if (m_ownerController->endAnimation)
				return true;
		}
		else  //탈출 시간없고 조건있으면 조건만족시 탈출
		{
			for (auto& condition : conditions)
			{
				if (true == condition.CheckTrans())
					return true;
			}
		}
	}
	return false;
}

std::vector<TransCondition> AniTransition::GetConditions()
{
	return conditions;
}

void AniTransition::DeleteCondition(int _index)
{
	if (_index >= 0 && _index < static_cast<int>(conditions.size())) {
		conditions.erase(conditions.begin() + _index);
	}
}

void AniTransition::SetCurState(std::string _curStateName)
{
	curState = m_ownerController->FindState(_curStateName);
	curStateName = _curStateName;
}

void AniTransition::SetCurState(AnimationState* _curState)
{
	curState = _curState;
	curStateName = curState->m_name;
}

void AniTransition::SetNextState(std::string _nextStateName)
{
	nextState = m_ownerController->FindState(_nextStateName);
	nextStateName = _nextStateName;
}

void AniTransition::SetNextState(AnimationState* _nextState)
{
	nextState = _nextState;
	nextStateName = nextState->m_name;
}


std::string AniTransition::GetCurState()
{
	if (curState == nullptr)
		return "None";
	return curState->m_name;
}
std::string AniTransition::GetNextState()
{
	return nextState->m_name;
}