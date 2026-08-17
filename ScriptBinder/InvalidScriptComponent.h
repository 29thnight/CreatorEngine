#pragma once
#include "Core.Minimal.h"
#include "Component.h"

class InvalidScriptComponent : public meta::identity<InvalidScriptComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_errorMessage>);
   }
public:
	InvalidScriptComponent() = default;

	const char* m_errorMessage{ "Invalid Script - Please delete this ScriptComponent." };
};
