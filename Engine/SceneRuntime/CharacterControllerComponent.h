#pragma once
#include "SceneManager.h"
#include "Entity.h"
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../physics/Physx.h"
#include "../Physics/ICollider.h"
#include "RigidBodyComponent.h"
#include "InputManager.h"
#include <mathematics/vector2.hpp>
#include "Scene.h"

class [[reflgen::reflect]] CharacterControllerComponent : public meta::identity<CharacterControllerComponent, Component>, public ICollider
{
   friend struct reflgen::access;
   public:
public:
	CharacterControllerComponent() = default;

   void OnInitialized() override
   {
	   auto scene = GetOwner()->m_ownerScene;
	   if (scene)
	   {
		   scene->CollectColliderComponent(this);
	   }
   }

   void OnBeginSimulation() override;

   void OnUninitializing() override
   {
	   auto scene = GetOwner()->m_ownerScene;
	   if (scene)
	   {
		   scene->UnCollectColliderComponent(this);
	   }
   }

   // íŠ¸ë™ C3: FixedUpdate/LateUpdate ê°€ìƒ ì˜¤ë²„ë¼ì´ë“œë¥¼ ê±·ì–´ë‚´ê³ 
   // CharacterControllerSystem(ì¡°ë°€ ë²¡í„°, ì „ìš© í‹±)ìœ¼ë¡œ ì˜®ê²¼ë‹¤. OnInitialized/OnUninitializing(ìœ„,
   // ì½œë¼ì´ë” ë“±ë¡ìš©)ëŠ” ì´ íŠ¸ë™ ë²”ìœ„ ë°–ì´ë¼ ê·¸ëŒ€ë¡œ ë‘”ë‹¤. ë“±ë¡/í•´ì§€ëŠ” ì”¬
   // í¸ì…/ì´íƒˆ í›…(OnAddedToScene/OnRemovingFromScene, DDOL ì•ˆì „ ê·¼ê±°ëŠ”
   // CharacterControllerSystem.h ì£¼ì„ ì°¸ê³ )ìœ¼ë¡œ í•œë‹¤. ì•„ë˜ OnFixedUpdate/
   // OnLateUpdate ëª¸í†µì€ ê·¸ëŒ€ë¡œ ë‚¨ê²¨ CharacterControllerSystemì´ ì§ì ‘ ë¶€ë¥¸ë‹¤
   // (ê°€ìƒ ì˜¤ë²„ë¼ì´ë“œê°€ ì•„ë‹ˆë¼ í‰ë²”í•œ ë©¤ë²„ í•¨ìˆ˜ë¼ LifecycleRegistry::
   // MaskOfTypeì´ ì•”ë¬µ êµ¬ë…ìœ¼ë¡œ ë‹¤ì‹œ ì¡ì§€ ì•ŠëŠ”ë‹¤).
   void OnAddedToScene() override;
   void OnRemovingFromScene() override;

	void Move(const math::vector2& moveInput)
	{
		m_moveInput = moveInput;
	}

	//==========================
	//¾À ³»ºÎ¿¡¼­ ½ÇÇà	µÇ´Â ÇÔ¼öµé
	void OnFixedUpdate(float fixedDeltaTime);
	void OnLateUpdate(float fixedDeltaTime);

	//ÄÁÆ®·Ñ·¯ Á¤º¸ ¹İÈ¯
	CharacterControllerInfo GetControllerInfo() 
	{
		m_controllerInfo.radius = m_radius;
		m_controllerInfo.height = m_height;
		return m_controllerInfo;
	}

	//ÄÁÆ®·Ñ·¯ Á¤º¸ ¼³Á¤
	void SetControllerInfo(const CharacterControllerInfo& info)
	{
		m_controllerInfo = info;
		m_controllerInfo.contactOffset = std::max(m_controllerInfo.contactOffset, 0.0001f);
	}

	//ÄÁÆ®·Ñ·¯ ÀÌµ¿ Á¤	º¸ ¹İÈ¯
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
	//ÄÁÆ®·Ñ·¯ ÀÌµ¿ Á¤º¸ ¼³Á¤
	void SetMovementInfo(const CharacterMovementInfo& info)
	{
		m_movementInfo = info;
	}

	//falling »óÅÂÀÎÁö Ã¼Å©
	bool IsFalling() const
	{
		return m_bIsFall;
	}
	void SetFalling(bool isFall)
	{
		m_bIsFall = isFall;
	}

	//offset
	math::vector3 GetPositionOffset() override
	{
		return m_posOffset;
	}

	void SetPositionOffset(math::vector3 pos) override
	{
		m_posOffset = pos;
	}

	math::quaternion GetRotationOffset() override
	{
		return m_rotOffset;
	}

	void SetRotationOffset(math::quaternion rotation) override
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

