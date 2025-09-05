#pragma once
#include "Core.Minimal.h"
#include "TransCondition.generated.h"
#include "ConditionParameter.h"
#include <nlohmann/json.hpp>

class AnimationController;
class TransCondition
{
public:
   ReflectTransCondition
	[[Serializable]]
	TransCondition() = default;

	template<typename T>
	TransCondition(T Comparevalue, ConditionType _cType, ValueType vType) :CompareParameter(Comparevalue, vType)
	{
		if (_cType == ConditionType::None)
		{
			switch (vType)
			{
			case ValueType::Float:
				_cType = ConditionType::Greater;
				break;
			case ValueType::Int:
				_cType = ConditionType::Greater;
				break;
			case ValueType::Bool:
				_cType = ConditionType::True;
				break;
			case ValueType::Trigger:
				_cType = ConditionType::None;
				break;
			}
		}
		cType = _cType;
	}
	bool CheckTrans();

	std::string GetConditionType()
	{
		switch(cType)
		{
		case ConditionType::Equal:
			return "Equal";
		case ConditionType::NotEqual:
			return "NotEqual";
		case ConditionType::Greater:
			return "Greater";
		case ConditionType::Less:
			return "Less";
		case ConditionType::True:
			return "True";
		case ConditionType::False:
			return "False";
		case ConditionType::None:
			return "None";
		}
	}

	void SetValue(std::string valueName);
	void SetCondition(std::string _parameterName);

	void SetConditionType(ConditionType _conditionType) { cType = _conditionType;}
	//타입 ,값 ,함수 
	[[Property]]
	std::string valueName = "None";

	ConditionParameter* valueParameter;
	[[Property]]
	ConditionType cType = ConditionType::Equal;
	[[Property]]
	ConditionParameter CompareParameter;

	AnimationController* m_ownerController{};


	nlohmann::json Serialize();
	TransCondition Deserialize();
};

