#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "ConditionParameter.h"
#include <nlohmann/json.hpp>

class AnimationController;
class TransCondition
{
public:
   static consteval auto describe()
   {
       return meta::describe<TransCondition>(
           meta::member<&TransCondition::valueName>(),
           meta::member<&TransCondition::CompareParameter>(),
           meta::member<&TransCondition::cType>());
   }
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
	nlohmann::json Serialize();
	TransCondition Deserialize();
	//타입 ,값 ,함수 
	std::string valueName = "None";
	ConditionParameter* valueParameter;
	ConditionParameter CompareParameter;
	AnimationController* m_ownerController{};
	ConditionType cType = ConditionType::Equal;
};

