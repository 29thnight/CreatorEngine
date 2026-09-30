#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "ArticulationData.h"
#include "ArticulationLoader.h"


class AriculationData;
class ArticulationLoader;

class [[reflgen::reflect]] RagdollComponent : public meta::identity<RagdollComponent, Component>, public ICollider
{
   public:
public:
	RagdollComponent() = default;

	bool m_bIsRagdoll{ false };


private:
	[[reflgen::ignore]]
	unsigned int m_ragdollID;

	[[reflgen::ignore]]
	unsigned int m_collsionCount = 0;

	//bool m_bIsRagdoll{ false };

	[[reflgen::ignore]]
	float m_fBlendTime{ 0.0f };

	[[reflgen::ignore]]
	float m_fComplateTime{ 1.0f };

	[[reflgen::ignore]]
	math::vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };

	[[reflgen::ignore]]
	math::quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	[[reflgen::ignore]]
	std::string m_ArticulationPath;

	[[reflgen::ignore]]
	ArticulationData* m_articulationData;


	// ICollider을(를) 통해 상속됨
	void SetPositionOffset(math::vector3 pos) override {}

	math::vector3 GetPositionOffset() override { return m_posOffset; }

	void SetRotationOffset(math::quaternion rotation) override {}

	math::quaternion GetRotationOffset() override { return m_rotOffset; }

	void OnTriggerEnter(ICollider* other) override {}

	void OnTriggerStay(ICollider* other) override {}

	void OnTriggerExit(ICollider* other) override {}

	void OnCollisionEnter(ICollider* other) override {}

	void OnCollisionStay(ICollider* other) override {}

	void OnCollisionExit(ICollider* other) override {}

	void SetColliderType(EColliderType type) override {}
	EColliderType GetColliderType() const override { return EColliderType::COLLISION; }
};
