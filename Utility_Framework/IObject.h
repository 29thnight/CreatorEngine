#pragma once
#ifndef interface
#define interface struct
#endif

#include <string>
// HashedGuid — 예전에는 includer(Object.h)의 전이에 기댔다. 자급자족으로 세운다.
#include "TypeTrait.h"

interface IObject
{
	virtual size_t GetInstanceID() const = 0;
	virtual HashedGuid GetTypeID() const = 0;
	virtual std::string ToString() const = 0;
};