	//ÀÌµ¿ Áß
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

	//collision Ä«¿îÆ®
	unsigned int GetCollisionCount() const
	{
		return m_collsionCount;
	}

	//¼ø°£ÀÌµ¿ ÇØ´ç Æ÷Áö¼Ç À§Ä¡·Î °­Á¦ÀÌµ¿
	void ForcedSetPosition(const math::vector3& pos);
	// CCTÀÇ ÀÚµ¿ È¸Àü ±â´ÉÀ» ÄÑ°Å³ª ²ü´Ï´Ù.
	void SetAutomaticRotation(bool useAuto);

	// ³Ë¹é, ´ë½Ã µî ÀÏÈ¸¼º °­Á¦ ÀÌµ¿À» '¿äÃ»'ÇÕ´Ï´Ù.
	void TriggerForcedMove(const math::vector3& initialVelocity,
		float duration = 0.0f);

	// ÁøÇà ÁßÀÎ °­Á¦ ÀÌµ¿À» '¿äÃ»'ÇÏ¿© ÁßÁö½ÃÅµ´Ï´Ù.
	void StopForcedMove();

	bool IsInForcedMove() const;

	void SetLookDirection(const math::vector3& direction);
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
	math::vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	float m_radius = 0.55f;
	math::quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };
	[[reflgen::ignore]] math::vector2 m_moveInput{ 0.0f, 0.0f };
	float m_height = 2.f;

	float maxSpeed = 1.025f;	//ÃÖ´ë ¼Óµµ //&&&&&speed
	float acceleration = 1.0f;	//°¡¼Óµµ
	float staticFriction = 0.4f;	//Á¤Àû ¹°Ã¼ ¸¶Âû °è¼ö
	float dynamicFriction = 0.1f;	//µ¿Àû ¹°Ã¼ ¸¶Âû °è¼ö
	float jumpSpeed = 0.05f;	//Á¡ÇÁ ¼Óµµ
	float gravityWeight = 0.2f;	//Áß·Â °¡¼Óµµ

private:
	[[reflgen::ignore]] bool m_bIsFall{ false }; //³«ÇÏÁßÀÎÁö Ã¼Å©
	[[reflgen::ignore]] bool m_bOnMove{ false }; //ÀÌµ¿ÁßÀÎÁö Ã¼Å©
	[[reflgen::ignore]] bool m_bHasInput{ false }; //ÀÔ·Â°ªÀÌ ÀÖ´ÂÁö Ã¼Å©
	[[reflgen::ignore]] bool m_useAutomaticRotation{ true }; // ÀÚµ¿ È¸Àü ±â´É »ç¿ë ¿©ºÎ

	[[reflgen::ignore]] math::vector3 m_lookDirection;
	[[reflgen::ignore]] bool m_hasCustomLookDirection = false;

	[[reflgen::ignore]] Transform* m_transform;
	//ÄÁÆ®·Ñ·¯ Á¤º¸
	[[reflgen::ignore]] CharacterControllerInfo m_controllerInfo;
	//¹«ºê¸ÕÆ® Á¤º¸
	[[reflgen::ignore]] CharacterMovementInfo m_movementInfo;
	//ÄÁÆ®·Ñ·¯ ¾ÆÀÌµğ
	[[reflgen::ignore]] unsigned int m_controllerID{ 0 };
	//collision
	[[reflgen::ignore]] unsigned int m_collsionCount{ 0 };
	float m_fBaseSpeed{ 0.025f }; //±âº» ¼Óµµ
	[[reflgen::ignore]] float PreSpeed = m_fBaseSpeed;
	[[reflgen::ignore]] float m_speed = 0.f; //º¯ÇØ¼­ ¾µ½ºÇÇµå
	[[reflgen::ignore]] float m_fBaseAcceleration{ 1.0f }; //±âº» °¡¼Óµµ
	float m_fFinalMultiplierSpeed{ 1.0f }; //ÃÖÁ¾ ¼Óµµ
	[[reflgen::ignore]] float JumpPower = 0.f; //Á¡ÇÁ³ª ³Ë¹é½Ã À§·Î¶ãÈû
	[[reflgen::ignore]] math::vector3 preRotation;
	float m_rotationSpeed{ 0.1f }; //È¸Àü ¼Óµµ
	//ÀÌµ¿ Á¦ÇÑ
	[[reflgen::ignore]] std::array<bool, 4> m_bMoveRestrict;

	[[reflgen::ignore]] EColliderType m_type{ EColliderType::COLLISION };
	// IColliderÀ»(¸¦) ÅëÇØ »ó¼ÓµÊ
	void SetColliderType(EColliderType type) override
	{
		m_type = type;
	}

	EColliderType GetColliderType() const override 
	{
		return m_type;
	}

};
