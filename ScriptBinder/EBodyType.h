#pragma once
#include "Core.Minimal.h"

enum class EBodyType
{
	STATIC,
	DYNAMIC,
	KINEMATIC,
	DISABLE, // ��Ȱ��ȭ ����
	END // Enum�� ���� ��Ÿ���� ��
};
AUTO_REGISTER_ENUM(EBodyType)