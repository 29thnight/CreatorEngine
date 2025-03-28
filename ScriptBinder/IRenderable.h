#pragma once
#ifndef interface
#define interface struct
#endif

interface IRenderable
{
	virtual bool IsEnabled() const = 0;
	virtual void SetEnabled(bool enabled) = 0;
};