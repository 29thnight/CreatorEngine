#pragma once
#include "Core.Minimal.h"

enum class BlackBoardType
{
	None = 0,
	Bool,
	Int,
	Float,
	String,
	Vector2,
	Vector3,
	Vector4,
	GameObject, // 게임 오브젝트 타입
	Transform,  // 트랜스폼 타입
};
AUTO_REGISTER_ENUM(BlackBoardType)