#pragma once
#include "Core.Minimal.h"

interface IComponent
{
	virtual void Initialize() = 0;
	virtual void FixedUpdate(float fixedTick) = 0;
	virtual void Update(float tick) = 0;
	virtual void LateUpdate(float tick) = 0;
	virtual void EditorContext() = 0;
	virtual void Serialize(_inout json& out) = 0;
	virtual void DeSerialize(_in json& in) = 0;
};
