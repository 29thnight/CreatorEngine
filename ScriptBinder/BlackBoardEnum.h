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
	GameObject, // ���� ������Ʈ Ÿ��
	Transform,  // Ʈ������ Ÿ��
};
AUTO_REGISTER_ENUM(BlackBoardType)