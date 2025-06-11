#include "TransCondition.h"
#include "AnimationController.h"
#include "Animator.h"
bool TransCondition::CheckTrans()
{
	auto& parameters = m_ownerController->m_owner->Parameters;

	
	for (auto& parameter : parameters)
	{
		if (parameter.name != valueName) continue;
		if (parameter.vType == ValueType::Float)
		{
			switch (cType)
			{
			case ConditionType::Greater:
				return parameter.fValue > CompareParameter.fValue;
			case ConditionType::Less:
				return parameter.fValue < CompareParameter.fValue;
			case ConditionType::Equal:
				return parameter.fValue == CompareParameter.fValue;
			case ConditionType::NotEqual:
				return parameter.fValue != CompareParameter.fValue;
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
			case ConditionType::Equal:
				return parameter.iValue == CompareParameter.iValue;
			case ConditionType::NotEqual:
				return parameter.iValue != CompareParameter.iValue;
			}
		}
		else if (parameter.vType == ValueType::Bool)
		{
			switch (cType)
			{
			case ConditionType::True:
				return CompareParameter.bValue;
			case ConditionType::False:
				return !CompareParameter.bValue;
			}
		}
		else if (parameter.vType == ValueType::Trigger)
		{
			return parameter.tValue == true;
		}
	}
	

	return false;
}

void TransCondition::SetCondition(std::string _parameterName)
{
	ConditionParameter* parameter = m_ownerController->m_owner->FindParameter(_parameterName);
	valueName = parameter->name;
		switch (parameter->vType)
		{
		case ValueType::Float:
			cType = ConditionType::Greater;
			CompareParameter.fValue = 0;
			break;
		case ValueType::Int:
			cType = ConditionType::Greater;
			CompareParameter.iValue = 0;
			break;
		case ValueType::Bool:
			cType = ConditionType::True;
			CompareParameter.bValue = true;
			break;
		case ValueType::Trigger:
			cType = ConditionType::None;
			break;
		}
	
}
