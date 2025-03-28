#pragma once
#ifndef interface
#define interface struct
#endif

interface ILifeSycle
{
	virtual void Start() = 0;
	virtual void Update(float tick) = 0;
	virtual void FixedUpdate(float fixedTick) = 0;
	virtual void LateUpdate(float tick) = 0;
};