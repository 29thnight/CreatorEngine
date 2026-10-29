#pragma once
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include <string>
#include "BlackBoardEnum.h"

struct [[reflgen::reflect, reflgen::public_methods]] BlackBoardValue
{
   public:
	BlackBoardValue() = default;
	~BlackBoardValue() = default;

	BlackBoardType Type = BlackBoardType::None;
	bool			BoolValue;
	int				IntValue;
	float			FloatValue;
	std::string		StringValue;  // 문자열, GameObjectName, TransformPath 등
	math::vector2	Vec2Value;
	math::vector3	Vec3Value;
	math::vector4	Vec4Value;

	void Clear()
	{
		Type = BlackBoardType::None;
		StringValue.clear();
	}
};
