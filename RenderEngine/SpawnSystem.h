#pragma once
#include "Core.Minimal.h"
#include "ParticleModule.h"
#include "EffectEventUnit.h"

class SpawnSystem : public ParticleModule, public IEffectImGuiRender
{
public:
	SpawnSystem() = default;
	~SpawnSystem() = default;

    EffectEventUnit m_spawnStartEvents{};
	EffectEventUnit m_spawnEndEvents{};

	HashedGuid m_instanceID{ make_guid() };
	HashedGuid m_startPinID{ make_guid() }; //input pin ID
	HashedGuid m_endPinID{ make_guid() }; //input pin ID
	HashedGuid m_SpawnEventPinID{ make_guid() }; //output pin ID for Editor use

	virtual void DrawNodeGUI() override abstract; //pure virtual function for drawing the node GUI in the editor
};