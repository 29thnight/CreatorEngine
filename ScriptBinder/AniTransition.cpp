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


bool AniTransition::CheckTransiton(bool isBlend)
{
	auto Progress = m_ownerController->curAnimationProgress;
	//���������� progress�� ����� �ʱ�ȭ�ȵǼ� ���̰� �̻��ϰԵ�
	if (isBlend)
	{
		Progress = m_ownerController->nextAnimationProgress;
	}
	if (hasExitTime) //�ּ� Ż��ð� ������
	{
		if (conditions.empty()) //Ż��ð��� �ִ´� ���Ǿ����� Ż��ð��Ǹ� Ż��
		{
			if (GetExitTime() <= Progress)
				return true;
		}
		else  //Ż��ð� + ���������� �Ѵٸ����ؾ� Ż��
		{
			if (GetExitTime() <= Progress)
			{
				for (auto& condition : conditions)
				{
					if (true == condition.CheckTrans())
						return true;
				}
			}
		}
	}
	else   //������
	{
		if (conditions.empty()) //Ż��ð��� ���� ���Ǿ����� �ִϸ��̼� ������Ż�� loop�� Ż��Ұ�
		{
			if (m_ownerController->endAnimation)
				return true;
		}
		else  //Ż�� �ð����� ���������� ���Ǹ����� Ż��
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

nlohmann::json AniTransition::Serialize()
{
	nlohmann::json j;
	j["transName"] = m_name;
	j["curStateName"] = curStateName;
	j["nextStateName"] = nextStateName;
	j["hasExitTime"] = (int)hasExitTime;
	j["exitTime"] = exitTime;
	j["blendTime"] = blendTime;
	nlohmann::json conditionJson = nlohmann::json::array();
	for (auto& condition : conditions)
	{
		conditionJson.push_back(condition.Serialize());
	}
	j["conditions"] = conditionJson;
	return j;
}

AniTransition AniTransition::Deserialize()
{
	return AniTransition();
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