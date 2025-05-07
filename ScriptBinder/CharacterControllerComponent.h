#pragma once
#include "SceneManager.h"
#include "GameObject.h"
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../physics/Physx.h"
#include "../Physics/ICollider.h"
#include "RigidBodyComponent.h"
#include "InputManager.h"
#include "directxtk\SimpleMath.h"
#include "Scene.h"
#include "CharacterControllerComponent.generated.h"

class CharacterControllerComponent : public Component, public ICollider
{
public:

   
   
   ReflectCharacterControllerComponent
	[[Serializable(Inheritance:Component)]]
   CharacterControllerComponent() {
	   m_name = "CharacterControllerComponent"; 
	   m_typeID = TypeTrait::GUIDCreator::GetTypeID<CharacterControllerComponent>();

	   m_onStartHandle = SceneManagers->GetActiveScene()->StartEvent.AddRaw(this, &CharacterControllerComponent::OnStart);
	   m_onFixedUpdateHandle = SceneManagers->GetActiveScene()->FixedUpdateEvent.AddRaw(this, &CharacterControllerComponent::OnFixedUpdate);
	   m_onLateUpdateHandle = SceneManagers->GetActiveScene()->LateUpdateEvent.AddRaw(this, &CharacterControllerComponent::OnLateUpdate);


   } virtual ~CharacterControllerComponent() {
	   SceneManagers->GetActiveScene()->StartEvent.Remove(m_onStartHandle);
	   SceneManagers->GetActiveScene()->FixedUpdateEvent.Remove(m_onFixedUpdateHandle);
	   SceneManagers->GetActiveScene()->LateUpdateEvent.Remove(m_onLateUpdateHandle);
   };

	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };


	//==========================
	//�� ���ο��� ����	�Ǵ� �Լ���
	void OnStart();
	void OnFixedUpdate(float fixedDeltaTime);
	void OnLateUpdate(float fixedDeltaTime);

	//==========================
	Core::DelegateHandle m_onStartHandle;
	Core::DelegateHandle m_onFixedUpdateHandle;
	Core::DelegateHandle m_onLateUpdateHandle;
	//==========================



	//��Ʈ�ѷ� ���� ��ȯ
	CharacterControllerInfo GetControllerInfo() const
	{
		return m_controllerInfo;
	}

	//��Ʈ�ѷ� ���� ����
	void SetControllerInfo(const CharacterControllerInfo& info)
	{
		m_controllerInfo = info;
		m_controllerInfo.contactOffset = std::max(m_controllerInfo.contactOffset, 0.0001f);
	}

	//��Ʈ�ѷ� �̵� ��	�� ��ȯ
	CharacterMovementInfo GetMovementInfo() const
	{
		return m_movementInfo;
	}
	//��Ʈ�ѷ� �̵� ���� ����
	void SetMovementInfo(const CharacterMovementInfo& info)
	{
		m_movementInfo = info;
	}

	//falling �������� üũ
	bool IsFalling() const
	{
		return m_bIsFall;
	}
	void SetFalling(bool isFall)
	{
		m_bIsFall = isFall;
	}

	//offset
	DirectX::SimpleMath::Vector3 GetPositionOffset() override
	{
		return m_posOffset;
	}

	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override
	{
		m_posOffset = pos;
	}

	DirectX::SimpleMath::Quaternion GetRotationOffset() override
	{
		return m_rotOffset;
	}

	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override
	{
		m_rotOffset = rotation;
	}


	//Id
	unsigned int GetControllerID() const
	{
		return m_controllerID;
	}

	void SetControllerID(unsigned int id)
	{
		m_controllerID = id;
	}

	//�̵� ��
	bool IsOnMove() const
	{
		return m_bOnMove;
	}

	void SetOnMove(bool isMove)
	{
		m_bOnMove = isMove;
	}

	float GetFinalMultiplierSpeed() const
	{
		return m_fFinalMultiplierSpeed;
	}

	void AddFinalMultiplierSpeed(float speed)
	{
		m_fFinalMultiplierSpeed += speed;
	}

	float GetBaseSpeed() const
	{
		return m_fBaseSpeed;
	}

	std::array<bool, 4> GetMoveRestrict() const
	{
		return m_bMoveRestrict;
	}

	void SetMoveRestrict(const std::array<bool, 4> & restrict)
	{
		m_bMoveRestrict = restrict;
	}





	//collision ī��Ʈ
	unsigned int GetCollisionCount() const
	{
		return m_collsionCount;
	}

private: 

	//collision event
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;


	Transform* m_transform;

	//��Ʈ�ѷ� ����
	CharacterControllerInfo m_controllerInfo;
	//�����Ʈ ����
	CharacterMovementInfo m_movementInfo;

	//��Ʈ�ѷ� ���̵�
	unsigned int m_controllerID{ 0 };
	
	//collision
	unsigned int m_collsionCount{ 0 };

	bool m_bIsFall{ false }; //���������� üũ
	bool m_bOnMove{ false }; //�̵������� üũ
	bool m_bHasInput{ false }; //�Է°��� �ִ��� üũ

	float m_fBaseSpeed{ 0.0f }; //�⺻ �ӵ�
	float m_fBaseAcceleration{ 0.0f }; //�⺻ ���ӵ�
	float m_fFinalMultiplierSpeed{ 0.0f }; //���� �ӵ�


	//�̵� ����
	std::array<bool, 4> m_bMoveRestrict;

};
