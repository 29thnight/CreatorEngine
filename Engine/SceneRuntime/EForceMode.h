#pragma once
#include "../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Foundation/Core.Minimal.h"

enum class EForceMode
{
	FORCE,
	IMPULSE,
	VELOCITY_CHANGE,
	ACCELERATION,
	NONE
};
