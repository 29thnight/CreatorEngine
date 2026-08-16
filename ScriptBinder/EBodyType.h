#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class EBodyType
{
	STATIC,
	DYNAMIC,
	KINEMATIC,
	DISABLE, // 비활성화 상태
	END // Enum의 끝을 나타내는 값
};
AUTO_REGISTER_ENUM(EBodyType)