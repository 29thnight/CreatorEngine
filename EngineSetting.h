#pragma once
#include "EngineSetting.h"
#include "Core.Minimal.h"

class EngineSetting : public Singleton<EngineSetting>
{
private:
	friend class Singleton;
	EngineSetting() = default;
	~EngineSetting() = default;

public:

};