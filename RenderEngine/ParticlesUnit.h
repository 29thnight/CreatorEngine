#pragma once
#include "SpawnSystem.h"

class ParticlesUnit
{
public:
	ParticlesUnit() = default;
	~ParticlesUnit() = default;

	std::vector<ParticleModule*> m_modules; //파티클 모듈들(제외 SpawnSystem 및 Render Module)

	HashedGuid m_SpawnEventPinID{ make_guid() }; //input pin ID for Editor use
	HashedGuid m_RenderEventPinID{ make_guid() }; //output pin ID for Editor use
};