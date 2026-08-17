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

class CharacterControllerComponent : public meta::identity<CharacterControllerComponent, Component>, public ICollider
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_posOffset>,
           meta::field<&Self::m_radius>,
           meta::field<&Self::m_rotOffset>,
           meta::field<&Self::m_height>,
           meta::field<&Self::maxSpeed>,
           meta::field<&Self::acceleration>,
           meta::field<&Self::staticFriction>,
           meta::field<&Self::dynamicFriction>,
           meta::field<&Self::jumpSpeed>,
           meta::field<&Self::gravityWeight>,
           meta::field<&Self::m_fBaseSpeed>,
           meta::field<&Self::m_fFinalMultiplierSpeed>,
           meta::field<&Self::m_rotationSpeed>);
   }
public:
	CharacterControllerComponent() = default;

   void Awake() override
   {
	   auto scene = GetOwner()->m_ownerScene;
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
	   auto scene = GetOwner()->m_ownerScene;
	   if (scene)
	   {
		   scene->UnCollectColliderComponent(this);
	   }
   }
	
	void Move(const DirectX::SimpleMath::Vector2& moveInput)
	{
		m_moveInput = moveInput;
	}

	//==========================
	//씬 내부에서 실행	되는 함수들
	void OnStart();
	void OnFixedUpdate(float fixedDeltaTime);
	void OnLateUpdate(float fixedDeltaTime);

	//컨트롤러 정보 반환
	CharacterControllerInfo GetControllerInfo() 
	{
		m_controllerInfo.radius = m_radius;
		m_controllerInfo.height = m_height;
		return m_controllerInfo;
	}

	//컨트롤러 정보 설정
	void SetControllerInfo(const CharacterControllerInfo& info)
	{
		m_controllerInfo = info;
		m_controllerInfo.contactOffset = std::max(m_controllerInfo.contactOffset, 0.0001f);
	}

	//컨트롤러 이동 정	보 반환
	CharacterMovementInfo GetMovementInfo()
	{
		m_movementInfo.maxSpeed = maxSpeed;
		m_movementInfo.acceleration = acceleration;
		m_movementInfo.staticFriction = staticFriction;
		m_movementInfo.dynamicFriction = dynamicFriction;
		m_movementInfo.jumpSpeed = jumpSpeed;
		m_movementInfo.gravityWeight = gravityWeight;

		return m_movementInfo;
	}
	//컨트롤러 이동 정보 설정
	void SetMovementInfo(const CharacterMovementInfo& info)
	{
		m_movementInfo = info;
	}

	//falling 상태인지 체크
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

	//이동 중
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

	void SetBaseSpeed(float speed)
	{
		m_fBaseSpeed = speed;
	}
	std::array<bool, 4> GetMoveRestrict() const
	{
		return m_bMoveRestrict;
	}

	void SetMoveRestrict(const std::array<bool, 4> & restrict)
	{
		m_bMoveRestrict = restrict;
	}

	//collision 카운트
	unsigned int GetCollisionCount() const
	{
		return m_collsionCount;
	}

	//순간이동 해당 포지션 위치로 강제이동
	void ForcedSetPosition(const DirectX::SimpleMath::Vector3& pos);
	// CCT의 자동 회전 기능을 켜거나 끕니다.
	void SetAutomaticRotation(bool useAuto);

	// 넉백, 대시 등 일회성 강제 이동을 '요청'합니다.
	void TriggerForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration=0.0f, Mathf::Easing::EaseType curveType = Mathf::Easing::EaseType::None);
	//void TriggerForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration = 0.0f);

	// 진행 중인 강제 이동을 '요청'하여 중지시킵니다.
	void StopForcedMove();

	bool IsInForcedMove() const;

	void SetLookDirection(const DirectX::SimpleMath::Vector3& direction);
	void ClearLookDirection();
private: 

	//collision event
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;

public:
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	float m_radius = 0.55f;
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };
	DirectX::SimpleMath::Vector2 m_moveInput{ 0.0f, 0.0f };
	float m_height = 2.f;

	float maxSpeed = 1.025f;	//최대 속도 //&&&&&speed
	float acceleration = 1.0f;	//가속도
	float staticFriction = 0.4f;	//정적 물체 마찰 계수
	float dynamicFriction = 0.1f;	//동적 물체 마찰 계수
	float jumpSpeed = 0.05f;	//점프 속도
	float gravityWeight = 0.2f;	//중력 가속도

private:
	bool m_bIsFall{ false }; //낙하중인지 체크
	bool m_bOnMove{ false }; //이동중인지 체크
	bool m_bHasInput{ false }; //입력값이 있는지 체크
	bool m_useAutomaticRotation{ true }; // 자동 회전 기능 사용 여부

	DirectX::SimpleMath::Vector3 m_lookDirection;
	bool m_hasCustomLookDirection = false;

	Transform* m_transform;
	//컨트롤러 정보
	CharacterControllerInfo m_controllerInfo;
	//무브먼트 정보
	CharacterMovementInfo m_movementInfo;
	//컨트롤러 아이디
	unsigned int m_controllerID{ 0 };
	//collision
	unsigned int m_collsionCount{ 0 };
	float m_fBaseSpeed{ 0.025f }; //기본 속도
	float PreSpeed = m_fBaseSpeed;
	float m_speed = 0.f; //변해서 쓸스피드
	float m_fBaseAcceleration{ 1.0f }; //기본 가속도
	float m_fFinalMultiplierSpeed{ 1.0f }; //최종 속도
	float JumpPower = 0.f; //점프나 넉백시 위로뜰힘
	Mathf::Vector3 preRotation;
	float m_rotationSpeed{ 0.1f }; //회전 속도
	//이동 제한
	std::array<bool, 4> m_bMoveRestrict;

	EColliderType m_type{ EColliderType::COLLISION };
	// ICollider을(를) 통해 상속됨
	void SetColliderType(EColliderType type) override
	{
		m_type = type;
	}

	EColliderType GetColliderType() const override 
	{
		return m_type;
	}

};
