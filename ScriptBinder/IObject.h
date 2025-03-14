#pragma once
#ifndef interface
#define interface struct
#endif

#include <string>

interface IObject
{
	virtual void Initialize() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};

	virtual unsigned int GetInstanceID() const {};
	virtual std::string ToString() const = 0;
};