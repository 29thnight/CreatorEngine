#pragma once
#ifndef interface
#define interface struct
#endif

#include <string>

interface IComponent
{
	virtual std::string ToString() const = 0;
	virtual uint32_t GetTypeID() const = 0;
	virtual size_t GetInstanceID() const = 0;
};