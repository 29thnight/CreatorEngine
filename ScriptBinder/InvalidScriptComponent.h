#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "InvalidScriptComponent.generated.h"

class InvalidScriptComponent : public Component
{
public:
   ReflectInvalidScriptComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(InvalidScriptComponent)

	[[Property]]
	const char* m_errorMessage{ "Invalid Script - Please delete this ScriptComponent." };
};
