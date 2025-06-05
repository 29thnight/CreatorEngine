#include "AniTransition.h"
#include "AniTransition.h"


AniTransition::AniTransition(std::string curStatename, std::string nextStatename)
	:curState(curStatename),nextState(nextStatename)
{

}
AniTransition::~AniTransition()
{
}

void AniTransition::AddDefaultCondition()
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

