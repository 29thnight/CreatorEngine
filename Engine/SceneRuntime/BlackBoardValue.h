#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "BlackBoardEnum.h"

struct BlackBoardValue
{
   public:
   static consteval auto reflect()
   {
       using Self = BlackBoardValue;
       return meta::schema<Self>(
           meta::field<&Self::Type>,
           meta::field<&Self::BoolValue>,
           meta::field<&Self::IntValue>,
           meta::field<&Self::FloatValue>,
           meta::field<&Self::StringValue>,
           meta::field<&Self::Vec2Value>,
           meta::field<&Self::Vec3Value>,
           meta::field<&Self::Vec4Value>);
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
