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
	//씬 내부에서 실행	되는 함수들
	void OnStart();
	void OnFixedUpdate(float fixedDeltaTime);
	void OnLateUpdate(float fixedDeltaTime);

	//==========================
	Core::DelegateHandle m_onStartHandle;
	Core::DelegateHandle m_onFixedUpdateHandle;
	Core::DelegateHandle m_onLateUpdateHandle;
	//==========================



	//컨트롤러 정보 반환
	CharacterControllerInfo GetControllerInfo() const
	{
		return m_controllerInfo;
	}

	//컨트롤러 정보 설정
	void SetControllerInfo(const CharacterControllerInfo& info)
	{
		m_controllerInfo = info;
		m_controllerInfo.contactOffset = std::max(m_controllerInfo.contactOffset, 0.0001f);
	}

	//컨트롤러 이동 정	보 반환
	CharacterMovementInfo GetMovementInfo() const
	{
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

private: 

	//collision event
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;


	Transform* m_transform;

	//컨트롤러 정보
	CharacterControllerInfo m_controllerInfo;
	//무브먼트 정보
	CharacterMovementInfo m_movementInfo;

	//컨트롤러 아이디
	unsigned int m_controllerID{ 0 };
	
	//collision
	unsigned int m_collsionCount{ 0 };

	bool m_bIsFall{ false }; //낙하중인지 체크
	bool m_bOnMove{ false }; //이동중인지 체크
	bool m_bHasInput{ false }; //입력값이 있는지 체크

	float m_fBaseSpeed{ 0.0f }; //기본 속도
	float m_fBaseAcceleration{ 0.0f }; //기본 가속도
	float m_fFinalMultiplierSpeed{ 0.0f }; //최종 속도


	//이동 제한
	std::array<bool, 4> m_bMoveRestrict;

};
