#pragma once
#ifndef interface
#define interface struct
#endif
#include "TypeTrait.h"
#include <string>

interface IComponent
{
	virtual std::string ToString() const = 0;
	virtual HashedGuid GetTypeID() const = 0;
	virtual HashedGuid GetInstanceID() const = 0;
};