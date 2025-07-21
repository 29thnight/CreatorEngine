#pragma once
#include "SceneManager.h"
#include "GameObject.h"
#include "Component.h"
#include "IAwakable.h"
#include "IOnDestroy.h"
#include "IStartable.h"
#include "IFixedUpdatable.h"
#include "ILateUpdatable.h"
#include "../physics/PhysicsCommon.h"
#include "../physics/Physx.h"
#include "../Physics/ICollider.h"
#include "RigidBodyComponent.h"
#include "InputManager.h"
#include "directxtk\SimpleMath.h"
#include "Scene.h"
#include "CharacterControllerComponent.generated.h"

class CharacterControllerComponent : public Component, public ICollider, public IAwakable, public IOnDestroy, public IStartable, public IFixedUpdatable, public ILateUpdatable
{
public:
   ReflectCharacterControllerComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(CharacterControllerComponent)

   void Awake() override
   {
	   auto scene = SceneManagers->GetActiveScene();
	   if (scene)
	   {
		   scene->CollectColliderComponent(this);
	   }
   }

   void Start() override
   {
	   OnStart();
   }

   void FixedUpdate(float fixedDeltaTime) override
   {
	   OnFixedUpdate(fixedDeltaTime);
   }

   void LateUpdate(float fixedDeltaTime) override
   {
	   OnLateUpdate(fixedDeltaTime);
   }

   void OnDestroy() override
   {
	   auto scene = SceneManagers->GetActiveScene();
	   if (scene)
	   {
		   scene->UnCollectColliderComponent(this);
	   }
   }

	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	
	
	DirectX::SimpleMath::Vector2 m_moveInput{ 0.0f, 0.0f };
	
	void Move(const DirectX::SimpleMath::Vector2& moveInput)
	{
		m_moveInput = moveInput;
	}

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

	void Stun(float stunTime);
	void SetKnockBack(float KnockBackPower, float yKnockBackPower);
	void EndKnockBack();
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

	bool  m_isKnockBack = false;
	bool  m_isStun = false;
	float m_stunTime = 0.f;
	float stunElapsedTime = 0.f;
	[[Property]]
	float m_fBaseSpeed{ 0.025f }; //�⺻ �ӵ�
	float PreSpeed = m_fBaseSpeed;
	float m_speed = 0.f; //���ؼ� �����ǵ�
	float m_fBaseAcceleration{ 1.0f }; //�⺻ ���ӵ�
	[[Property]]
	float m_fFinalMultiplierSpeed{ 1.0f }; //���� �ӵ�
	float JumpPower = 0.f; //������ �˹�� ���ζ���
	Mathf::Vector3 preRotation;

	[[Property]]
	float m_rotationSpeed{ 0.1f }; //ȸ�� �ӵ�

	//�̵� ����
	std::array<bool, 4> m_bMoveRestrict;

};
