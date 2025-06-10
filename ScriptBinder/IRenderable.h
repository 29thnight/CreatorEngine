#pragma once
#ifndef interface
#define interface struct
#endif

//TODO : 
interface
[[deprecated("Will be change IRenderable interface soon, Please! Don't inheritance this interface")]]
IRenderable
{
	[[deprecated("Will be change IRenderable interface soon, Please! Don't inheritance this interface")]]
	virtual bool IsEnabled() const = 0;
	[[deprecated("Will be change IRenderable interface soon, Please! Don't inheritance this interface")]]
	virtual void SetEnabled(bool enabled) = 0;
};