#include "TransCondition.h"
#include "AnimationController.h"
bool TransCondition::CheckTrans()
{
	
	for (auto& para : ownerFSM->Parameters)
	{
		if (para.vType == valueType::Float)
		{
			switch (cType)
			{
			case conditionType::Greater:
				return para.fValue > CompareParameter.fValue;
			case conditionType::Less:
				return para.fValue < CompareParameter.fValue;
			}
		}
		else if (para.vType == valueType::Int)
		{
			switch (cType)
			{
			case conditionType::Greater:
				return para.iValue > CompareParameter.iValue;
			case conditionType::Less:
				return para.iValue < CompareParameter.iValue;
			}
		}
		if (para.vType == valueType::Bool)
		{
			switch (cType)
			{
			case conditionType::Equal:
				return para.bValue == CompareParameter.bValue;
			case conditionType::NotEqual:
				return para.bValue == CompareParameter.bValue;
			}
		}
	}
	

	return false;
}
