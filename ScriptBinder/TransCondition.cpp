#include "TransCondition.h"
#include "AnimationController.h"
#include "Animator.h"
bool TransCondition::CheckTrans()
{
	auto& parameters = m_ownerController->m_owner->Parameters;
	for (auto& parameter : parameters)
	{
		if (parameter.vType == ValueType::Float)
		{
			switch (cType)
			{
			case ConditionType::Greater:
				return parameter.fValue > CompareParameter.fValue;
			case ConditionType::Less:
				return parameter.fValue < CompareParameter.fValue;
			}
		}
		else if (parameter.vType == ValueType::Int)
		{
			switch (cType)
			{
			case ConditionType::Greater:
				return parameter.iValue > CompareParameter.iValue;
			case ConditionType::Less:
				return parameter.iValue < CompareParameter.iValue;
			}
		}
		else if (parameter.vType == ValueType::Bool)
		{
			switch (cType)
			{
			case ConditionType::Equal:
				return parameter.bValue == CompareParameter.bValue;
			case ConditionType::NotEqual:
				return parameter.bValue == CompareParameter.bValue;
			}
		}
		else if (parameter.vType == ValueType::Trigger)
		{
			return parameter.tValue == true;
		}
	}
	

	return false;
}
