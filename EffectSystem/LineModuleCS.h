#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"
class LineModuleCS : public ParticleModule, public ISerializable
{
public:
	LineModuleCS();
	~LineModuleCS();
};

