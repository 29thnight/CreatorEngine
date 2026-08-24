#include "TransCondition.h"
#include "AnimationController.h"
#include "Animator.h"
bool TransCondition::CheckTrans()
{
	auto& parameters = m_ownerController->m_owner->Parameters;

	if (valueParameter == nullptr)
		valueParameter = m_ownerController->m_owner->FindParameter(valueName);
	if (valueParameter != nullptr)
	{
		for (auto& parameter : parameters)
		{
			if (parameter != valueParameter) continue;
			if (parameter->vType == ValueType::Float)
			{
				switch (cType)
				{
				case ConditionType::Greater:
					return parameter->fValue > CompareParameter.fValue;
				case ConditionType::Less:
					return parameter->fValue < CompareParameter.fValue;
				case ConditionType::Equal:
					return parameter->fValue == CompareParameter.fValue;
				case ConditionType::NotEqual:
					return parameter->fValue != CompareParameter.fValue;
				}
			}
			else if (parameter->vType == ValueType::Int)
			{
				switch (cType)
				{
				case ConditionType::Greater:
					return parameter->iValue > CompareParameter.iValue;
				case ConditionType::Less:
					return parameter->iValue < CompareParameter.iValue;
				case ConditionType::Equal:
					return parameter->iValue == CompareParameter.iValue;
				case ConditionType::NotEqual:
					return parameter->iValue != CompareParameter.iValue;
				}
			}
			else if (parameter->vType == ValueType::Bool)
			{
				switch (cType)
				{
				case ConditionType::True:
					return parameter->bValue == true;
				case ConditionType::False:
					return parameter->bValue == false;
				}
			}
			else if (parameter->vType == ValueType::Trigger)
			{
				return parameter->tValue == true;
			}
		}
	}

	return false;
}

void TransCondition::SetValue(std::string valueName)
{
	auto parameter = m_ownerController->m_owner->FindParameter(valueName);
	if(parameter)
		valueParameter = m_ownerController->m_owner->FindParameter(valueName);
}

void TransCondition::SetCondition(std::string _parameterName)
{
	ConditionParameter* parameter = m_ownerController->m_owner->FindParameter(_parameterName);

	if(parameter)
		valueParameter = parameter;
	valueName = _parameterName;
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

nlohmann::json TransCondition::Serialize()
{
	nlohmann::json j;
	j["valueName"] = valueName;
	int ctype = 0;
	if (cType == ConditionType::Greater)
		ctype = 0;
	else if (cType == ConditionType::Less)
		ctype = 1;
	else if (cType == ConditionType::Equal)
		ctype = 2;
	else if (cType == ConditionType::NotEqual)
		ctype = 3;
	else if (cType == ConditionType::True)
		ctype = 4;
	else if (cType == ConditionType::False)
		ctype = 5;
	else
	{
		ctype = 0;
	}
	j["cType"] = ctype;
	j["fValue"] = CompareParameter.fValue;
	j["iValue"] = CompareParameter.iValue;
	j["bValue"] = CompareParameter.bValue;
	j["tValue"] = CompareParameter.tValue;

	return j;
}

TransCondition TransCondition::Deserialize()
{
	return TransCondition();
}
