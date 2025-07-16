#pragma once
#include "Core.Minimal.h"
#include "BlackBoardEnum.h"
#include "BlackBoardValue.generated.h"

struct BlackBoardValue
{
   ReflectBlackBoardValue
	[[Serializable]]
	BlackBoardValue() = default;
	~BlackBoardValue() = default;

	[[Property]]
	BlackBoardType Type = BlackBoardType::None;
	[[Property]]
	bool			BoolValue;
	[[Property]]
	int				IntValue;
	[[Property]]
	float			FloatValue;
	[[Property]]
	std::string		StringValue;  // 문자열, GameObjectName, TransformPath 등
	[[Property]]
	Mathf::Vector2	Vec2Value;
	[[Property]]
	Mathf::Vector3	Vec3Value;
	[[Property]]
	Mathf::Vector4	Vec4Value;

	void Clear()
	{
		Type = BlackBoardType::None;
		StringValue.clear();
	}
};
