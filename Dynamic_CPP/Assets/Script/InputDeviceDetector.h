#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "InputDeviceDetector.generated.h"

class InputDeviceDetector : public ModuleBehavior
{
public:
   ReflectInputDeviceDetector
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(InputDeviceDetector)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	[[Method]]
	void MoveSelector(Mathf::Vector2 dir);
};
