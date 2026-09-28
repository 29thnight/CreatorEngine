#pragma once
#include "Core.Minimal.h"
#include "Component.h"

class [[reflgen::reflect]] InvalidScriptComponent : public meta::identity<InvalidScriptComponent, Component>
{
   public:
public:
	InvalidScriptComponent() = default;

	const char* m_errorMessage{ "Invalid Script - Please delete this ScriptComponent." };
};
