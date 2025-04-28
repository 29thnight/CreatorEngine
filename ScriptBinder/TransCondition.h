#pragma once
#include "Core.Minimal.h"
#include "TransCondition.generated.h"
#include "aniStruct.h"

class aniFSM;
class TransCondition
{
public:
   ReflectTransCondition
	[[Serializable]]
	TransCondition() = default;


	TransCondition(float Comparevalue, conditionType cType, valueType vType) : cType(cType) , CompareParameter(Comparevalue, vType)
	{
	

	}
	bool CheckTrans();
	//타입 ,값 ,함수 
	[[Property]]
	std::string valueName{};
	[[Property]]
	conditionType cType = conditionType::Equal;
	[[Property]]
	aniParameter CompareParameter;

	aniFSM* ownerFSM{};
};

