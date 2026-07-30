#pragma once
#include "Core.Minimal.h"
#include "ConditionParameter.generated.h"
#include <nlohmann/json.hpp>

enum class ConditionType
{
	Greater,
	Less,
	Equal,
	NotEqual,
	True,
	False,
	None,
};
AUTO_REGISTER_ENUM(ConditionType)


enum class ValueType : std::uint16_t
{
	Float,
	Int,
	Bool,
	Trigger,
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
		case ValueType::Trigger:
			tValue = static_cast<bool>(value);
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
		case ValueType::Trigger:
			tValue = static_cast<bool>(value);
			break;
		}
	}

	void ResetTrigger() 
	{
		tValue = false; 
	}

	template<typename T>
	T GetValue()
	{
		switch (vType)
		{
		case ValueType::Float:
			return static_cast<T>(fValue);
		case ValueType::Int:
			return static_cast<T>(iValue);
		case ValueType::Bool:
			return static_cast<T>(bValue);
		case ValueType::Trigger:
			// 트리거는 tValue에 담긴다(UpdateParameter·ResetTrigger가 그쪽을 쓴다).
			// bValue를 읽고 있어서 트리거는 늘 false로 보였다 — 전이 판정은
			// TransCondition이 tValue를 직접 봐서 무사했고, 읽기 경로만 어긋나 있었다.
			return static_cast<T>(tValue);
		}

		// vType이 범위를 벗어나면(직렬화가 깨진 경우 등) 여기로 온다.
		return T{};
	}

	template<typename T>
	void SetParameter(T value, ValueType _vType, std::string _name = "None")
	{
		vType = _vType;
		name = _name;
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
		case ValueType::Trigger:
			tValue = static_cast<bool>(value);
			break;
		}
	}
	nlohmann::json Serialize();
	void Deserialize();

public:
	[[Property]]
	std::string name = "None";
	[[Property]]
	float fValue{};
	[[Property]]
	int iValue{};
	[[Property]]
	ValueType vType =ValueType::Float;
	[[Property]]
	bool bValue{};
	[[Property]]
	bool tValue{false};
};
