#pragma once
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "Component.h"

class [[reflgen::reflect, reflgen::public_methods]] InvalidScriptComponent : public meta::identity<InvalidScriptComponent, Component>
{
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
	InvalidScriptComponent() = default;

	const char* m_errorMessage{ "Invalid Script - Please delete this ScriptComponent." };
};
