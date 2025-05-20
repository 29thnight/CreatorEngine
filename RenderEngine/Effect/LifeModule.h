#pragma once
#include "ParticleModule.h"

// none
class LifeModule : public ParticleModule
{
public:
	void Update(float delta, std::vector<ParticleData>& particles) override;
private:
};