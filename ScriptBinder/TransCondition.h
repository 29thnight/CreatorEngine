#pragma once
#include "Core.Minimal.h"
#include "TransCondition.generated.h"
#include "ConditionParameter.h"

class AnimationController;
class TransCondition
{
public:
   ReflectTransCondition
	[[Serializable]]
	TransCondition() = default;


	TransCondition(float Comparevalue, ConditionType cType, ValueType vType) : cType(cType) , CompareParameter(Comparevalue, vType)
	{
	

	}
	bool CheckTrans();
	//Ÿ�� ,�� ,�Լ� 
	[[Property]]
	std::string valueName{};
	[[Property]]
	ConditionType cType = ConditionType::Equal;
	[[Property]]
	ConditionParameter CompareParameter;

	AnimationController* m_ownerController{};
};

