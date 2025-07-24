#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "ArticulationData.h"
#include "ArticulationLoader.h"
#include "RagdollComponent.generated.h"


class AriculationData;
class ArticulationLoader;

class RagdollComponent : public Component, public ICollider
{
public:
   ReflectRagdollComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(RagdollComponent)

	[[Property]]
	bool m_bIsRagdoll{ false };


private:
	unsigned int m_ragdollID;
	unsigned int m_collsionCount = 0;

	//bool m_bIsRagdoll{ false };

	float m_fBlendTime{ 0.0f };
	float m_fComplateTime{ 1.0f };

	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	std::string m_ArticulationPath;
	ArticulationData* m_articulationData;


	// ICollider을(를) 통해 상속됨
	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override {}

	DirectX::SimpleMath::Vector3 GetPositionOffset() override { return m_posOffset; }

	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override {}

	DirectX::SimpleMath::Quaternion GetRotationOffset() override { return m_rotOffset; }

	void OnTriggerEnter(ICollider* other) override {}

	void OnTriggerStay(ICollider* other) override {}

	void OnTriggerExit(ICollider* other) override {}

	void OnCollisionEnter(ICollider* other) override {}

	void OnCollisionStay(ICollider* other) override {}

	void OnCollisionExit(ICollider* other) override {}

	void SetColliderType(EColliderType type) override {}
	EColliderType GetColliderType() const override { return EColliderType::COLLISION; }
};
