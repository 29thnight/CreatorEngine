#include "TransCondition.h"
#include "AnimationController.h"
bool TransCondition::CheckTrans()
{
	
	for (auto& para : m_ownerController->Parameters)
	{
		if (para.vType == ValueType::Float)
		{
			switch (cType)
			{
			case ConditionType::Greater:
				return para.fValue > CompareParameter.fValue;
			case ConditionType::Less:
				return para.fValue < CompareParameter.fValue;
			}
		}
		else if (para.vType == ValueType::Int)
		{
			switch (cType)
			{
			case ConditionType::Greater:
				return para.iValue > CompareParameter.iValue;
			case ConditionType::Less:
				return para.iValue < CompareParameter.iValue;
			}
		}
		if (para.vType == ValueType::Bool)
		{
			switch (cType)
			{
			case ConditionType::Equal:
				return para.bValue == CompareParameter.bValue;
			case ConditionType::NotEqual:
				return para.bValue == CompareParameter.bValue;
			}
		}
	}
	

	return false;
}
