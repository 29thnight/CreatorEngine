#pragma once
#include <physx/PxPhysicsAPI.h>
#include "../Utility_Framework/Core.Minimal.h"

class CharacterController
{
public:
	CharacterController();
	~CharacterController();

protected:
	unsigned int m_id;
	unsigned int m_layerNumber;

	Mathf::Vector3 m_inputMove;
	bool m_IsDynamic;
	std::array<bool, 4> m_bMoveRestrict;


	physx::PxMaterial* m_material;
	physx::PxController* m_controller;

};

