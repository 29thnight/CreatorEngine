#pragma once
#ifndef interface
#define interface struct
#endif

#include <string>
// HashedGuid — 예전에는 includer(Object.h)의 전이에 기댔다. 자급자족으로 세운다.
#include "TypeTrait.h"

interface IObject
{
	// 이 인터페이스로 소멸이 일어난 적은 아직 없지만, 그때 UB가 되는 것을 막는다.
	// 파생(Object)에 이미 vtable이 있어 크기는 변하지 않는다.
	virtual ~IObject() = default;

	virtual size_t GetInstanceID() const = 0;
	virtual HashedGuid GetTypeID() const = 0;
	virtual std::string ToString() const = 0;
};
