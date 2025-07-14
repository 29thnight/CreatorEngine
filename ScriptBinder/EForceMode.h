#pragma once
#include "Core.Minimal.h"

enum class EForceMode
{
	FORCE,
	IMPULSE,
	VELOCITY_CHANGE,
	ACCELERATION,
	NONE
};
AUTO_REGISTER_ENUM(EForceMode)