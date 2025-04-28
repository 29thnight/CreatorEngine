#include "AniTransition.h"


AniTransition::AniTransition(std::string curStatename, std::string nextStatename)
	:curState(curStatename),nextState(nextStatename)
{

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
		return condition.CheckTrans();
	}

	return true;
}

bool TransCondition::CheckTrans()
{
	/*if (!value)
				return false;

			switch (cType)
			{
			case conditionType::Greater:
				return *value > Comparevalue;
			case conditionType::Less:
				return *value < Comparevalue;
			case conditionType::Equal:
				return *value == Comparevalue;
			case conditionType::NotEqual:
				return *value != Comparevalue;
			default:
				return false;
			}*/

	if (vType == valueType::Float)
	{
		switch (cType)
		{
		case conditionType::Greater:
			return *FValue > FCompareValue;
		case conditionType::Less:
			return *FValue < FCompareValue;
		}
	}
	else if (vType == valueType::Int)
	{
		switch (cType)
		{
		case conditionType::Greater:
			return *IValue > ICompareValue;
		case conditionType::Less:
			return *IValue < ICompareValue;
		case conditionType::Equal:
			return *IValue == ICompareValue;
		case conditionType::NotEqual:
			return *IValue != ICompareValue;
		}
	}
	else if (vType == valueType::Bool)
	{
		switch (cType)
		{
		case conditionType::Equal:
			return *BValue == BCompareValue;
		case conditionType::NotEqual:
			return *BValue != BCompareValue;
		}
	}
	return false;
}
