#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "BlackBoardEnum.h"

struct BlackBoardValue
{
   static consteval auto describe()
   {
       return meta::describe<BlackBoardValue>(
           meta::member<&BlackBoardValue::Type>(),
           meta::member<&BlackBoardValue::BoolValue>(),
           meta::member<&BlackBoardValue::IntValue>(),
           meta::member<&BlackBoardValue::FloatValue>(),
           meta::member<&BlackBoardValue::StringValue>(),
           meta::member<&BlackBoardValue::Vec2Value>(),
           meta::member<&BlackBoardValue::Vec3Value>(),
           meta::member<&BlackBoardValue::Vec4Value>());
   }
	BlackBoardValue() = default;
	~BlackBoardValue() = default;

	BlackBoardType Type = BlackBoardType::None;
	bool			BoolValue;
	int				IntValue;
	float			FloatValue;
	std::string		StringValue;  // 문자열, GameObjectName, TransformPath 등
	Mathf::Vector2	Vec2Value;
	Mathf::Vector3	Vec3Value;
	Mathf::Vector4	Vec4Value;

	void Clear()
	{
		Type = BlackBoardType::None;
		StringValue.clear();
	}
};
