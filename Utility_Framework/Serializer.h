#pragma once
#include "Core.Minimal.h"

interface Serializer
{
	virtual void Serialize(_inout json& out) = 0;
	virtual void DeSerialize(_in json& in) = 0;
};