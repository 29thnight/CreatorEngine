#pragma once
#include "Core.Minimal.h"
#include "ConditionParameter.generated.h"


enum class ConditionType
{
	Greater,
	Less,
	Equal,
	NotEqual,
};
AUTO_REGISTER_ENUM(ConditionType)


enum class ValueType
{
	Float,
	Int,
	Bool,
};
AUTO_REGISTER_ENUM(ValueType)

class ConditionParameter
{
public:
   ReflectConditionParameter
	[[Serializable]]
	ConditionParameter() = default;
	template<typename T>
	ConditionParameter(T value, ValueType _vType, std::string _name = "None")
		: vType(_vType), name(_name)
	{
		switch (vType)
		{
		case ValueType::Float:
			fValue = static_cast<float>(value);
			break;
		case ValueType::Int:
			iValue = static_cast<int>(value);
			break;
		case ValueType::Bool:
			bValue = static_cast<bool>(value);
			break;
		}
	}

	template<typename T>
	void UpdateParameter(T value)
	{
		switch (vType)
		{
		case ValueType::Float:
			fValue = static_cast<float>(value);
			break;
		case ValueType::Int:
			iValue = static_cast<int>(value);
			break;
		case ValueType::Bool:
			bValue = static_cast<bool>(value);
			break;
		}
	}
	[[Property]]
	ValueType vType =ValueType::Float;
	[[Property]]
	std::string name = "None";
	[[Property]]
	float fValue{};
	[[Property]]
	int iValue{};
	[[Property]]
	bool bValue{};
};
