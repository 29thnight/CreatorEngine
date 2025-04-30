#pragma once
#include "Core.Minimal.h"
#include "aniParameter.generated.h"


enum class conditionType
{
	Greater,
	Less,
	Equal,
	NotEqual,
};
AUTO_REGISTER_ENUM(conditionType)


enum class valueType
{
	Float,
	Int,
	Bool,
};
AUTO_REGISTER_ENUM(valueType)

class aniParameter
{
public:
   ReflectaniParameter
	[[Serializable]]
	aniParameter() = default;
	template<typename T>
	aniParameter(T value, valueType _vType, std::string _name = "None")
		: vType(_vType), name(_name)
	{
		switch (vType)
		{
		case valueType::Float:
			fValue = static_cast<float>(value);
			break;
		case valueType::Int:
			iValue = static_cast<int>(value);
			break;
		case valueType::Bool:
			bValue = static_cast<bool>(value);
			break;
		}
	}

	template<typename T>
	void UpdateParameter(T value)
	{
		switch (vType)
		{
		case valueType::Float:
			fValue = static_cast<float>(value);
			break;
		case valueType::Int:
			iValue = static_cast<int>(value);
			break;
		case valueType::Bool:
			bValue = static_cast<bool>(value);
			break;
		}
	}
	[[Property]]
	valueType vType =valueType::Float;
	[[Property]]
	std::string name = "None";
	[[Property]]
	float fValue{};
	[[Property]]
	int iValue{};
	[[Property]]
	bool bValue{};
};
