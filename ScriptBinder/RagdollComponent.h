#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "ArticulationData.h"
#include "ArticulationLoader.h"


class AriculationData;
class ArticulationLoader;

class RagdollComponent : public Component, public ICollider
{
public:

	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(RagdollComponent)




private:
	unsigned int m_ragdollID;
	unsigned int m_collsionCount = 0;

	bool m_bIsRagdoll{ false };

	float m_fBlendTime{ 0.0f };
	float m_fComplateTime{ 1.0f };

	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	std::string m_ArticulationPath;
	ArticulationData* m_articulationData;

};