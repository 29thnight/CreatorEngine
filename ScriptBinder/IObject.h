#pragma once
#ifndef interface
#define interface struct
#endif

#include <string>

interface IObject
{
	virtual size_t GetInstanceID() const = 0;
	virtual HashedGuid GetTypeID() const = 0;
	virtual std::string ToString() const = 0;
};
