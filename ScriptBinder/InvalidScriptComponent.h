#pragma once
#include "Core.Minimal.h"
#include "Component.h"

class InvalidScriptComponent : public meta::identity<InvalidScriptComponent, Component>
{
public:
   static consteval auto describe()
   {
       return meta::describe<InvalidScriptComponent>(
           meta::base<Component>(),
           meta::member<&InvalidScriptComponent::m_errorMessage>());
   }
	InvalidScriptComponent() = default;

	const char* m_errorMessage{ "Invalid Script - Please delete this ScriptComponent." };
};
