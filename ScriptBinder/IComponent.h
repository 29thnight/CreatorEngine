#pragma once
#ifndef interface
#define interface struct
#endif

#include <string>

interface IComponent
{
	virtual void Initialize() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};
};