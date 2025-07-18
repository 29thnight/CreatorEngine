#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "PlayerInput.generated.h"
class PlayerInput : public Component
{
public:
   ReflectPlayerInput
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(PlayerInput)

	[[Property]]
	std::string m_actionMapName{};
	[[Property]]
	std::string m_scriptName{};
	[[Property]]
	std::string m_funName;
};

